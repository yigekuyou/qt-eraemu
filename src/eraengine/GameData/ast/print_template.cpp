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
QSharedPointer<CompiledPrintTemplate> compileMarkup(const QString& markup, const QList<QSharedPointer<ExpressionNode>>& expressions) {
    auto out = QSharedPointer<CompiledPrintTemplate>::create();
    QStringList stack;
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
        CompiledPrintTemplatePart part; part.style = {};
        static const QRegularExpression attrRe("([a-zA-Z][a-zA-Z0-9_-]*)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s/]+))");
        auto am = attrRe.globalMatch(token.mid(tagMatch.capturedEnd()));
        while (am.hasNext()) {
            auto a = am.next(); const QString key = a.captured(1).toLower();
            const QString value = a.captured(2).isNull() ? (a.captured(3).isNull() ? a.captured(4) : a.captured(3)) : a.captured(2);
            if (!part.attributes.contains(key)) part.attributeOrder.append(key);
            part.attributes[key] = QSharedPointer<StrFormNode>::create(fragments(value, expressions));
        }
        if (closing) {
            if (tag == "p") { part.kind = PrintTemplatePart::Kind::EndAlignment; out->parts.append(part); }
            if (tag == "nobr") { part.kind = PrintTemplatePart::Kind::EndNoWrap; out->parts.append(part); }
            if (tag == "button") { part.kind = PrintTemplatePart::Kind::EndButton; out->parts.append(part); }
            for (int i = stack.size()-1; i >= 0; --i) if (stack[i] == tag) { part.kind = PrintTemplatePart::Kind::EndStyle; part.x = stack.size() - i; out->parts.append(part); stack.resize(i); break; }
        } else if (tag == "br") { part.kind = PrintTemplatePart::Kind::Break; out->parts.append(part); }
        else if (tag == "nobr") { part.kind = PrintTemplatePart::Kind::NoWrap; out->parts.append(part); }
        else if (tag == "button" || tag == "img" || tag == "shape" || tag == "p") {
            part.kind = tag == "button" ? PrintTemplatePart::Kind::Button : tag == "img" ? PrintTemplatePart::Kind::Image : tag == "shape" ? PrintTemplatePart::Kind::Shape : PrintTemplatePart::Kind::Alignment;
            out->parts.append(part);
        } else if (tag == "font" || tag == "b" || tag == "strong" || tag == "i" || tag == "em" || tag == "u" || tag == "s") {
            stack.append(tag);
            part.kind = PrintTemplatePart::Kind::Style;
            part.text = tag; out->parts.append(part);

        }
        pos = gt+1;
    }
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
    for (const auto& compiled : source.parts) {
        PrintTemplatePart part = compiled;
        if (compiled.expression) {
            const auto value = flatString(eval(*compiled.expression, compiled.occurrenceId));
            out.parts.append(value.markup.parts);
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
        if (part.kind == PrintTemplatePart::Kind::Style) {
            part.style.color = QColor(attrs.value("color"));
            part.style.buttonColor = QColor(attrs.value("bcolor"));
            part.style.fontName = attrs.value("face");
            part.style.bold = part.text == "b" || part.text == "strong";
            part.style.italic = part.text == "i" || part.text == "em";
            part.style.underline = part.text == "u"; part.style.strike = part.text == "s";
        }
        part.buttonValue = attrs.value("value"); part.tooltip = attrs.value("title");
        if (part.kind == PrintTemplatePart::Kind::Image) { part.text = attrs.value("src"); part.imageAlt = attrs.value("srcb"); }
        if (part.kind == PrintTemplatePart::Kind::Alignment) part.text = attrs.value("align");
        part.shapeType = attrs.value("type");
        for (const auto& value : attrs.value("param").split(',', Qt::SkipEmptyParts)) part.shapeParams.append(value.trimmed().toInt());
        part.width = attrs.value("width").toInt(); part.height = attrs.value("height").toInt(); part.y = attrs.value("ypos").toInt();
        out.parts.append(std::move(part));
    }
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

bool PrintTemplateCompiler::coversText(const PrintTemplate& markup, const QString& text) {
    if (!text.contains(QLatin1Char('<'))) return true;   // 字符串里没有标签，无需解析
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
