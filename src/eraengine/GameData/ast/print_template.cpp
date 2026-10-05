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
        out.append(StrFormPart::makeExpr(expressions[index])); pos = m.capturedEnd();
    }
    if (pos < text.size()) out.append(StrFormPart::makeText(decode(text.mid(pos))));
    return out;
}
QSharedPointer<PrintTemplate> compileMarkup(const QString& markup, const QList<QSharedPointer<ExpressionNode>>& expressions) {
    auto out = QSharedPointer<PrintTemplate>::create();
    QStringList stack;
    auto text = [&](const QString& value) {
        for (const auto& f : fragments(value, expressions)) {
            PrintTemplatePart part; part.text = f.text; part.expression = f.expression;
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
            else if (c == '>') break;
        }
        if (gt == markup.size()) { text(markup.mid(lt)); break; }
        const QString token = markup.mid(lt+1, gt-lt-1).trimmed();
        static const QRegularExpression tagRe("^(/?)\\s*([a-zA-Z][a-zA-Z0-9]*)");
        const auto tagMatch = tagRe.match(token);
        if (!tagMatch.hasMatch()) { text(markup.mid(lt, gt-lt+1)); pos = gt+1; continue; }
        const QString tag = tagMatch.captured(2).toLower();
        const bool closing = !tagMatch.captured(1).isEmpty();
        PrintTemplatePart part; part.style = {};
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
QSharedPointer<PrintTemplate> PrintTemplateCompiler::compile(const QString& markup) { return compileMarkup(markup, {}); }
QSharedPointer<PrintTemplate> PrintTemplateCompiler::compile(const QSharedPointer<ExpressionNode>& node) {
    if (!node) return {};
    if (node->kind() == NodeKind::Literal && node->isString()) return compile(static_cast<const LiteralNode&>(*node).strValue());
    if (node->kind() != NodeKind::StrForm) return {};
    QString markup; QList<QSharedPointer<ExpressionNode>> expressions;
    for (const auto& part : static_cast<const StrFormNode&>(*node).parts()) {
        if (part.expression) { markup += marker(expressions.size()); expressions.append(part.width ? QSharedPointer<ExpressionNode>(QSharedPointer<StrFormNode>::create(QList<StrFormPart>{part})) : part.expression); }
        else markup += part.text;
    }
    return compileMarkup(markup, expressions);
}
PrintTemplate PrintTemplateCompiler::evaluate(const PrintTemplate& source, const std::function<QString(const ExpressionNode&)>& eval) {
    PrintTemplate out = source;
    for (auto& part : out.parts) {
        if (part.expression) { part.text = eval(*part.expression); part.expression.clear(); part.kind = PrintTemplatePart::Kind::Text; }
        QHash<QString, QString> attrs;
        for (const auto& key : part.attributeOrder) {
            const auto& form = static_cast<const StrFormNode&>(*part.attributes.value(key));
            QString value;
            for (const auto& fragment : form.parts()) value += fragment.expression ? eval(*fragment.expression) : fragment.text;
            attrs[key] = value;
        }
        part.attributeOrder.clear();
        part.attributes.clear();
        if (part.kind == PrintTemplatePart::Kind::Style) {
            part.style.color = QColor(attrs.value("color"));
            part.style.buttonColor = QColor(attrs.value("bcolor"));
            part.style.fontName = attrs.value("face");
            part.style.bold = part.text == "b" || part.text == "strong";
            part.style.italic = part.text == "i" || part.text == "em";
            part.style.underline = part.text == "u"; part.style.strike = part.text == "s";
        }
        part.buttonValue = attrs.value("value"); part.tooltip = attrs.value("title");
        if (part.kind == PrintTemplatePart::Kind::Image) {
            part.text = attrs.value("src");
            // <img srcb='...'>：按钮选中/悬停态替换图（C# ButtonResourceName）
            part.imageAlt = attrs.value("srcb");
        }
        if (part.kind == PrintTemplatePart::Kind::Alignment) part.text = attrs.value("align");
        part.shapeType = attrs.value("type");
        for (const auto& value : attrs.value("param").split(',', Qt::SkipEmptyParts)) part.shapeParams.append(value.trimmed().toInt());
        part.width = attrs.value("width").toInt(); part.height = attrs.value("height").toInt(); part.y = attrs.value("ypos").toInt();
    }
    return out;
}
