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
std::string windows1252ToUtf8(const std::string& input)
{
    static constexpr std::uint16_t extension[32]={
        0x20ac,0,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
        0x02c6,0x2030,0x0160,0x2039,0x0152,0,0x017d,0,
        0,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x02dc,0x2122,0x0161,0x203a,0x0153,0,0x017e,0x0178};
    std::string output;output.reserve(input.size()+16);
    for(const unsigned char byte:input)
    {
        std::uint32_t codepoint=byte;
        if(byte>=0x80 && byte<=0x9f)
        {
            codepoint=extension[byte-0x80];
            if(!codepoint)throw std::runtime_error("undefined byte in Windows-1252 drawing metadata");
        }
        if(codepoint<0x80)output.push_back(static_cast<char>(codepoint));
        else if(codepoint<0x800)
        {
            output.push_back(static_cast<char>(0xc0|(codepoint>>6)));
            output.push_back(static_cast<char>(0x80|(codepoint&0x3f)));
        }
        else
        {
            output.push_back(static_cast<char>(0xe0|(codepoint>>12)));
            output.push_back(static_cast<char>(0x80|((codepoint>>6)&0x3f)));
            output.push_back(static_cast<char>(0x80|(codepoint&0x3f)));
        }
    }
    return output;
}
bool containsMalformedUtf8Encoding(const std::string& input)
{
    const auto continuation=[](unsigned char byte) { return byte>=0x80 && byte<=0xbf; };
    for(std::size_t i=0;i+1<input.size();++i)
    {
        const auto first=static_cast<unsigned char>(input[i]);
        const auto second=static_cast<unsigned char>(input[i+1]);
        if(((first==0xc0 || first==0xc1) && continuation(second)) ||
           (first>=0xf5 && first<=0xf7 && i+3<input.size() && continuation(second) &&
             continuation(static_cast<unsigned char>(input[i+2])) && continuation(static_cast<unsigned char>(input[i+3]))))
            return true;
        if(i+2<input.size())
        {
            const auto third=static_cast<unsigned char>(input[i+2]);
            if(((first==0xe0 && second>=0x80 && second<=0x9f) ||
                (first==0xed && second>=0xa0 && second<=0xbf)) && continuation(third))
                return true;
            if(i+3<input.size())
            {
                const auto fourth=static_cast<unsigned char>(input[i+3]);
                if(((first==0xf0 && second>=0x80 && second<=0x8f) ||
                    (first==0xf4 && second>=0x90 && second<=0xbf)) && continuation(third) && continuation(fourth))
                    return true;
            }
        }
    }
    return false;
}
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
std::optional<std::string> scalarText(const xml::XMLElement& element)
{
    std::string value;
    for(const auto* node=element.FirstChild();node;node=node->NextSibling())
    {
        if(const auto* text=node->ToText())
            value+=text->CData()?text->Value():decode(text->Value());
        else if(node->ToComment())continue;
        else return {};
    }
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
        DrawingVersionMetadata parsed;parsed.rawXml.assign(bytes.begin(),bytes.end());
        std::string xmlText=parsed.rawXml;
        try { validateUtf8(xmlText); }
        catch(const std::exception&)
        {
            if(containsMalformedUtf8Encoding(xmlText))
                throw std::runtime_error("malformed UTF-8 sequence in drawing metadata");
            xmlText=windows1252ToUtf8(xmlText);
            validateUtf8(xmlText);
            parsed.diagnostics.emplace_back("Windows-1252 drawing metadata text decoded as UTF-8; original bytes retained in rawXml");
        }
        std::string normalized;normalized.reserve(xmlText.size());
        for(std::size_t i=0;i<xmlText.size();++i)
        {
            const char c=xmlText[i];normalized+=c=='\r'?'\n':c;
            if(c=='\r' && i+1<xmlText.size() && xmlText[i+1]=='\n')++i;
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
            "IssueDate","NextDate","IssueId","Flag","Revision","PlotDate","ModifyDate","CreateDate","PartsNo","UpToDate","Lock","AlwaysNecessary"};
        std::set<std::string> seen;
        for(const auto* element=root->FirstChildElement();element;element=element->NextSiblingElement())
        {
            const std::string name=element->Name();
            if(!names.count(name))
            {
                parsed.diagnostics.push_back("unknown drawing metadata field retained in raw XML: "+name);
                parsed.extensions.push_back({name,scalarText(*element),element->FirstAttribute()!=nullptr});
                continue;
            }
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
        for(const char* name:{"IssueDate","NextDate","IssueId","Flag","Revision","PlotDate","ModifyDate","CreateDate","PartsNo","UpToDate","Lock","AlwaysNecessary"})
            if(root->FirstChildElement(name))parsed.storedIntegers.emplace(name,integer(*root,name));
        if(seen.size()!=names.size())parsed.diagnostics.emplace_back("drawing metadata fields are missing; no default values substituted");
        parsed.diagnostics.emplace_back("saved drawing version metadata; integer flags, dates and lifecycle precedence remain uninterpreted");
        result=std::move(parsed);return true;
    }
    catch(const std::exception& e){error=e.what();return false;}
}
}
