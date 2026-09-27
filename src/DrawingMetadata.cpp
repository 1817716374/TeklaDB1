#include <tekla/DrawingMetadata.hpp>
#include "BinaryIO.hpp"
#include "XmlPrivate.hpp"
#include <charconv>
#include <cmath>
#include <locale>
#include <sstream>
#include <set>
#include <limits>

namespace tekla
{
namespace
{
using namespace db1::detail::xmlsupport;
std::string guid(const xml::XMLElement& root, const char* name)
{
    auto value=trimXml(field(root,name));
    if(value.empty())return value;
    if(value.size()!=36)throw std::runtime_error(std::string("invalid metadata GUID: ")+name);
    for(std::size_t i=0;i<value.size();++i)
    {
        auto& c=value[i];
        if(i==8 || i==13 || i==18 || i==23)
        {if(c!='-')throw std::runtime_error(std::string("invalid metadata GUID: ")+name);}
        else
        {
            if(c>='A' && c<='F')c=static_cast<char>(c-'A'+'a');
            if(!((c>='0' && c<='9') || (c>='a' && c<='f')))
                throw std::runtime_error(std::string("invalid metadata GUID: ")+name);
        }
    }
    return value;
}
std::int64_t integer(const xml::XMLElement& root,const char* name)
{
    const auto value=trimXml(field(root,name));std::int64_t number=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
    if(value.empty() || parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size())
        throw std::runtime_error(std::string("invalid metadata integer: ")+name);
    return number;
}
std::optional<double> dimension(const xml::XMLElement& root,const char* name)
{
    if(!root.FirstChildElement(name))return {};
    const auto text=trimXml(field(root,name));std::istringstream stream(text);stream.imbue(std::locale::classic());
    double value=0;stream>>std::noskipws>>value;
    if(!stream || !stream.eof() || !std::isfinite(value) || value<=0)
        throw std::runtime_error(std::string("invalid metadata dimension: ")+name);
    return value;
}
}
bool parseDrawingVersionMetadata(const std::filesystem::path& path,
    DrawingVersionMetadata& result,std::string& error,std::size_t maxDecodedBytes)
{
    result={};error.clear();
    try
    {
        const auto bytes=db1::detail::readPayload(path,maxDecodedBytes);
        DrawingVersionMetadata parsed;parsed.rawXml.assign(bytes.begin(),bytes.end());validateUtf8(parsed.rawXml);
        std::string normalized;normalized.reserve(parsed.rawXml.size());
        for(std::size_t i=0;i<parsed.rawXml.size();++i)
        {
            const char c=parsed.rawXml[i];normalized+=c=='\r'?'\n':c;
            if(c=='\r' && i+1<parsed.rawXml.size() && parsed.rawXml[i+1]=='\n')++i;
        }
        xml::XMLDocument document(false,xml::PEDANTIC_WHITESPACE);
        if(document.Parse(normalized.data(),normalized.size())!=xml::XML_SUCCESS)
            throw std::runtime_error(std::string("invalid drawing metadata XML: ")+document.ErrorStr());
        const auto* root=document.RootElement();
        if(!root || std::strcmp(root->Name(),"DrawingVersionMetadata")!=0 || root->NextSiblingElement())
            throw std::runtime_error("XML is not a single DrawingVersionMetadata document");
        for(const auto* node=document.FirstChild();node;node=node->NextSibling())
        {
            if(node==root || node->ToComment())continue;
            if(node->ToDeclaration() && node==document.FirstChild())declaration(node->Value());
            else if(node->ToText() && trimXml(node->Value()).empty())continue;
            else throw std::runtime_error("unsupported drawing metadata declaration or root content");
        }
        validateNodes(*root);containerText(*root);
        if(root->Attribute("xmlns"))throw std::runtime_error("unsupported drawing metadata namespace");
        if(root->FirstAttribute())parsed.diagnostics.emplace_back("drawing metadata root attributes retained in raw XML");
        const std::set<std::string> names={"DrawingGuid","MainObjectGuid","Author","Mark","Name","Title1","Title2","Title3","Width","Height","DrawingType",
            "IssueDate","NextDate","IssueId","Flag","Revision","PlotDate","ModifyDate","CreateDate","PartsNo","UpToDate","Lock"};
        std::set<std::string> seen;
        for(const auto* element=root->FirstChildElement();element;element=element->NextSiblingElement())
        {
            const std::string name=element->Name();
            if(!names.count(name)){parsed.diagnostics.push_back("unknown drawing metadata field retained in raw XML: "+name);continue;}
            if(!seen.insert(name).second)throw std::runtime_error("duplicate drawing metadata field: "+name);
            if(element->FirstAttribute())throw std::runtime_error("unsupported attributes on drawing metadata field: "+name);
        }
        parsed.drawingGuid=guid(*root,"DrawingGuid");parsed.mainObjectGuid=guid(*root,"MainObjectGuid");
        parsed.author=trimXml(field(*root,"Author"));parsed.mark=trimXml(field(*root,"Mark"));parsed.name=trimXml(field(*root,"Name"));
        parsed.title1=trimXml(field(*root,"Title1"));parsed.title2=trimXml(field(*root,"Title2"));parsed.title3=trimXml(field(*root,"Title3"));
        parsed.width=dimension(*root,"Width");parsed.height=dimension(*root,"Height");
        if(root->FirstChildElement("DrawingType"))
        {
            const auto n=integer(*root,"DrawingType");
            if(n<0 || static_cast<std::uint64_t>(n)>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("drawing metadata type out of range");
            parsed.drawingType=static_cast<std::uint32_t>(n);
        }
        for(const char* name:{"IssueDate","NextDate","IssueId","Flag","Revision","PlotDate","ModifyDate","CreateDate","PartsNo","UpToDate","Lock"})
            if(root->FirstChildElement(name))parsed.storedIntegers.emplace(name,integer(*root,name));
        if(seen.size()!=names.size())parsed.diagnostics.emplace_back("drawing metadata fields are missing; no default values substituted");
        parsed.diagnostics.emplace_back("saved drawing version metadata; integer flags, dates and lifecycle precedence remain uninterpreted");
        result=std::move(parsed);return true;
    }
    catch(const std::exception& e){error=e.what();return false;}
}
}
