#include <tekla/db1/Catalogs.hpp>
#include "BinaryIO.hpp"
#include "XmlPrivate.hpp"

namespace tekla::db1
{
namespace { using namespace detail::xmlsupport; }


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
