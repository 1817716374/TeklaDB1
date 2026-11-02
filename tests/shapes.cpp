#include <tekla/db1/Catalogs.hpp>
#include <zlib.h>
#include <fstream>
#include <iostream>
#include <iterator>
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
            const auto readCompressed=[&]() {
                std::ifstream in(path,std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
            };
            const auto reject=[&](const std::string& bytes,const char* message) {
                save(path,bytes);g.points.push_back({9,9,9});error="stale";
                check(!tekla::db1::parseShapeGeometry(path,g,error),message);
                check(!error.empty() && g.points.empty() && g.rawXml.empty(),"invalid gzip retained partial shape output");
            };
            const auto valid=readCompressed();check(valid.size()>8,"gzip fixture unexpectedly short");
            auto badCrc=valid;badCrc[badCrc.size()-8]^=1;reject(badCrc,"corrupt gzip CRC accepted");
            auto truncated=valid;truncated.pop_back();reject(truncated,"truncated gzip stream accepted");
            reject(valid+"garbage","trailing gzip garbage accepted");
            save(path,valid);error.clear();
            check(tekla::db1::parseShapeGeometry(path,g,error),error);check(g.rawXml==geometry,"valid gzip changed after rejected inputs");
        }
        else if(mode=="shapes_catalog")
        {
            const auto root=folder/"valid-guid-match";save(root/"Shapes/GUID.xml",definition);save(root/"ShapeGeometries/geom.xml",geometry);save(root/"unrelated.xml","<Other><!-- <ImportPart/> --></Other>");
            tekla::db1::ShapeCatalog catalog;check(tekla::db1::parseShapeCatalog(root,catalog,error),error);
            check(catalog.definitionsByGuid.size()==1 && catalog.geometriesByStorageId.size()==1 && catalog.diagnostics.empty(),
                "plain XML geometry not linked defs="+std::to_string(catalog.definitionsByGuid.size())+
                " geos="+std::to_string(catalog.geometriesByStorageId.size())+
                " diagnostics="+(catalog.diagnostics.empty()?std::string{}:catalog.diagnostics.front()));
            const auto mismatch=folder/"filename-mismatch";save(mismatch/"Shapes/wrong-name.xml",definition);
            check(tekla::db1::parseShapeCatalog(mismatch,catalog,error),error);
            check(catalog.definitionsByGuid.empty() && catalog.diagnostics.size()==1 && catalog.diagnostics.front().find("does not match its filename")!=std::string::npos,
                "shape definition filename identity conflict was not reported");
            const auto dup=folder/"duplicates-guid-match";save(dup/"a/guid.xml",definition);save(dup/"b/guid.xml",definition);save(dup/"c/guid.xml",definition);
            save(dup/"one/geom.xml",geometry);save(dup/"two/geom.tez",geometry);
            check(tekla::db1::parseShapeCatalog(dup,catalog,error),error);
            check(catalog.definitionsByGuid.empty() && catalog.geometriesByStorageId.empty() && catalog.diagnostics.size()==3,"ambiguous keys selected silently");
        }
        else if(mode=="rebar_catalog")
        {
            const auto path=folder/"RebarShapeCatalog.rscx";
            const std::string xml="<?xml version='1.0'?><!--saved--><ShapeCatalog><Category Name='Straight'><Shape>1</Shape></Category><Category Name='One &amp; two'><Shape>2_1</Shape><Shape>2_2</Shape></Category><Shape>5_1</Shape><Shape>36_3</Shape></ShapeCatalog>";
            save(path,xml);tekla::db1::RebarShapeCatalog catalog;error="stale";
            check(tekla::db1::parseRebarShapeCatalog(path,catalog,error),error);
            check(error.empty() && catalog.categories.size()==2 && catalog.categories[0].name=="Straight" &&
                  catalog.categories[0].shapeIds==std::vector<std::string>{"1"} && catalog.categories[1].name=="One & two" &&
                  catalog.categories[1].shapeIds==std::vector<std::string>({"2_1","2_2"}) &&
                  catalog.uncategorizedShapeIds==std::vector<std::string>({"5_1","36_3"}) && catalog.rawXml==xml &&
                  catalog.sourcePath==path,"rebar catalog order, entities, root entries, or raw retention");
            for(const auto& invalid:std::vector<std::string>{
                    "<Other/>","<ShapeCatalog><Category><Shape>1</Shape></Category></ShapeCatalog>",
                    "<ShapeCatalog><Category Name='  '/></ShapeCatalog>","<ShapeCatalog><Shape> </Shape></ShapeCatalog>",
                    "<ShapeCatalog><Category Name='x'><Other>1</Other></Category></ShapeCatalog>",
                    "<ShapeCatalog><Unknown/></ShapeCatalog>","<ShapeCatalog><Shape><Id>1</Id></Shape></ShapeCatalog>"})
            {
                save(path,invalid);catalog.categories.push_back({"stale",{"stale"}});error.clear();
                check(!tekla::db1::parseRebarShapeCatalog(path,catalog,error) && !error.empty() && catalog.categories.empty() &&
                      catalog.uncategorizedShapeIds.empty() && catalog.rawXml.empty(),"invalid rebar catalog accepted or retained output");
            }
        }
        else if(mode=="rebar_schedule_config")
        {
            const auto path=folder/"rebar_schedule_config.inp";
            // Syntax examples published in Trimble's Tekla Structures documentation.
            const std::string source=
                "# example mappings\r\n"
                "BEND_TYPE_5_1[1]=\"E\"\r\n"
                "BEND_TYPE_5_1[2]=\"DIM_A=D1\"\r\n"
                "BEND_TYPE_6_2[3]=\"DIM_B=D2*cos(A2*PI/180)\"\r\n"
                "BEND_TYPE_4[1]=if (D1==D3) then (\"A\") else (\"B\") endif\r\n"
                "UNKNOWN_OPTION=preserve me\r\n";
            save(path,source);tekla::db1::RebarScheduleConfig config;error="stale";
            check(tekla::db1::parseRebarScheduleConfig(path,config,error),error);
            check(error.empty()&&config.entries.size()==4&&config.entries[0].internalBendingType=="5_1"&&
                  config.entries[0].index==1&&config.entries[0].rawValue=="\"E\""&&
                  config.entries[2].rawValue=="\"DIM_B=D2*cos(A2*PI/180)\""&&
                  config.entries[3].rawValue=="if (D1==D3) then (\"A\") else (\"B\") endif"&&
                  config.entries[3].lineNumber==5&&config.unparsedLines.size()==1&&
                  config.unparsedLines[0].second=="UNKNOWN_OPTION=preserve me\r"&&config.rawText==source&&config.sourcePath==path,
                  "schedule mapping order, expression text, line numbers, or raw retention: entries="+
                  std::to_string(config.entries.size())+" type="+(config.entries.empty()?"":config.entries[0].internalBendingType)+
                  " value="+(config.entries.empty()?"":config.entries[0].rawValue)+" line="+
                  std::to_string(config.entries.size()<4?0:config.entries[3].lineNumber)+" unknown="+
                  std::to_string(config.unparsedLines.size()));
            const std::string bom=std::string("\xef\xbb\xbf",3)+"BEND_TYPE_1[1]=\"A\"\n";
            save(path,bom);check(tekla::db1::parseRebarScheduleConfig(path,config,error)&&
                config.entries.size()==1&&config.entries[0].rawValue=="\"A\""&&config.rawText==bom,
                "UTF-8 BOM blocked schedule entry or raw retention");
            for(const auto* invalid:{"BEND_TYPE_5_1[0]=x\n","BEND_TYPE_5_1[x]=x\n","BEND_TYPE_5_1[1] x\n",
                                     "BEND_TYPE_5_1[1]=x\nBEND_TYPE_5_1[1]=y\n","BEND_TYPE_5_1[4294967296]=x\n",
                                     "BEND_TYPE_5_1[1]=\n"})
            {
                save(path,invalid);config.entries.push_back({"stale",1,"stale",1});error.clear();
                check(!tekla::db1::parseRebarScheduleConfig(path,config,error)&&!error.empty()&&
                      config.entries.empty()&&config.rawText.empty(),"malformed schedule mapping accepted or retained output");
            }
        }
        else throw std::runtime_error("unknown shape test");
        std::cout<<"PASS "<<mode<<'\n';return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
