#include "print_template.h"
#include <QRegularExpression>

namespace {
QString decode(const QString& text) {
    static const QRegularExpression entity(QStringLiteral("&(#x[0-9a-fA-F]+|#[0-9]+|lt|gt|amp|quot|apos|nbsp);"));
    QString result; qsizetype pos = 0;
    auto matches = entity.globalMatch(text);
    while (matches.hasNext()) {
        const auto m = matches.next();
        result += text.mid(pos, m.capturedStart() - pos);
        const QString name = m.captured(1);
        if (name.startsWith('#')) {
            bool ok; const bool hex = name.startsWith("#x");
            const uint cp = name.mid(hex ? 2 : 1).toUInt(&ok, hex ? 16 : 10);
            if (ok && cp > 0 && cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
                const char32_t c = cp; result += QString::fromUcs4(&c, 1);
            } else result += m.captured();
        } else {
            static const QHash<QString, QString> names{{"lt","<"},{"gt",">"},{"amp","&"},{"quot","\""},{"apos","'"},{"nbsp",QString(QChar(0xa0))}};
            result += names.value(name);
        }
        pos = m.capturedEnd();
    }
    return result + text.mid(pos);
}
// Private markers stand for AST references, never evaluated markup.
QString marker(int i) { return QString(QChar(0xfdd0)) + QString::number(i) + QChar(0xfdd1); }
QList<StrFormPart> fragments(const QString& text, const QList<QSharedPointer<ExpressionNode>>& expressions) {
    QList<StrFormPart> out;
    static const QRegularExpression re(QString(QChar(0xfdd0)) + "([0-9]+)" + QChar(0xfdd1));
    qsizetype pos = 0; auto matches = re.globalMatch(text);
    while (matches.hasNext()) {
        auto m = matches.next(); const int index = m.captured(1).toInt();
        if (index >= expressions.size()) continue;
        if (m.capturedStart() > pos) out.append(StrFormPart::makeText(decode(text.mid(pos, m.capturedStart()-pos))));
        auto part = StrFormPart::makeExpr(expressions[index]);
        part.occurrenceId = index;
        out.append(std::move(part));
        pos = m.capturedEnd();
    }
    if (pos < text.size()) out.append(StrFormPart::makeText(decode(text.mid(pos))));
    return out;
}
// 这个插值是不是「就一个变量/SFORM 读取」（没有宽度格式）：运行时只对这种洞做抽取。
bool isPlainRead(const QSharedPointer<ExpressionNode>& node) {
    if (!node) return false;
    if (node->kind() == NodeKind::Variable) return true;
    if (node->kind() != NodeKind::StrForm) return false;
    const auto& parts = static_cast<const StrFormNode&>(*node).parts();
    return parts.size() == 1 && !parts.first().width && parts.first().expression
           && isPlainRead(parts.first().expression);
}
// 各标签接受的属性（C# ConsoleDivPart / ConsoleImagePart / ConsoleButtonPart …）。
// 标签内部的「裸片段」插值（`<div %RECT% …>` 的 RECT）只能贡献这些名字：
// 装载期把标签身份和名单一起定下，运行期只是把值拆成键值对、按名单筛。
QStringList tagAttributes(const QString& tag) {
    if (tag == QLatin1String("div"))
        return {QStringLiteral("rect"), QStringLiteral("xpos"), QStringLiteral("ypos"),
                QStringLiteral("width"), QStringLiteral("height"), QStringLiteral("border"),
                QStringLiteral("bcolor"), QStringLiteral("color"), QStringLiteral("depth"),
                QStringLiteral("display"), QStringLiteral("relative"), QStringLiteral("align")};
    if (tag == QLatin1String("img"))
        return {QStringLiteral("src"), QStringLiteral("srcb"), QStringLiteral("width"),
                QStringLiteral("height"), QStringLiteral("ypos")};
    if (tag == QLatin1String("button") || tag == QLatin1String("nonbutton"))
        return {QStringLiteral("value"), QStringLiteral("title"), QStringLiteral("color"),
                QStringLiteral("bcolor"), QStringLiteral("border"), QStringLiteral("pos")};
    if (tag == QLatin1String("shape"))
        return {QStringLiteral("type"), QStringLiteral("param"), QStringLiteral("color"),
                QStringLiteral("bcolor")};
    if (tag == QLatin1String("p")) return {QStringLiteral("align")};
    if (tag == QLatin1String("font"))
        return {QStringLiteral("color"), QStringLiteral("face"), QStringLiteral("size"),
                QStringLiteral("bcolor")};
    return {};
}
QSharedPointer<CompiledPrintTemplate> compileMarkup(const QString& markup, const QList<QSharedPointer<ExpressionNode>>& expressions) {
    auto out = QSharedPointer<CompiledPrintTemplate>::create();
    QStringList stack;
    QString buttonTag;
    auto text = [&](const QString& value) {
        for (const auto& f : fragments(value, expressions)) {
            CompiledPrintTemplatePart part; part.text = f.text; part.expression = f.expression;
            part.occurrenceId = f.occurrenceId;
            part.kind = f.expression ? PrintTemplatePart::Kind::Expression : PrintTemplatePart::Kind::Text;
            out->parts.append(part);
        }
    };
    qsizetype pos = 0;
    while (pos < markup.size()) {
        const auto lt = markup.indexOf('<', pos);
        if (lt < 0) { text(markup.mid(pos)); break; }
        text(markup.mid(pos, lt-pos));
        QChar quote; qsizetype gt = lt+1;
        for (; gt < markup.size(); ++gt) {
            const auto c = markup[gt];
            if (!quote.isNull()) { if (c == quote) quote = {}; }
            else if (c == '\'' || c == '"') quote = c;
            else if (c == '>' || c == '<') break;
        }
        if (gt == markup.size()) { text(markup.mid(lt)); break; }
        // A new unquoted tag cannot complete an earlier unfinished tag.
        // Preserve that prefix and resume at the new '<', rather than swallowing
        // both tags (and any interpolations between them).
        if (markup[gt] == '<') { text(markup.mid(lt, gt-lt)); pos = gt; continue; }
        const QString token = markup.mid(lt+1, gt-lt-1).trimmed();
        static const QRegularExpression tagRe("^(/?)\\s*([a-zA-Z][a-zA-Z0-9]*)(?=\\s|/|$)");
        const auto tagMatch = tagRe.match(token);
        if (!tagMatch.hasMatch()) { text(markup.mid(lt, gt-lt+1)); pos = gt+1; continue; }
        const QString tag = tagMatch.captured(2).toLower();
        const bool closing = !tagMatch.captured(1).isEmpty();
        // 标签内部的洞分两种：
        //  · 属性**值**位置（`rect = '%POS_X%, …'`、`src = '%NAME%'`）—— 属性名在
        //    装载期已经确定，值只是普通插值，静态 part 就能承载，**不需要**任何
        //    运行期分词；
        //  · 属性**语法**位置（`<div %RECT% …>`，裸位置）—— 标签身份不变，只是
        //    属性表由值提供：装载期锁住名单（tagAttributes），运行期把值拆成
        //    `名字 = 值` 对并按名单筛，**不重新分词**。若链反向追踪/登记已把这段
        //    还原成文本，洞会落在引号内，走上面第一种。
        const QString rawTag = markup.mid(lt, gt - lt + 1);
        bool fragment = false;
        if (!closing && rawTag.contains(QChar(0xfdd0))) {
            bool quoted = false; QChar last;
            for (qsizetype k = 0; k + 1 < rawTag.size(); ++k) {
                const QChar c = rawTag.at(k);
                if (c == QChar(0xfdd0)) {
                    if (!quoted && last != QLatin1Char('=')) { fragment = true; break; }
                    while (k + 1 < rawTag.size() && rawTag.at(k) != QChar(0xfdd1)) ++k;
                    last = {};
                    continue;
                }
                if (!quoted && (c == QLatin1Char('\'') || c == QLatin1Char('"'))) quoted = true;
                else if (quoted && (c == QLatin1Char('\'') || c == QLatin1Char('"'))) quoted = false;
                if (!c.isSpace()) last = c;
            }
        }
        CompiledPrintTemplatePart part; part.style = {};
        static const QRegularExpression attrRe("([a-zA-Z][a-zA-Z0-9_-]*)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s/]+))");
        auto am = attrRe.globalMatch(token.mid(tagMatch.capturedEnd()));
        while (am.hasNext()) {
            auto a = am.next(); const QString key = a.captured(1).toLower();
            const QString value = a.captured(2).isNull() ? (a.captured(3).isNull() ? a.captured(4) : a.captured(3)) : a.captured(2);
            if (!part.attributes.contains(key)) part.attributeOrder.append(key);
            part.attributes[key] = QSharedPointer<StrFormNode>::create(fragments(value, expressions));
        }
        if (fragment) {
            // 属性**语法**位置的洞（`<div %RECT%>`）：标签身份和可接受的属性名
            // 装载期就定了（tagAttributes），运行期只把这段值拆成 `名字 = 值` 对
            // 并按名单筛一遍 —— 不重新分词，标签已经没有别的形态。
            // 链反向追踪/登记若能还原出 `rect = '…'`，洞会落在引号内，走上面
            // 属性值那条路，连这一步都不需要。
            part.tagAllowed = tagAttributes(tag);
            for (const auto& f : fragments(rawTag, expressions))
                if (f.expression) { part.tagHoles.append(f.expression); part.tagHoleIds.append(f.occurrenceId); }
        }
        if (closing) {
            // 结构标签自成一路（不进样式栈）：`</p>`/`</nobr>`/`</button>` 只对应
            // 各自的段落状态，不参与 font/b/i 的配对判定 —— 若把它们也塞进下面的
            // 栈检查，每个合法的 `</p>` 都会被误报成 UnexpectedCloseTag。
            if (tag == "p") { part.kind = PrintTemplatePart::Kind::EndAlignment; out->parts.append(part); }
            else if (tag == "nobr") { part.kind = PrintTemplatePart::Kind::EndNoWrap; out->parts.append(part); }
            else if (tag == "div") { part.kind = PrintTemplatePart::Kind::EndDivision; out->parts.append(part); }
            else if (tag == "button" || tag == "nonbutton") {
                if (buttonTag != tag) out->problems.append(QStringLiteral("UnexpectedCloseTag </%1>").arg(tag));
                buttonTag.clear();
                part.kind = PrintTemplatePart::Kind::EndButton; out->parts.append(part);
            }
            // HTML 样式按栈闭合。非栈顶或不存在的闭合标签只被消费，
            // 不得跨层弹栈；否则后续文字会继承错误的字体/颜色。
            // 但**消费不等于合法**：C# 在这里抛 UnexpectedCloseTag（栈空或与已开
            // 标签不对应，HtmlManager.cs:906-910）。词法器不打断渲染，只把问题记在
            // 模板上 —— 谁持有最终字符串，谁才能判定（装载期 verified，否则打印期
            // 整串重新分词）。未知标签（`</size>` 等）保持静默消费，与既有移植一致。
            else if (!stack.isEmpty() && stack.constLast() == tag) {
                part.kind = PrintTemplatePart::Kind::EndStyle;
                part.x = 1;
                out->parts.append(part);
                stack.removeLast();
            } else if (tag == "font" || tag == "b" || tag == "strong" || tag == "i"
                       || tag == "em" || tag == "u" || tag == "s") {
                out->problems.append(QStringLiteral("UnexpectedCloseTag </%1>").arg(tag));
            }
        } else if (tag == "br") { part.kind = PrintTemplatePart::Kind::Break; out->parts.append(part); }
        else if (tag == "nobr") { part.kind = PrintTemplatePart::Kind::NoWrap; out->parts.append(part); }
        else if (tag == "div") { part.kind = PrintTemplatePart::Kind::Division; out->parts.append(part); }
        else if (tag == "button" || tag == "nonbutton" || tag == "img" || tag == "shape" || tag == "p") {
            part.kind = tag == "button" ? PrintTemplatePart::Kind::Button : tag == "nonbutton" ? PrintTemplatePart::Kind::NonButton : tag == "img" ? PrintTemplatePart::Kind::Image : tag == "shape" ? PrintTemplatePart::Kind::Shape : PrintTemplatePart::Kind::Alignment;
            if (tag == "button" || tag == "nonbutton") {
                if (!buttonTag.isEmpty()) out->problems.append(QStringLiteral("NestedButtonTag"));
                buttonTag = tag;
            }
            out->parts.append(part);
        } else if (tag == "font" || tag == "b" || tag == "strong" || tag == "i" || tag == "em" || tag == "u" || tag == "s") {
            stack.append(tag);
            part.kind = PrintTemplatePart::Kind::Style;
            part.text = tag; out->parts.append(part);

        }
        pos = gt+1;
    }
    // 整串扫完仍有未闭合的样式标签：C# 的行为是打印时报错（Html2DisplayLine
    // 结束时校验栈），这里同样只记录、不打断渲染。
    if (!buttonTag.isEmpty()) out->problems.append(QStringLiteral("UnclosedTag <%1>").arg(buttonTag));
    if (!stack.isEmpty())
        out->problems.append(QStringLiteral("UnclosedTag <%1>").arg(stack.join(QLatin1String(","))));
    return out;
}
}
QSharedPointer<CompiledPrintTemplate> PrintTemplateCompiler::compile(const QSharedPointer<ExpressionNode>& node) {
    if (!node) return {};
    QString markup;
    QList<QSharedPointer<ExpressionNode>> expressions;
    const auto expression = [&](const QSharedPointer<ExpressionNode>& value) {
        markup += marker(expressions.size());
        expressions.append(value);
    };
    // Flatten the structural AST before scanning. In particular, a FORM can
    // complete a tag begun in a literal on either side of a string addition.
    // Interpolation leaves are opaque, even when their value is constant HTML.
    const auto append = [&](const auto& self, const QSharedPointer<ExpressionNode>& value) -> void {
        if (!value) return;
        if (value->kind() == NodeKind::Literal && value->isString()) {
            markup += static_cast<const LiteralNode&>(*value).strValue();
            return;
        }
        if (value->kind() == NodeKind::BinaryOp && value->isString()) {
            const auto& sum = static_cast<const BinaryOpNode&>(*value);
            if (sum.op().type() == TokenType::PLUS) {
                self(self, sum.left()); self(self, sum.right()); return;
            }
        }
        if (value->kind() == NodeKind::StrForm) {
            for (const auto& part : static_cast<const StrFormNode&>(*value).parts()) {
                if (!part.expression) markup += part.text;
                else if (!part.width && part.expectedType == OperandType::Unknown
                         && part.expression->kind() == NodeKind::StrForm)
                    self(self, part.expression);
                else {
                    // Keep FORM width/alignment and expected-type checks when
                    // extracting an interpolation into a template expression.
                    expression(QSharedPointer<StrFormNode>::create(QList<StrFormPart>{part}));
                }
            }
            return;
        }
        expression(value);
    };
    append(append, node);
    auto result = compileMarkup(markup, expressions);
    // Keep the raw spelling alongside compiled tags, sharing interpolation identities.
    // Raw string values retain entity spelling.
    qsizetype pos = 0;
    for (int i = 0; i < expressions.size(); ++i) {
        const auto at = markup.indexOf(marker(i), pos);
        result->rawParts.append(StrFormPart::makeText(markup.mid(pos, at-pos)));
        StrFormPart part; part.expression = expressions[i]; part.occurrenceId = i; result->rawParts.append(part);
        pos = at + marker(i).size();
    }
    result->rawParts.append(StrFormPart::makeText(markup.mid(pos)));
    return result;
}
PrintTemplate PrintTemplateCompiler::evaluateOccurrences(const CompiledPrintTemplate& source,
    const std::function<QVariant(const ExpressionNode&, int)>& eval) {
    PrintTemplate out;
    out.alignment = source.alignment; out.noWrap = source.noWrap;
    out.problems = source.problems;   // 结构问题是分词结果的一部分，随 IR 一起走
    for (const auto& compiled : source.parts) {
        PrintTemplatePart part = compiled;
        if (compiled.expression) {
            const auto value = flatString(eval(*compiled.expression, compiled.occurrenceId));
            // 正文位置的插值（`<` `>` 之外）：外层字面量不重分词，只把取到的
            // 这一串内容做「运行时抽取」—— 变量读出来本身就是一整串文本，
            // 里面若有标签（`%PORTRAIT%` = `<img …>`）就在这里成为结构。
            // FORM 字符串产出的值带结构（verified），直接并入，不再解释。
            if (isPlainRead(compiled.expression) && !value.markup.verified
                && value.text.contains(QLatin1Char('<'))) {
                for (const auto& nested : parseRuntimeMarkup(value.text).parts)
                    out.parts.append(nested);
            } else {
                out.parts.append(value.markup.parts);
            }
            continue;
        }
        QHash<QString, QString> attrs;
        for (const auto& key : compiled.attributeOrder) {
            const auto& form = static_cast<const StrFormNode&>(*compiled.attributes.value(key));
            QString value;
            for (const auto& fragment : form.parts())
                value += fragment.expression ? stringText(eval(*fragment.expression, fragment.occurrenceId)) : fragment.text;
            attrs[key] = value;
        }
        // 语法位置的洞：只贡献**属性键值对**，名字必须在装载期锁定的名单里
        //（`nonsense = 'x'` 直接忽略）；标签本身（div/img/…）早已确定。
        for (int i = 0; i < compiled.tagHoles.size(); ++i) {
            const QString text = stringText(eval(*compiled.tagHoles.at(i), compiled.tagHoleIds.value(i)));
            static const QRegularExpression pair(QStringLiteral(
                "([a-zA-Z][a-zA-Z0-9_-]*)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s/]+))"));
            auto m = pair.globalMatch(text);
            while (m.hasNext()) {
                const auto p = m.next();
                const QString key = p.captured(1).toLower();
                if (!compiled.tagAllowed.contains(key)) continue;
                attrs[key] = p.captured(2).isNull()
                                 ? (p.captured(3).isNull() ? p.captured(4) : p.captured(3))
                                 : p.captured(2);
            }
        }
        if (part.kind == PrintTemplatePart::Kind::Style) {
            part.style.color = QColor(attrs.value("color"));
            part.style.buttonColor = QColor(attrs.value("bcolor"));
            part.style.fontName = attrs.value("face");
            part.style.bold = part.text == "b" || part.text == "strong";
            part.style.italic = part.text == "i" || part.text == "em";
            part.style.underline = part.text == "u"; part.style.strike = part.text == "s";
        }
        part.buttonValue = attrs.value("value"); part.tooltip = attrs.value("title");
        if (part.kind == PrintTemplatePart::Kind::Button || part.kind == PrintTemplatePart::Kind::NonButton) {
            // Missing value makes <button> a non-clickable segment too.
            if (!attrs.contains("value")) part.kind = PrintTemplatePart::Kind::NonButton;
            if (attrs.contains("pos")) {
                part.x = attrs.value("pos").toInt(&part.lockedPosition);
                if (!part.lockedPosition) out.problems.append(QStringLiteral("AttributeCanNotInterpretNum: pos"));
            }
        }
        if (part.kind == PrintTemplatePart::Kind::Image) { part.text = attrs.value("src"); part.imageAlt = attrs.value("srcb"); }
        if (part.kind == PrintTemplatePart::Kind::Alignment) part.text = attrs.value("align");
        if (part.kind == PrintTemplatePart::Kind::Shape) {
            part.style.color = QColor(attrs.value("color"));
            part.style.buttonColor = QColor(attrs.value("bcolor"));
        }
        part.shapeType = attrs.value("type");
        for (const auto& value : attrs.value("param").split(',', Qt::SkipEmptyParts)) part.shapeParams.append(value.trimmed().toInt());
        const auto parseLength = [](const QString& raw, bool& pixels) {
            const QString value = raw.trimmed();
            pixels = value.endsWith(QStringLiteral("px"), Qt::CaseInsensitive);
            const QString number = pixels ? value.left(value.size() - 2).trimmed() : value;
            return number.toInt();
        };
        part.width = parseLength(attrs.value("width"), part.widthIsPixels);
        part.height = parseLength(attrs.value("height"), part.heightIsPixels);
        part.y = parseLength(attrs.value("ypos"), part.yposIsPixels);
        if (part.kind == PrintTemplatePart::Kind::Division) {
            part.x = parseLength(attrs.value("xpos"), part.xposIsPixels);
            const QStringList rect = attrs.value("rect").split(',');
            if (rect.size() == 4) {
                part.x = parseLength(rect[0], part.xposIsPixels);
                part.y = parseLength(rect[1], part.yposIsPixels);
                part.width = parseLength(rect[2], part.widthIsPixels);
                part.height = parseLength(rect[3], part.heightIsPixels);
            }
            // C# ConsoleDivPart：
            //   divXOffset += margin.Left + padding.Left + border.Left
            //   yOffset    += margin.Top  + padding.Top  + border.Top
            // 子内容从「rect 左上角 + 这三者」开始排；rect 原点保持不变。
            // C# Utils.ParseParam4MixedNum：1 值四边相同 / 2 值 上下,左右 /
            // 3 值 上,左右,下 / 4 值 上,右,下,左。每项都是 **MixedNum**：
            // 裸数字 = 字号百分比（num*FontSize/100），带 px 才是绝对像素。
            // 这里把两项分别存进 px / rel 两条通道，布局期再折算。
            struct Sides {
                int px[4] = {0, 0, 0, 0};   // 上,右,下,左
                int rel[4] = {0, 0, 0, 0};  // 上,右,下,左（1/100 em）
            };
            const auto sidesOf = [](QString raw) {
                Sides out;
                struct V { int value = 0; bool pixels = false; };
                auto one = [](QString v) {
                    V r;
                    v = v.trimmed();
                    if (v.endsWith(QLatin1String("px"))) { v.chop(2); r.pixels = true; }
                    bool ok = false;
                    const int n = v.toInt(&ok);
                    r.value = ok ? n : 0;
                    return r;
                };
                const QStringList f = raw.split(',', Qt::SkipEmptyParts);
                if (f.isEmpty() || f.size() > 4) return out;
                V v[4];
                switch (f.size()) {
                case 1: v[0] = one(f[0]); v[1] = v[2] = v[3] = v[0]; break;
                case 2: v[0] = one(f[0]); v[1] = one(f[1]); v[2] = v[0]; v[3] = v[1]; break;
                case 3: v[0] = one(f[0]); v[1] = one(f[1]); v[2] = one(f[2]); v[3] = v[1]; break;
                default: for (int i = 0; i < 4; ++i) v[i] = one(f.at(i)); break;
                }
                for (int i = 0; i < 4; ++i)
                    if (v[i].pixels) out.px[i] = v[i].value; else out.rel[i] = v[i].value;
                return out;
            };
            Sides margin;
            const QString marginRaw = attrs.value(QStringLiteral("margin"));
            if (!marginRaw.isEmpty()) margin = sidesOf(marginRaw);
            part.marginPx = {margin.px[0], margin.px[1], margin.px[2], margin.px[3]};
            part.marginRel = {margin.rel[0], margin.rel[1], margin.rel[2], margin.rel[3]};
            Sides inset = margin;
            for (const char* key : {"padding", "border"}) {
                const QString raw = attrs.value(QLatin1String(key));
                if (raw.isEmpty()) continue;
                const auto in = sidesOf(raw);
                for (int i = 0; i < 4; ++i) { inset.px[i] += in.px[i]; inset.rel[i] += in.rel[i]; }
            }
            part.insetPxSum = {inset.px[0], inset.px[1], inset.px[2], inset.px[3]};
            part.insetRelSum = {inset.rel[0], inset.rel[1], inset.rel[2], inset.rel[3]};
            // C# BoxBorder.DrawBorder：div 自身先画背景填充（color），
            // 再在 rect 内侧画四边边框（bcolor + border 宽度），最后才画子内容。
            part.fillColor = QColor(attrs.value("color"));
            part.depth = attrs.value("depth").trimmed().toInt();
            const auto borderBox = sidesOf(attrs.value("border"));
            part.borderPx = {qMax(0, borderBox.px[0]), qMax(0, borderBox.px[1]),
                             qMax(0, borderBox.px[2]), qMax(0, borderBox.px[3])};
            part.borderRel = {qMax(0, borderBox.rel[0]), qMax(0, borderBox.rel[1]),
                              qMax(0, borderBox.rel[2]), qMax(0, borderBox.rel[3])};
            // bcolor 与 border 一样是 CSS 1~4 值简写（C# ParseParam4MixedNum 的
            // 展开规则同样适用于 AddColorParam4）：上 / 上下、左右 / 上、左右、下 /
            // 上、右、下、左。全无效 = 没有边框色（不画边）。
            const auto colorsOf = [](const QString& raw) {
                QStringList f = raw.split(',');
                for (QString& v : f) v = v.trimmed();
                if (f.isEmpty() || f.size() > 4) return QList<QColor>{};
                QList<QColor> four;
                switch (f.size()) {
                case 1: four = {QColor(f[0]), QColor(f[0]), QColor(f[0]), QColor(f[0])}; break;
                case 2: four = {QColor(f[0]), QColor(f[1]), QColor(f[0]), QColor(f[1])}; break;
                case 3: four = {QColor(f[0]), QColor(f[1]), QColor(f[2]), QColor(f[1])}; break;
                default: four = {QColor(f[0]), QColor(f[1]), QColor(f[2]), QColor(f[3])}; break;
                }
                bool any = false;
                for (const QColor& c : four) any = any || c.isValid();
                return any ? four : QList<QColor>{};
            };
            part.borderColors = colorsOf(attrs.value("bcolor"));
            // radius：与 border 同一套 CSS 1~4 值简写（C# ParseParam4MixedNum ->
            // Corner.LeftTop/RightTop/LeftBottom/RightBottom），裸数字同样按
            // 字号百分比折算；带 px 才是绝对像素。
            const auto radiusBox = sidesOf(attrs.value("radius"));
            const int radiusTotal = radiusBox.px[0] + radiusBox.px[1] + radiusBox.px[2]
                                    + radiusBox.px[3] + radiusBox.rel[0] + radiusBox.rel[1]
                                    + radiusBox.rel[2] + radiusBox.rel[3];
            if (radiusTotal != 0) {
                part.radiusPx = {qMax(0, radiusBox.px[0]), qMax(0, radiusBox.px[1]),
                                 qMax(0, radiusBox.px[2]), qMax(0, radiusBox.px[3])};
                part.radiusRel = {qMax(0, radiusBox.rel[0]), qMax(0, radiusBox.rel[1]),
                                  qMax(0, radiusBox.rel[2]), qMax(0, radiusBox.rel[3])};
            }
        }
        out.parts.append(std::move(part));
    }
    // C# checks positioned segments against the final HTML line flags.
    bool hasPos = false, hasNoWrap = false;
    QString alignment = "left";
    for (const auto& part : out.parts) {
        hasPos |= part.lockedPosition;
        hasNoWrap |= part.kind == PrintTemplatePart::Kind::NoWrap;
        if (part.kind == PrintTemplatePart::Kind::Alignment) alignment = part.text.toLower();
    }
    if (hasPos && !hasNoWrap) out.problems.append(QStringLiteral("CanNotUsePosWithoutNobr"));
    if (hasPos && alignment != "left") out.problems.append(QStringLiteral("CanOnlyUsePosAssignmentLR"));
    return out;
}

namespace {
// 文本里是否存在「未闭合的 '<'」：被 `+=` 拼断的标签（`<img src='x' height='500'`
// 接 `ypos='201'>`）、或字符串函数拼出来的半截标签。这类 '<' 是编译期没能解释掉的
// 标签材料（compile 会把它当普通文本片段保留）。
bool hasUnterminatedTag(const QString& text) {
    for (int i = text.indexOf(QLatin1Char('<')); i >= 0; i = text.indexOf(QLatin1Char('<'), i + 1)) {
        if (text.indexOf(QLatin1Char('>'), i) < 0) return true;
    }
    return false;
}
} // namespace

PrintTemplate PrintTemplateCompiler::parseRuntimeMarkup(const QString& text) {
    // 同一 compileMarkup 词法；表达式集为空 -> fragments() 把全文当字面量，
    // 属性值（src/width/value/title…）也全部是纯文本（对齐 C# 打印期解析，
    // 该路径没有可再求值的东西）。实体（& 等）在此按 decode() 还原 ——
    // 字符串值保留实体拼写（print_template.h 的约定），正好对上。
    const auto compiled = compileMarkup(text, {});
    return evaluateOccurrences(*compiled,
                               [](const ExpressionNode&, int) { return QVariant(); });
}

bool PrintTemplateCompiler::coversText(const PrintTemplate& markup, const QString& text) {
    if (!text.contains(QLatin1Char('<'))) return true;   // 字符串里没有标签，无需解析
    // IR 里拼进过未经 compileMarkup 的纯文本（拼接/中转/读档）：
    // 「没有未闭合标签」不再能证明结构完整 —— 完整标签也可能躺在未扫描的 Text 里
    // （实测 `sPrint_Text += @"</font>"` 就整段留在了 Text part，打印成字面量）。
    // 此时按 C# 的做法对最终字符串重新分词。
    if (!markup.verified) return false;
    bool structured = false;
    for (const PrintTemplatePart& part : markup.parts) {
        if (part.kind != PrintTemplatePart::Kind::Text) { structured = true; continue; }
        if (hasUnterminatedTag(part.text)) return false;
    }
    // 一个结构化片段都没有 = 整个串从未被解析过（裸文本/字符串函数产物）：
    // 里面若有完整标签也必须在打印时解析。
    return structured;
}

void PrintTemplateCompiler::prepare(const QSharedPointer<ExpressionNode>& node) {
    if (!node) return;
    switch (node->kind()) {
    case NodeKind::BinaryOp: {
        const auto& n = static_cast<const BinaryOpNode&>(*node);
        prepare(n.left()); prepare(n.right()); break;
    }
    case NodeKind::Function:
        for (const auto& arg : static_cast<const FunctionNode&>(*node).arguments()) prepare(arg);
        break;
    case NodeKind::If: {
        const auto& n = static_cast<const IfNode&>(*node);
        prepare(n.condition()); prepare(n.thenExpr()); prepare(n.elseExpr()); break;
    }
    default: break;
    }
    const bool sum = node->kind() == NodeKind::BinaryOp
        && static_cast<const BinaryOpNode&>(*node).op().type() == TokenType::PLUS;
    if (node->kind() == NodeKind::StrForm || (node->isString() &&
        (node->kind() == NodeKind::Literal || sum))) {
        const auto compiled = compile(node);
        for (const auto& part : compiled->parts) {
            if (part.kind != PrintTemplatePart::Kind::Text && part.kind != PrintTemplatePart::Kind::Expression) {
                node->markupTemplate = compiled; break;
            }
        }
    }
}
