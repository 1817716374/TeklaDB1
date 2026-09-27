#include <tekla/db1/Catalogs.hpp>
#include "BinaryIO.hpp"
#include "XmlPrivate.hpp"
#include "ShapeCatalogPrivate.hpp"
#include <charconv>
#include <cmath>
#include <cctype>
#include <set>

namespace tekla::db1
{
namespace
{
namespace xu = detail::xmlsupport;
namespace xml = tekla_db1_tinyxml2;

struct Document
{
    std::string raw;
    xml::XMLDocument dom{false, xml::PEDANTIC_WHITESPACE};
    const xml::XMLElement* root = nullptr;
    Document(const std::filesystem::path& path, const ShapeReadOptions& options)
    {
        const auto bytes=detail::readPayload(path,options.maxDecodedBytes);
        raw.assign(bytes.begin(),bytes.end()); xu::validateUtf8(raw);
        std::string normalized; normalized.reserve(raw.size());
        for(std::size_t i=0;i<raw.size();++i)
        {
            normalized+=raw[i]=='\r'?'\n':raw[i];
            if(raw[i]=='\r' && i+1<raw.size() && raw[i+1]=='\n')++i;
        }
        if(dom.Parse(normalized.data(),normalized.size())!=xml::XML_SUCCESS)
            throw std::runtime_error(std::string("invalid shape XML: ")+dom.ErrorStr());
        root=dom.RootElement();
        if(!root || root->NextSiblingElement())throw std::runtime_error("shape XML requires one root");
        for(const auto* n=dom.FirstChild();n;n=n->NextSibling())
        {
            if(n==root || n->ToComment())continue;
            if(n->ToDeclaration() && n==dom.FirstChild())xu::declaration(n->Value());
            else if(n->ToText() && xu::trimXml(n->Value()).empty())continue;
            else throw std::runtime_error("unsupported shape XML declaration or content outside root");
        }
        xu::validateNodes(*root);
    }
};
const xml::XMLElement& child(const xml::XMLElement& parent,const char* name)
{
    const auto* e=parent.FirstChildElement(name);
    if(!e || e->NextSiblingElement(name))throw std::runtime_error(std::string("missing or duplicate shape element: ")+name);
    xu::containerText(parent);return *e;
}
std::string scalar(const xml::XMLElement& element)
{
    std::string value;
    for(const auto* n=element.FirstChild();n;n=n->NextSibling())
        if(const auto* t=n->ToText())value+=t->CData()?t->Value():xu::decode(t->Value());
        else if(!n->ToComment())throw std::runtime_error("non-scalar shape field");
    return value;
}
std::string value(const xml::XMLElement& parent,const char* name)
{ return scalar(child(parent,name)); }
double number(std::string text)
{
    text=xu::trimXml(std::move(text));
    if(!text.empty() && text.front()=='+')text.erase(0,1);
    double result=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result,std::chars_format::general);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || !std::isfinite(result))
        throw std::runtime_error("invalid finite shape number");
    return result;
}
std::uint32_t index(std::string text)
{
    text=xu::trimXml(std::move(text));
    if(!text.empty() && text.front()=='+')text.erase(0,1);
    std::uint32_t result=0;
    const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw std::runtime_error("invalid shape integer");
    return result;
}
void requireRoot(const Document& d,const char* name)
{
    if(std::strcmp(d.root->Name(),name)!=0)throw std::runtime_error(std::string("shape XML root must be ")+name);
    const auto* ns=d.root->Attribute("xmlns");
    if(ns && *ns)throw std::runtime_error("unsupported shape XML namespace");
    const auto* version=d.root->Attribute("version");
    if(version && std::strcmp(version,"1.0")!=0)throw std::runtime_error("unsupported shape XML version");
}
ShapeDefinition definition(Document& d,const std::filesystem::path& path)
{
    requireRoot(d,"ImportPart");const auto& info=child(*d.root,"Info");
    ShapeDefinition out;
    out.name=xu::field(info,"Name");out.guid=value(info,"Guid");out.brepStorageId=value(info,"BrepStorageId");
    if(xu::trimXml(out.guid).empty() || xu::trimXml(out.brepStorageId).empty())throw std::runtime_error("empty shape identity/reference");
    out.fingerprint=xu::field(info,"Fingerprint");
    if(info.FirstChildElement("BrepGeomType")) { out.geometryType=index(value(info,"BrepGeomType"));out.hasGeometryType=true; }
    if(info.FirstChildElement("IsSolid"))
    {
        auto solid=xu::trimXml(value(info,"IsSolid"));
        std::transform(solid.begin(),solid.end(),solid.begin(),[](char c){return c>='a' && c<='z'?char(c-'a'+'A'):c;});
        if(solid=="TRUE" || solid=="1")out.isSolid=true;
        else if(solid=="FALSE" || solid=="0")out.isSolid=false;
        else throw std::runtime_error("invalid shape IsSolid boolean");
    }
    const auto& extrema=child(info,"Extrema");
    const auto& minimum=child(extrema,"MinPoint");const auto& maximum=child(extrema,"MaxPoint");
    for(std::size_t i=0;i<3;++i)
    {
        const std::string axis(1,"XYZ"[i]);const auto* lo=minimum.Attribute(axis.c_str());const auto* hi=maximum.Attribute(axis.c_str());
        if(!lo || !hi)throw std::runtime_error("missing shape bound coordinate");
        out.minimum[i]=number(xu::decode(lo));out.maximum[i]=number(xu::decode(hi));
        if(out.minimum[i]>out.maximum[i])throw std::runtime_error("inverted shape bounds");
    }
    out.sourcePath=path;out.rawXml=std::move(d.raw);return out;
}
std::vector<std::uint32_t> loop(const xml::XMLElement& element,std::size_t points)
{
    xu::containerText(element);std::vector<std::uint32_t> out;
    for(const auto* e=element.FirstChildElement("Index");e;e=e->NextSiblingElement("Index"))
    {
        const auto v=index(scalar(*e));if(v>=points)throw std::runtime_error("shape index outside point array");out.push_back(v);
    }
    if(out.size()<3)throw std::runtime_error("shape loop requires at least three vertices");
    return out;
}
ShapeGeometry geometry(Document& d,const std::filesystem::path& path)
{
    requireRoot(d,"Polymesh");ShapeGeometry out;
    const auto& points=child(*d.root,"Points");xu::containerText(points);
    for(const auto* p=points.FirstChildElement("Point");p;p=p->NextSiblingElement("Point"))
        out.points.push_back({number(value(*p,"X")),number(value(*p,"Y")),number(value(*p,"Z"))});
    const auto& faces=child(*d.root,"Faces");xu::containerText(faces);
    for(const auto* f=faces.FirstChildElement("Face");f;f=f->NextSiblingElement("Face"))
    {
        ShapeFace face;face.outerLoop=loop(child(*f,"OuterLoop"),out.points.size());
        if(f->FirstChildElement("InnerLoops"))
        {
            const auto& inner=child(*f,"InnerLoops");xu::containerText(inner);
            for(const auto* l=inner.FirstChildElement("Loop");l;l=l->NextSiblingElement("Loop"))face.innerLoops.push_back(loop(*l,out.points.size()));
        }
        out.faces.push_back(std::move(face));
    }
    if(d.root->FirstChildElement("Edges"))
    {
        const auto& edges=child(*d.root,"Edges");xu::containerText(edges);
        for(const auto* e=edges.FirstChildElement("Edge");e;e=e->NextSiblingElement("Edge"))
        {
            ShapeEdge edge;edge.firstVertex=index(value(*e,"FirstVertexIndex"));edge.secondVertex=index(value(*e,"SecondVertexIndex"));edge.rawType=value(*e,"EdgeType");
            if(edge.firstVertex>=out.points.size() || edge.secondVertex>=out.points.size())throw std::runtime_error("shape edge outside point array");
            if(edge.rawType=="VisibleEdge")edge.type=ShapeEdgeType::Visible;
            else if(edge.rawType=="InvisibleEdge")edge.type=ShapeEdgeType::Invisible;
            out.edges.push_back(std::move(edge));
        }
    }
    out.storageId=detail::pathUtf8(path.stem());out.sourcePath=path;out.rawXml=std::move(d.raw);return out;
}
}
bool parseShapeDefinition(const std::filesystem::path& path,ShapeDefinition& result,std::string& error,const ShapeReadOptions& options)
{
    result={};error.clear();try { Document d(path,options);result=definition(d,path);return true; }
    catch(const std::exception& e){error=e.what();return false;}
}
bool parseShapeDefinition(const std::filesystem::path& path,ShapeDefinition& result,std::string& error)
{return parseShapeDefinition(path,result,error,ShapeReadOptions{});}
bool parseShapeGeometry(const std::filesystem::path& path,ShapeGeometry& result,std::string& error,const ShapeReadOptions& options)
{
    result={};error.clear();try { Document d(path,options);result=geometry(d,path);return true; }
    catch(const std::exception& e){error=e.what();return false;}
}
bool parseShapeGeometry(const std::filesystem::path& path,ShapeGeometry& result,std::string& error)
{return parseShapeGeometry(path,result,error,ShapeReadOptions{});}
bool detail::readShapeDirectories(const std::vector<std::filesystem::path>& directories,ShapeDirectoryScan& scan,std::string& error)
{
    scan={};error.clear();auto& result=scan.catalog;
    try
    {
        std::vector<std::filesystem::path> paths;
        for(const auto& directory:directories)
        {
            if(!std::filesystem::is_directory(directory))throw std::runtime_error("shape catalog input is not a directory");
            for(const auto& item:std::filesystem::recursive_directory_iterator(directory))if(item.is_regular_file())paths.push_back(item.path());
        }
        std::sort(paths.begin(),paths.end());paths.erase(std::unique(paths.begin(),paths.end()),paths.end());
        auto& ambiguousDefinitions=scan.ambiguousDefinitions;auto& ambiguousGeometries=scan.ambiguousGeometries;
        for(const auto& path:paths)
        {
            auto ext=detail::pathUtf8(path.extension());
            std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
            if(ext!=".xml" && ext!=".tez")continue;
            try
            {
                Document d(path,ShapeReadOptions{});
                if(std::strcmp(d.root->Name(),"Polymesh")==0 || ext==".tez")
                {
                    auto item=geometry(d,path);const auto key=item.storageId;
                    if(ambiguousGeometries.count(key) || !result.geometriesByStorageId.emplace(key,std::move(item)).second)
                    {ambiguousGeometries.insert(key);result.geometriesByStorageId.erase(key);throw std::runtime_error("ambiguous shape geometry storage ID: "+key);}
                }
                else if(std::strcmp(d.root->Name(),"ImportPart")==0)
                {
                    auto item=definition(d,path);const auto key=item.guid;
                    if(ambiguousDefinitions.count(key) || !result.definitionsByGuid.emplace(key,std::move(item)).second)
                    {ambiguousDefinitions.insert(key);result.definitionsByGuid.erase(key);throw std::runtime_error("ambiguous shape definition GUID: "+key);}
                }
            }
            catch(const std::exception& e){result.diagnostics.push_back(detail::pathUtf8(path)+": "+e.what());}
        }
        return true;
    }
    catch(const std::exception& e){scan={};error=e.what();return false;}
}
bool parseShapeCatalog(const std::filesystem::path& directory,ShapeCatalog& result,std::string& error)
{
    result={};detail::ShapeDirectoryScan scan;
    if(!detail::readShapeDirectories({directory},scan,error))return false;
    result=std::move(scan.catalog);
    for(const auto& entry:result.definitionsByGuid)
        if(!result.geometriesByStorageId.count(entry.second.brepStorageId))result.diagnostics.push_back("shape "+entry.second.name+" references missing geometry "+entry.second.brepStorageId);
    return true;
}
}
