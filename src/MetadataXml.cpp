#include <tekla/db1/Catalogs.hpp>
#include "BinaryIO.hpp"

// Private symbols: a consumer may also link its own TinyXML2.
#define tinyxml2 tekla_db1_tinyxml2
#include "../third_party/tinyxml2/tinyxml2.h"
#undef tinyxml2

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace tekla::db1
{
namespace
{
namespace xml = tekla_db1_tinyxml2;
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
std::string trimXml(std::string text)
{
    const auto first = std::find_if_not(text.begin(), text.end(), space);
    const auto last = std::find_if_not(text.rbegin(), text.rend(), space).base();
    return first < last ? std::string(first, last) : std::string{};
}
bool xmlCharacter(std::uint32_t c)
{
    return c == 9 || c == 10 || c == 13 || (c >= 0x20 && c <= 0xd7ff) ||
           (c >= 0xe000 && c <= 0xfffd) || (c >= 0x10000 && c <= 0x10ffff);
}
void validateUtf8(const std::string& text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto c = static_cast<unsigned char>(text[i++]);
        std::uint32_t value = c, minimum = 0;
        unsigned following = 0;
        if (c >= 0xc2 && c <= 0xdf) { value = c & 31; following = 1; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { value = c & 15; following = 2; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { value = c & 7; following = 3; minimum = 0x10000; }
        else if (c >= 0x80) throw std::runtime_error("metadata requires valid UTF-8");
        for (unsigned j = 0; j < following; ++j)
        {
            if (i == text.size() || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
                throw std::runtime_error("metadata requires valid UTF-8");
            value = (value << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        }
        if (value < minimum || !xmlCharacter(value))
            throw std::runtime_error("invalid XML character in metadata");
    }
}
void appendUtf8(std::string& out, std::uint32_t c)
{
    if (c < 0x80) out += static_cast<char>(c);
    else if (c < 0x800)
    { out += static_cast<char>(0xc0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 63)); }
    else if (c < 0x10000)
    { out += static_cast<char>(0xe0 | (c >> 12)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
    else
    { out += static_cast<char>(0xf0 | (c >> 18)); out += static_cast<char>(0x80 | ((c >> 12) & 63)); out += static_cast<char>(0x80 | ((c >> 6) & 63)); out += static_cast<char>(0x80 | (c & 63)); }
}
// TinyXML2 intentionally tolerates unknown entities. Decode with entities
// disabled in the DOM, so unsupported/invalid references cannot look valid.
std::string decode(const char* value)
{
    const std::string text(value);
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] != '&') { out += text[i]; continue; }
        const auto end = text.find(';', i + 1);
        if (end == std::string::npos) throw std::runtime_error("unterminated XML entity in metadata");
        const auto entity = text.substr(i + 1, end - i - 1);
        if (entity == "amp") out += '&';
        else if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "quot") out += '"';
        else if (entity == "apos") out += '\'';
        else if (!entity.empty() && entity[0] == '#')
        {
            const bool hex = entity.size() > 1 && entity[1] == 'x';
            const std::size_t start = hex ? 2 : 1;
            const unsigned base = hex ? 16 : 10;
            std::uint32_t c = 0;
            if (start == entity.size()) throw std::runtime_error("empty XML character reference");
            for (std::size_t j = start; j < entity.size(); ++j)
            {
                const char ch = entity[j];
                const unsigned digit = ch >= '0' && ch <= '9' ? unsigned(ch - '0') :
                    ch >= 'a' && ch <= 'f' ? unsigned(ch - 'a' + 10) :
                    ch >= 'A' && ch <= 'F' ? unsigned(ch - 'A' + 10) : 16;
                if (digit >= base || c > (0x10ffff - digit) / base)
                    throw std::runtime_error("invalid XML character reference");
                c = c * base + digit;
            }
            if (!xmlCharacter(c)) throw std::runtime_error("invalid XML character reference");
            appendUtf8(out, c);
        }
        else throw std::runtime_error("unsupported XML entity in metadata");
        i = end;
    }
    return out;
}
void validateNodes(const xml::XMLNode& node)
{
    if (const auto* element = node.ToElement())
        for (const auto* attr = element->FirstAttribute(); attr; attr = attr->Next())
            (void)decode(attr->Value());
    for (const auto* child = node.FirstChild(); child; child = child->NextSibling())
    {
        if (const auto* text = child->ToText())
        { if (!text->CData()) (void)decode(text->Value()); }
        else if (child->ToUnknown() || child->ToDeclaration())
            throw std::runtime_error("unsupported declaration inside metadata root");
        validateNodes(*child);
    }
}
std::string field(const xml::XMLElement& model, const char* name)
{
    const auto* element = model.FirstChildElement(name);
    if (!element) return {};
    if (element->NextSiblingElement(name))
        throw std::runtime_error(std::string("duplicate metadata field: ") + name);
    std::string result;
    for (const auto* child = element->FirstChild(); child; child = child->NextSibling())
    {
        if (const auto* text = child->ToText())
            result += text->CData() ? text->Value() : decode(text->Value());
        else if (!child->ToComment())
            throw std::runtime_error(std::string("non-scalar metadata field: ") + name);
    }
    return result;
}
void containerText(const xml::XMLElement& element)
{
    for (const auto* node = element.FirstChild(); node; node = node->NextSibling())
        if (node->ToText() && !trimXml(node->Value()).empty())
            throw std::runtime_error("unexpected text in metadata container");
}
void declaration(const char* value)
{
    const std::string text(value);
    if (text.size() < 4 || text.compare(0, 3, "xml") != 0 || !space(text[3]))
        throw std::runtime_error("unsupported metadata processing instruction");
    xml::XMLDocument attributes(false);
    const auto fragment = "<declaration " + text.substr(4) + "/>";
    if (attributes.Parse(fragment.c_str()) != xml::XML_SUCCESS)
        throw std::runtime_error("invalid metadata XML declaration");
    const auto* element = attributes.RootElement();
    const char* version = element->Attribute("version");
    if (!version || std::strcmp(version, "1.0") != 0)
        throw std::runtime_error("metadata requires XML version 1.0");
    for (const auto* attr = element->FirstAttribute(); attr; attr = attr->Next())
    {
        const std::string name(attr->Name()), content(attr->Value());
        if (name == "encoding")
        {
            auto encoding = content;
            std::transform(encoding.begin(), encoding.end(), encoding.begin(), [](char c) {
                return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
            });
            if (encoding != "UTF-8") throw std::runtime_error("metadata requires UTF-8 encoding");
        }
        else if (name == "standalone")
        { if (content != "yes" && content != "no") throw std::runtime_error("invalid standalone declaration"); }
        else if (name != "version") throw std::runtime_error("unsupported XML declaration attribute");
    }
}
}

bool parseModelMetadata(const std::filesystem::path& path, ModelMetadata& result, std::string& error)
{
    result = {};
    error.clear();
    try
    {
        const auto bytes = detail::readPayload(path, 16 * 1024 * 1024);
        ModelMetadata parsed;
        parsed.rawXml.assign(bytes.begin(), bytes.end());
        validateUtf8(parsed.rawXml);
        // XML 1.0 folds CR and CRLF, but not LFCR. Normalize before the DOM
        // parser (which also folds LFCR), retaining the exact source separately.
        std::string normalized;
        normalized.reserve(parsed.rawXml.size());
        for (std::size_t i = 0; i < parsed.rawXml.size(); ++i)
        {
            const char c = parsed.rawXml[i];
            normalized += c == '\r' ? '\n' : c;
            if (c == '\r' && i + 1 < parsed.rawXml.size() && parsed.rawXml[i + 1] == '\n') ++i;
        }
        xml::XMLDocument document(false, xml::PEDANTIC_WHITESPACE);
        if (document.Parse(normalized.data(), normalized.size()) != xml::XML_SUCCESS)
            throw std::runtime_error(std::string("invalid metadata XML: ") + document.ErrorStr());
        const auto* root = document.RootElement();
        if (!root || std::strcmp(root->Name(), "TeklaStructuresModels") != 0 || root->NextSiblingElement())
            throw std::runtime_error("XML is not a single TeklaStructuresModels document");
        bool seenRoot = false, seenDoctype = false, seenDeclaration = false;
        for (const auto* node = document.FirstChild(); node; node = node->NextSibling())
        {
            if (node == root) seenRoot = true;
            else if (node->ToUnknown())
            {
                if (seenRoot || seenDoctype || trimXml(node->Value()) != "DOCTYPE TeklaStructuresModels")
                    throw std::runtime_error("unsupported metadata DOCTYPE or declaration");
                seenDoctype = true;
            }
            else if (node->ToDeclaration())
            {
                if (seenRoot || seenDoctype || seenDeclaration || node != document.FirstChild())
                    throw std::runtime_error("misplaced metadata XML declaration");
                declaration(node->Value());
                seenDeclaration = true;
            }
            else if (node->ToText() && trimXml(node->Value()).empty()) {}
            else if (!node->ToComment())
                throw std::runtime_error("unexpected content outside metadata root");
        }
        validateNodes(*root);
        containerText(*root);
        const auto* model = root->FirstChildElement("Model");
        if (!model || model->NextSiblingElement("Model"))
            throw std::runtime_error("metadata must contain exactly one direct Model element");
        containerText(*model);
        parsed.name = field(*model, "Name");
        parsed.designer = field(*model, "Designer");
        parsed.description = field(*model, "Description");
        parsed.version = field(*model, "Version");
        parsed.productVersion = field(*model, "ProductVersion");
        parsed.language = field(*model, "Language");
        parsed.templateName = field(*model, "Template");
        parsed.environment = field(*model, "Environment");
        auto boolean = trimXml(field(*model, "IsTemplate"));
        std::transform(boolean.begin(), boolean.end(), boolean.begin(), [](char c) {
            return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
        });
        if (boolean == "TRUE" || boolean == "1") parsed.isTemplate = true;
        else if (!boolean.empty() && boolean != "FALSE" && boolean != "0")
            throw std::runtime_error("invalid metadata IsTemplate boolean");
        parsed.projectSearchPath = field(*model, "XS_PROJECT");
        parsed.firmSearchPath = field(*model, "XS_FIRM");
        parsed.systemSearchPath = field(*model, "XS_SYSTEM");
        parsed.connectedId = field(*model, "ConnectedId");
        result = std::move(parsed);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}
}
