#include <tekla/db1/Catalogs.hpp>
#include <zlib.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
std::string replace(std::string text,const std::string& from,const std::string& to)
{const auto at=text.find(from);check(at!=std::string::npos,"fixture replacement absent");text.replace(at,from.size(),to);return text;}
void save(const std::filesystem::path& path,const std::string& text)
{std::filesystem::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write(text.data(),text.size());check(bool(f),"write fixture");}
const std::string definition="<ImportPart version='1.0'><Info><Name>test</Name><Guid>guid</Guid><BrepStorageId>geom</BrepStorageId><Extrema><MinPoint X='0' Y='0' Z='0'/><MaxPoint X='1' Y='1' Z='0'/></Extrema><IsSolid> False </IsSolid></Info></ImportPart>";
const std::string geometry="<Polymesh><Points><Point><X>0</X><Y>0</Y><Z>0</Z></Point><Point><X>1</X><Y>0</Y><Z>0</Z></Point><Point><X>0</X><Y>1</Y><Z>0</Z></Point></Points><Faces><Face><OuterLoop><Index>0</Index><Index>1</Index><Index>2</Index></OuterLoop><InnerLoops><Loop><Index>0</Index><Index>2</Index><Index>1</Index></Loop></InnerLoops></Face></Faces><Edges><Edge><FirstVertexIndex>0</FirstVertexIndex><SecondVertexIndex>1</SecondVertexIndex><EdgeType>FutureEdge</EdgeType></Edge></Edges></Polymesh>";
}
int main(int argc,char** argv)
{
    if(argc!=3)return 2;
    try
    {
        const std::string mode=argv[2];const auto folder=std::filesystem::u8path(argv[1])/mode;
        std::filesystem::create_directories(folder);const auto file=folder/"geom.xml";
        tekla::db1::ShapeDefinition d;tekla::db1::ShapeGeometry g;std::string error;
        const auto def=[&](const std::string& text,bool valid){save(file,text);error="stale";check(tekla::db1::parseShapeDefinition(file,d,error)==valid,"definition verdict: "+error);if(valid)check(error.empty() && d.rawXml==text,"definition raw/error");else check(!error.empty() && d.guid.empty() && d.rawXml.empty(),"definition failure retained output");};
        const auto geo=[&](const std::string& text,bool valid){save(file,text);error="stale";check(tekla::db1::parseShapeGeometry(file,g,error)==valid,"geometry verdict: "+error);if(valid)check(error.empty() && g.rawXml==text,"geometry raw/error");else check(!error.empty() && g.points.empty() && g.rawXml.empty(),"geometry failure retained output");};
        if(mode=="shapes_scope")
        {
            def("<!--<Name>wrong</Name>-->"+replace(definition,"<Info>","<Info><Extension><Name>nested</Name></Extension>"),true);
            check(d.name=="test" && !d.hasGeometryType && d.geometryType==0 && d.isSolid && !*d.isSolid,"optional fields/scope");
            def(replace(definition,"<Info>","<Info><BrepGeomType>0</BrepGeomType>"),true);check(d.hasGeometryType,"explicit zero type lost");
            def(replace(definition,"<Name>test</Name>","<Name><![CDATA[A&B]]><!--skip-->&amp;lt;&#x4e2d;</Name>"),true);
            check(d.name=="A&B&lt;\xe4\xb8\xad","entity decoded twice or CDATA changed");
        }
        else if(mode=="shapes_geometry")
        {
            geo("<!--<Point><X>999</X></Point>-->"+geometry,true);
            check(g.points.size()==3 && g.faces.size()==1 && g.faces[0].innerLoops.size()==1 && g.edges.size()==1,"geometry counts");
            check(g.edges[0].type==tekla::db1::ShapeEdgeType::Unknown && g.edges[0].rawType=="FutureEdge","unknown edge type lost");
            geo(replace(geometry,"<X>0</X>","<X> +0.0e0 </X>"),true);
            geo("<Polymesh><Points/><Faces/><Edges/></Polymesh>",true);check(g.points.empty() && g.faces.empty(),"empty geometry fabricated");
            geo(replace(geometry,"<Index>2</Index>","<Index>3</Index>"),false);
            geo(replace(geometry,"<Loop><Index>0</Index><Index>2</Index><Index>1</Index></Loop>","<Loop><Index>0</Index></Loop>"),false);
            geo(replace(geometry,"<SecondVertexIndex>1</SecondVertexIndex>","<SecondVertexIndex>99</SecondVertexIndex>"),false);
        }
        else if(mode=="shapes_numbers")
        {
            for(const auto* bad:{"nan","INF","-Infinity","1e999","1.2oops","0x1"})
            {geo(replace(geometry,"<X>0</X>",std::string("<X>")+bad+"</X>"),false);def(replace(definition,"X='0'",std::string("X='")+bad+"'"),false);}
            for(const auto* bad:{"nan","-1","4294967296","1.5","1e0","0x1",""})geo(replace(geometry,"<Index>0</Index>",std::string("<Index>")+bad+"</Index>"),false);
            def(replace(definition,"X='0'","X='2'"),false);
            def(replace(definition,"False","maybe"),false);
            def(replace(definition,"<Info>","<Info><BrepGeomType>nan</BrepGeomType>"),false);
        }
        else if(mode=="shapes_structure")
        {
            def("<Other>"+definition+"</Other>",false);
            def(definition+definition,false);
            def(replace(definition,"<Guid>guid</Guid>","<Extension><Guid>guid</Guid></Extension>"),false);
            def(replace(definition,"<Guid>guid</Guid>","<Guid>guid</Guid><Guid>other</Guid>"),false);
            def(replace(definition,"</Info>","</Info><Info/>"),false);
            def(replace(definition,"</Name>",""),false);
            def(replace(definition,"<Name>test</Name>","<Name><Value>test</Value></Name>"),false);
            def(replace(definition,"<Info>","<Info><BrepGeomType>1</BrepGeomType><BrepGeomType>2</BrepGeomType>"),false);
            geo(replace(geometry,"</Points>","</Points><Points/>"),false);
            geo(replace(geometry,"<X>0</X>","<X>0</X><X>1</X>"),false);
            geo("<Other><!--<Polymesh/>--></Other>",false);
            geo(replace(geometry,"<Polymesh>","<Polymesh xmlns='urn:other'>"),false);
            for(auto n:{std::size_t(0),geometry.size()/2,geometry.size()-1})geo(geometry.substr(0,n),false);
        }
        else if(mode=="shapes_entities")
        {
            def(replace(definition,"test","&amp;lt;"),true);check(d.name=="&lt;","double entity decoding");
            for(const auto* bad:{"&unknown;","&#0;","&#xD800;","&amp"})def(replace(definition,"test",bad),false);
            def("<!DOCTYPE ImportPart SYSTEM 'file:///not-opened'>"+definition,false);
            geo("<!DOCTYPE Polymesh [<!ENTITY x '1'>]>"+geometry,false);
            def(replace(definition,"test",std::string(1,char(0xff))),false);
            def(replace(definition,"test",std::string("x\0y",3)),false);
            def("<?xml version='1.0' encoding='UTF-16'?>"+definition,false);
        }
        else if(mode=="shapes_limits")
        {
            save(file,definition);tekla::db1::ShapeReadOptions opt;opt.maxDecodedBytes=definition.size()-1;
            check(!tekla::db1::parseShapeDefinition(file,d,error,opt) && d.guid.empty(),"definition limit ignored");
            save(file,geometry);opt.maxDecodedBytes=geometry.size()-1;
            check(!tekla::db1::parseShapeGeometry(file,g,error,opt) && g.points.empty(),"geometry limit ignored");
        }
        else if(mode=="shapes_gzip")
        {
            const auto path=folder/"geom.tez";gzFile f=gzopen(path.string().c_str(),"wb");check(f!=nullptr,"gzip fixture open");
            check(gzwrite(f,geometry.data(),unsigned(geometry.size()))==int(geometry.size()),"gzip fixture write");check(gzclose(f)==Z_OK,"gzip fixture close");
            check(tekla::db1::parseShapeGeometry(path,g,error),error);check(g.rawXml==geometry && g.storageId=="geom","gzip bytes/storage");
            tekla::db1::ShapeReadOptions opt;opt.maxDecodedBytes=geometry.size()-1;check(!tekla::db1::parseShapeGeometry(path,g,error,opt),"gzip limit ignored");
        }
        else if(mode=="shapes_catalog")
        {
            const auto root=folder/"valid";save(root/"Shapes/def.xml",definition);save(root/"ShapeGeometries/geom.xml",geometry);save(root/"unrelated.xml","<Other><!-- <ImportPart/> --></Other>");
            tekla::db1::ShapeCatalog catalog;check(tekla::db1::parseShapeCatalog(root,catalog,error),error);
            check(catalog.definitionsByGuid.size()==1 && catalog.geometriesByStorageId.size()==1 && catalog.diagnostics.empty(),"plain XML geometry not linked");
            const auto dup=folder/"duplicates";save(dup/"a.xml",definition);save(dup/"b.xml",definition);save(dup/"c.xml",definition);
            save(dup/"one/geom.xml",geometry);save(dup/"two/geom.tez",geometry);
            check(tekla::db1::parseShapeCatalog(dup,catalog,error),error);
            check(catalog.definitionsByGuid.empty() && catalog.geometriesByStorageId.empty() && catalog.diagnostics.size()==3,"ambiguous keys selected silently");
        }
        else throw std::runtime_error("unknown shape test");
        std::cout<<"PASS "<<mode<<'\n';return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
