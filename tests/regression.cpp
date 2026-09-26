#include <tekla/Project.hpp>
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <functional>
#include <limits>

namespace
{
using Bytes = std::vector<std::uint8_t>;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> void put(Bytes& bytes, std::size_t offset, T value)
{
    check(offset + sizeof(value) <= bytes.size(), "fixture write overflow");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
void u32(Bytes& bytes, std::uint32_t value) { auto offset = bytes.size(); bytes.resize(offset + 4); put(bytes, offset, value); }
void str(Bytes& bytes, std::size_t offset, const std::string& value)
{
    check(offset + value.size() < bytes.size(), "fixture string overflow");
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}
void save(const std::filesystem::path& path, const Bytes& bytes)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(bool(file), "fixture file write failed");
}
Bytes gzip(const Bytes& input)
{
    z_stream stream{};
    check(deflateInit2(&stream, Z_BEST_SPEED, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) == Z_OK, "gzip init");
    Bytes result(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_in = const_cast<Bytef*>(input.data()); stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = result.data(); stream.avail_out = static_cast<uInt>(result.size());
    const auto status = deflate(&stream, Z_FINISH); result.resize(stream.total_out); deflateEnd(&stream);
    check(status == Z_STREAM_END, "gzip finish"); return result;
}
struct Table { std::uint32_t payload = 4; std::vector<std::uint32_t> fields{1}; std::vector<Bytes> rows; };
std::vector<Table> schema()
{
    std::vector<Table> tables(356);
    const auto define = [&](std::size_t ordinal, std::uint32_t payload, std::size_t count, std::initializer_list<std::size_t> refs) {
        auto& t = tables[ordinal]; t.payload = payload; t.fields.assign(count, 0); for (auto r : refs) t.fields[r] = 1;
    };
    define(61,32,6,{0,1}); define(64,40,7,{0,1,2}); define(65,52,8,{0,7}); define(75,45,6,{0,1,2});
    define(122,44,6,{0,1,5}); define(154,104,22,{0,1,4,5,6,21});
    for (auto t : {160,161}) define(t,24,7,{0,1,2,3,4});
    for (auto t : {190,310}) define(t,24,7,{0,1,2,3,4,5,6});
    for (auto t : {191,192}) define(t,60,7,{0,1,3,4});
    define(355,72,13,{0,1,2,3,11,12}); define(341,372,19,{0,1,12}); define(328,456,84,{0,1});
    define(340,120,7,{0,1,5,6}); define(274,64,12,{0,1,2,3,4,5,6,7}); define(270,24,7,{0,1,2,5,6});
    define(228,104,18,{0,1,2}); define(300,92,9,{0,1,4,5}); define(351,316,30,{0,1,25,26,27,28,29});
    define(332,48,9,{0,1,3}); define(294,20,6,{0,1,2,4,5});
    return tables;
}
Bytes row(const Table& t, std::uint32_t id)
{
    Bytes bytes(t.payload + 9); bytes[0] = 4; put(bytes,1,id); return bytes;
}
Bytes encode(const std::vector<Table>& tables, const std::string& version = "9.52", bool shortTrailers = false)
{
    Bytes bytes(160); str(bytes,0,"Xsteel " + version);
    for (std::size_t i = 0; i < tables.size(); ++i)
    {
        const auto& table = tables[i];
        u32(bytes,0xdbcec066); u32(bytes,table.payload); u32(bytes,static_cast<std::uint32_t>(table.fields.size()));
        for (auto field : table.fields) u32(bytes,field);
        for (const auto& record : table.rows) bytes.insert(bytes.end(),record.begin(),record.end());
        bytes.resize(bytes.size() + (i + 1 == tables.size() ? 5 : (shortTrailers ? 1 : 9)));
    }
    return bytes;
}
std::vector<Table> onePart(std::uint32_t type = 2, bool broken = false, bool contour = false)
{
    auto tables = schema();
    auto a = row(tables[61],1), b = row(tables[61],2); put<double>(b,9,10.0); tables[61].rows = {a,b};
    auto frame = row(tables[65],0); put<double>(frame,1,1.0); put<double>(frame,33,1.0); put<std::uint32_t>(frame,49,3); tables[65].rows = {frame};
    auto def = row(tables[341],4); str(def,55,"TEST"); str(def,117,"PL10*10"); tables[341].rows = {def};
    auto identity = row(tables[355],5); put(identity,29,type); tables[355].rows = {identity};
    auto part = row(tables[274],5); put<std::uint32_t>(part,5,4); put<std::uint32_t>(part,13,broken?999:1);
    put<std::uint32_t>(part,17,2); put<std::uint32_t>(part,25,3); put<double>(part,57,10.0);
    if (contour)
    {
        put<std::uint32_t>(part,21,6);
        auto link = row(tables[270],6); put<std::uint32_t>(link,17,7); tables[270].rows={link};
        auto block = row(tables[328],7); put<std::uint32_t>(block,341,0x7fffffff); tables[328].rows={block};
    }
    tables[274].rows = {part}; return tables;
}
std::vector<Table> onePart782(bool library = false)
{
    std::vector<Table> tables(library ? 198 : 228);
    const std::array<std::array<std::size_t,4>,18> roles{{
        {{61,40,32,6}}, {{64,43,40,7}}, {{65,44,52,8}}, {{75,53,45,6}},
        {{122,95,44,6}}, {{154,126,104,22}}, {{160,132,24,7}}, {{161,133,24,7}},
        {{190,160,24,7}}, {{191,161,60,7}}, {{192,162,60,7}}, {{209,179,63,8}},
        {{207,177,380,31}}, {{121,94,332,84}}, {{116,90,116,6}}, {{193,163,56,11}},
        {{226,196,60,16}}, {{181,151,88,8}}}};
    for (const auto& spec : roles)
    {
        auto& t = tables[spec[library ? 1 : 0]];
        t.payload = static_cast<std::uint32_t>(spec[2]); t.fields.assign(spec[3],0); t.fields[0]=1;
    }
    const auto at = [&](std::size_t main) -> Table& {
        for (const auto& spec : roles) if (spec[0]==main) return tables[spec[library?1:0]];
        throw std::runtime_error("unknown fixture role");
    };
    auto p1=row(at(61),1),p2=row(at(61),2); put<double>(p2,9,100); at(61).rows={p1,p2};
    auto frame=row(at(65),0); put<double>(frame,1,1); put<double>(frame,33,1); put<std::uint32_t>(frame,49,3); at(65).rows={frame};
    auto dim=row(at(75),10),more=row(at(75),11);
    put<std::uint32_t>(dim,5,11); str(dim,17,"10"); str(more,17,"*20"); at(75).rows={dim,more};
    auto def=row(at(207),4); put<std::uint32_t>(def,5,2); put<std::uint32_t>(def,9,2);
    str(def,81,"7"); str(def,103,"PLATE"); str(def,125,"PL"); put<std::uint32_t>(def,189,10);
    str(def,193,"PART_PREFIX"); str(def,215,"SECONDARY"); str(def,277,"S235"); str(def,309,"NOT_MATERIAL");
    auto boltDef=def; put<std::uint32_t>(boltDef,1,14); put<std::uint32_t>(boltDef,5,10); at(207).rows={def,boltDef};
    auto ident=row(at(209),5); put<std::uint32_t>(ident,17,50); put<std::uint32_t>(ident,21,20); str(ident,25,"ID01234567-0000-0000-0000-000000000001");
    auto boltIdent=ident; put<std::uint32_t>(boltIdent,1,15); put<std::uint32_t>(boltIdent,21,0); at(209).rows={ident,boltIdent};
    auto part=row(at(193),5); put<std::uint32_t>(part,5,4); put<std::uint32_t>(part,9,1); put<std::uint32_t>(part,13,2);
    put<std::uint32_t>(part,17,7); put<std::uint32_t>(part,21,3); put<double>(part,25,123); put<double>(part,49,100);
    auto bolt=part; put<std::uint32_t>(bolt,1,15); put<std::uint32_t>(bolt,5,14); at(193).rows={part,bolt};
    auto contour=row(at(121),7); put<float>(contour,13,2); put<float>(contour,17,8); put<float>(contour,53,3);
    put<std::uint32_t>(contour,221,0x7fffffff); at(121).rows={contour};
    auto assembly=row(at(181),20); put<std::uint32_t>(assembly,5,15); put<std::uint32_t>(assembly,13,5);
    str(assembly,21,"ASSEMBLY"); at(181).rows={assembly};
    auto association=row(at(192),30); put<std::uint32_t>(association,5,10); put<std::uint32_t>(association,9,15);
    put<std::uint32_t>(association,13,5); at(192).rows={association};
    auto property=row(at(122),40); str(property,17,"PHASE"); put<double>(property,9,12); at(122).rows={property};
    auto link=row(at(160),41); put<std::uint32_t>(link,5,40); put<std::uint32_t>(link,9,5); at(160).rows={link};
    if (library)
    {
        auto& t=tables[68]; t.payload=76; t.fields={1,0,0,0,0,0}; auto param=row(t,60);
        str(param,5,"WIDTH"); str(param,36,"Width"); put<std::uint32_t>(param,69,10); t.rows={param};
        auto parameterIdentity=ident; put<std::uint32_t>(parameterIdentity,1,60); at(209).rows.push_back(parameterIdentity);
        auto definitionIdentity=ident; put<std::uint32_t>(definitionIdentity,1,50); at(209).rows.push_back(definitionIdentity);
        auto& c=tables[125]; c.payload=32; c.fields={1,0,0,0,0,0,0,0,0};
        auto custom=row(c,50); put<std::uint32_t>(custom,5,4); put<std::uint32_t>(custom,21,912);
        put<std::uint32_t>(custom,25,10); put<std::uint32_t>(custom,29,12); c.rows={custom};
        auto name=row(at(75),12); str(name,17,"Anchor"); at(75).rows.push_back(name);
    }
    return tables;
}
}

int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    const auto root = std::filesystem::u8path(argv[1]) / argv[2];
    const std::string name = argv[2];
    try
    {
        const auto path = root / "model.db1";
        tekla::db1::Model model; tekla::db1::RawDatabase raw; std::string error = "stale";
        const auto parse = [&] { return tekla::db1::parseModelFile(path,model,error); };
        if (name.rfind("782_",0)==0)
        {
            const bool library=name.rfind("782_library",0)==0;
            auto s=onePart782(library);
            if (name=="782_library_fields") s[68].fields[1]=1;
            if (name=="782_library_definition_fields") s[125].fields[1]=1;
            if (name=="782_library_definition_width") { s[125].payload=36; s[125].rows.clear(); }
            if (name=="782_library_definition_string")
            {
                put<std::uint32_t>(s[125].rows[0],29,999);
                // A prior permissive lookup must not fabricate a valid string chunk.
                auto component=row(s[126],90); put<std::uint32_t>(component,13,999); s[126].rows={component};
            }
            if (name=="782_library_definition_identity") s[179].rows.pop_back();
            if (name=="782_library_definition_duplicate") s[125].rows.push_back(s[125].rows[0]);
            if (name=="782_library_parameter_string") put<std::uint32_t>(s[68].rows[0],69,999);
            if (name=="782_library_parameter_identity") s[179].rows.erase(s[179].rows.begin()+2);
            if (name=="782_library_parameter_duplicate") s[68].rows.push_back(s[68].rows[0]);
            if (name=="782_fields") s[207].fields[2]=1;
            if (name=="782_width") { s[207].payload=372; s[207].rows.clear(); }
            if (name=="782_part_join") put<std::uint32_t>(s[193].rows[0],9,999);
            if (name=="782_contour_join") put<std::uint32_t>(s[193].rows[0],17,999);
            if (name=="782_string_cycle") put<std::uint32_t>(s[75].rows[1],5,10);
            if (name=="782_missing_string") put<std::uint32_t>(s[75].rows[1],5,999);
            if (name=="782_bolt_join") put<std::uint32_t>(s[192].rows[0],13,999);
            if (name=="782_numeric_nan") put<double>(s[122].rows[0],9,std::numeric_limits<double>::quiet_NaN());
            if (name=="782_numeric_overflow") put<double>(s[122].rows[0],9,1e30);
            if (name=="782_assembly_missing") put<std::uint32_t>(s[209].rows[0],21,999);
            save(path,encode(s,"7.82",true));
            const bool ok=library?tekla::db1::parseComponentLibrary(path,model,error):parse();
            if (name=="782_model" || name=="782_library" || name=="782_assembly_missing")
            {
                check(ok,error.c_str()); const auto& p=model.parts.at(5);
                check(p.profile=="PL10*20" && p.material=="S235" && p.name=="PLATE","7.82 definition offsets");
                check(p.origin[0]==123 && p.length==100 && p.ownerId==50,"7.82 placement/owner offsets");
                check(p.contour.size()==2 && p.contour[0].value[0]==2 && p.contour[1].value[0]==8,"7.82 explicit first contour point");
                check(p.properties.size()==1 && p.properties[0].integerValue==12,"7.82 property join");
                check(model.actualPartIds==std::vector<std::uint32_t>{5} && model.unhandledPartIds.empty(),"7.82 type classification");
                check(model.boltGroups.empty() && model.individualBolts.size()==1 && model.individualBolts[0].id==15,"7.82 individual bolt");
                check(model.individualBolts[0].connectedPartIds==std::vector<std::uint32_t>{5},"7.82 bolt connection");
                check(model.assemblies.size()==1 && model.assemblies[0].memberIds==std::vector<std::uint32_t>{5},"7.82 assembly membership");
                if (library)
                {
                    check(model.parameterDefinitions.at(60).expression=="10*20","7.82 parameter expression");
                    check(model.customComponentDefinitions.size()==1,"7.82 definition missing");
                    const auto& c=model.customComponentDefinitions[0];
                    check(c.id==50 && c.name=="Anchor" && c.description=="10*20" && c.kind==4 && c.classificationCode==912,"7.82 definition fields");
                    check(c.referenceIds==std::array<std::uint32_t,3>{10,12,0},"7.82 nonexistent third reference read");
                    check(c.guid==model.identities.at(50).guid && !c.guid.empty(),"7.82 definition identity join");
                    check(c.parameterIds==std::vector<std::uint32_t>{60} && c.childObjectIds==std::vector<std::uint32_t>{5,15,60},"7.82 definition owner joins");
                }
                if (name=="782_model")
                {
                    save(root/"xslib.db1",encode(onePart782(true),"7.82",true));
                    tekla::Project project;
                    check(tekla::readProject(root,project,error),error.c_str());
                    check(project.componentLibrary.has_value(),"7.82 project library missing");
                    check(project.files.size()==2,"7.82 project inventory");
                    for (const auto& file : project.files) check(file.level==tekla::ReadLevel::PartialSemantic,"7.82 coverage overstated");
                }
                if (name=="782_assembly_missing")
                {
                    check(model.identities.at(5).flags==999,"unresolved assembly reference lost");
                    check(std::any_of(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& x){return x.find("unresolved stored assembly reference 999")!=std::string::npos;}),"unresolved assembly diagnostic");
                }
            }
            else { check(!ok,"malformed 7.82 accepted"); check(!error.empty() && model.parts.empty(),"7.82 failure contract"); }
            if (name=="782_part_join")
            {
                s=onePart782(); s[209].rows.erase(s[209].rows.begin());
                auto relation=row(s[191],31); put<std::uint32_t>(relation,5,11);
                put<std::uint32_t>(relation,9,15); put<std::uint32_t>(relation,13,5); s[191].rows={relation};
                save(path,encode(s,"7.82",true));
                check(!parse() && error.find("association identity")!=std::string::npos,"association fabricated missing identity");
            }
        }
        else if (name == "valid_model" || name == "short_trailer")
        {
            save(path,encode(onePart(),"9.52",name=="short_trailer"));
            check(parse(),error.c_str()); check(error.empty(),"stale error");
            check(model.actualPartIds == std::vector<std::uint32_t>{5},"part IDs");
            check(model.parts.at(5).length == 10 && model.parts.at(5).end[0] == 10,"part values");
            check(model.parts.at(5).profile == "PL10*10","profile string");
        }
        else if (name == "unknown_version") { save(path,encode(schema(),"99.99")); check(!parse(),"unknown version accepted"); check(!error.empty(),"missing diagnostic"); }
        else if (name == "wrong_fields") { auto s=schema(); s[61].fields[1]=0; save(path,encode(s)); check(!parse(),"wrong field signature accepted"); }
        else if (name == "broken_reference") { save(path,encode(onePart(2,true))); check(!parse(),"broken join accepted"); check(model.parts.empty(),"failure leaked partial model"); }
        else if (name == "unhandled_part") { save(path,encode(onePart(999))); check(parse(),error.c_str()); check(model.unhandledPartIds==std::vector<std::uint32_t>{5},"unhandled part lost"); }
        else if (name == "unverified_contour") { save(path,encode(onePart(2,false,true))); check(parse(),error.c_str()); check(model.parts.at(5).contourKindUnverified,"unknown contour classified as plate"); }
        else if (name == "embedded_magic")
        {
            std::vector<Table> s(1); s[0].payload=32; auto r=row(s[0],42); put<std::uint32_t>(r,5,0xdbcec066);
            put<std::uint32_t>(r,9,4); put<std::uint32_t>(r,13,1); put<std::uint32_t>(r,17,1); s[0].rows={r}; save(path,encode(s));
            check(tekla::db1::parseRawDatabase(path,raw,error),error.c_str());
            check(raw.tables.size()==1 && raw.tables[0].records.size()==1,"payload magic split table");
            check(raw.tables[0].records[0].payload.size()==32,"payload lost");
        }
        else if (name == "plain_limit" || name == "gzip_limit")
        {
            auto b=encode(schema()); save(path,name=="gzip_limit"?gzip(b):b);
            tekla::db1::RawDatabaseOptions options; options.maxDecodedBytes=100;
            check(!tekla::db1::parseRawDatabase(path,raw,error,options),"byte limit ignored"); check(raw.tables.empty(),"partial raw on failure");
        }
        else if (name == "gzip_concat")
        {
            auto b=encode(onePart()); const auto half=b.size()/2; auto a=gzip(Bytes(b.begin(),b.begin()+half)); auto c=gzip(Bytes(b.begin()+half,b.end()));
            a.insert(a.end(),c.begin(),c.end()); save(path,a); check(parse(),error.c_str()); check(model.actualPartIds.size()==1,"concatenated member lost");
        }
        else if (name == "gzip_corrupt" || name == "gzip_truncated" || name == "gzip_garbage")
        {
            auto b=gzip(encode(onePart()));
            if (name=="gzip_corrupt") b[b.size()-8]^=1;
            if (name=="gzip_truncated") b.resize(b.size()-3);
            if (name=="gzip_garbage") b.push_back(42);
            save(path,b); check(!parse(),"invalid gzip accepted"); check(!error.empty(),"missing gzip diagnostic");
        }
        else if (name == "truncations")
        {
            auto b=encode(onePart());
            for (auto n : {std::size_t(0),std::size_t(7),std::size_t(159),b.size()-1,b.size()-4})
            { save(path,Bytes(b.begin(),b.begin()+n)); check(!parse(),"truncated model accepted"); }
        }
        else if (name == "unicode_path")
        {
            const auto unicode=root/std::filesystem::u8path("\xe6\xa8\xa1\xe5\x9e\x8b")/"model.db1";
            save(unicode,encode(onePart())); check(tekla::db1::parseModelFile(unicode,model,error),error.c_str());
        }
        else if (name == "ambiguity")
        {
            save(path,encode(schema())); save(root/"other.db1",encode(schema()));
            check(!tekla::db1::parseModelDirectory(root,model,error),"ambiguous directory accepted"); check(parse(),error.c_str());
        }
        else if (name == "raw_retention")
        {
            auto b=encode(onePart()); save(path,b); tekla::db1::RawDatabaseOptions options; options.retainDecompressedFileImage=true;
            check(tekla::db1::parseRawDatabase(path,raw,error,options),error.c_str()); check(raw.decompressedFileImage==b,"raw bytes changed"); check(error.empty(),"raw stale error");
        }
        else if (name == "project" || name == "strict_companions")
        {
            save(path,encode(onePart())); const std::string h="Xsteel\x80 9.52 00000000-0000-0000-0000-000000000001";
            save(root/"model.db2",Bytes(h.begin(),h.end()));
            save(root/"drawings"/"drawing.dg",Bytes{1,2,3});
            if (name=="strict_companions") save(root/"environment.db",Bytes{1,2,3});
            tekla::Project project; tekla::ProjectOptions options; options.strictCompanions=name=="strict_companions";
            const auto ok=tekla::readProject(root,project,error,options);
            if (options.strictCompanions) { check(!ok,"strict companion failure ignored"); check(project.files.empty(),"partial failed project"); }
            else
            {
                check(ok,error.c_str()); check(project.rawCompanions.size()==1,"DB2 not retained");
                check(project.associations.size()==1,"DB1/DB2 association missing");
                check(project.rawCompanions.begin()->second.kind==tekla::db1::DatabaseKind::Numbering,"DB2 misclassified");
            }
        }
        else if (name == "duplicate_id")
        {
            auto s=onePart(); s[355].rows.push_back(s[355].rows.front()); save(path,encode(s));
            check(!parse(),"duplicate identity accepted"); check(error.find("duplicate")!=std::string::npos,"duplicate diagnostic missing");
        }
        else if (name == "nonfinite")
        {
            auto s=onePart(); put<double>(s[61].rows.front(),9,std::numeric_limits<double>::quiet_NaN()); save(path,encode(s));
            check(!parse(),"NaN point accepted"); check(error.find("non-finite")!=std::string::npos,"NaN diagnostic missing");
        }
        else if (name == "model_limit")
        {
            save(path,gzip(encode(onePart()))); tekla::db1::ModelReadOptions options; options.maxDecodedBytes=100;
            check(!tekla::db1::parseModelFile(path,model,error,options),"model byte limit ignored");
        }
        else if (name == "dbv_name")
        {
            Bytes b{'D','B','V','@'}; u32(b,11); const std::string n="Environment"; b.insert(b.end(),n.begin(),n.end()); b.resize(b.size()+16);
            const auto p=root/"environment.db"; save(p,b);
            check(tekla::db1::parseRawDatabase(p,raw,error),error.c_str());
            check(raw.containerName==n && raw.storageVersion=="DBV","DBV length confused with version");
            put<std::uint32_t>(b,4,0xffffffffU); save(p,b);
            check(!tekla::db1::parseRawDatabase(p,raw,error),"invalid DBV name length accepted");
        }
        else if (name == "project_precedence")
        {
            save(path,encode(onePart())); const std::string a="LOCAL!TYPE!0!0!1!1", b="EXTERNAL!TYPE!0!0!1!1";
            save(root/"profitab.inp",Bytes(a.begin(),a.end())); save(root/"resources"/"profitab.inp",Bytes(b.begin(),b.end()));
            tekla::Project project; tekla::ProjectOptions options; options.resourceDirectories={"resources"};
            check(tekla::readProject(root,project,error,options),error.c_str());
            check(project.profileRules && project.profileRules->rules.front().prefix=="LOCAL","external resource overrides local resource");
        }
        else if (name == "project_guid")
        {
            auto b=encode(onePart()); str(b,0,"Xsteel 9.52 01234567-89ab-cdef-0123-456789abcdef"); save(path,b);
            const std::string header="Xsteel\x80 9.52 01234567-89ab-cdef-0123-456789abcdee";
            save(root/"model.db2",Bytes(header.begin(),header.end()));
            for (bool rawCompanions:{true,false})
            {
                tekla::Project project; tekla::ProjectOptions options; options.readRawCompanions=rawCompanions;
                check(tekla::readProject(root,project,error,options),error.c_str());
                check(project.numbering.size()==1 && project.associations.empty(),"conflicting DB2 GUID associated by basename");
                bool partial=false,diagnostic=false;
                for (const auto& f:project.files) partial |= f.level==tekla::ReadLevel::PartialSemantic;
                for (const auto& d:project.diagnostics) diagnostic |= d.find("GUID differs")!=std::string::npos;
                check(partial && diagnostic,"partial numbering/mismatched GUID not reported");
            }
        }
        else if (name == "partial_project")
        {
            save(path,encode(onePart())); save(root/"environment.db",Bytes{1,2,3}); tekla::Project project;
            check(tekla::readProject(root,project,error),error.c_str()); bool failed=false;
            for (const auto& f:project.files) failed |= f.level==tekla::ReadLevel::Failed;
            check(failed && !project.diagnostics.empty(),"partial companion silently ignored");
        }
        else if (name == "gzip_large")
        {
            // Exercise decoder output draining at and beyond the chunk boundary.
            for (std::size_t size : {262144,262145,1048576})
            {
                Bytes b(size); str(b,0,"DBV@"); put<std::uint32_t>(b,4,1); b[8]='X';
                const auto p=root/"options_model.db"; save(p,gzip(b));
                check(tekla::db1::parseRawDatabase(p,raw,error),error.c_str()); check(raw.preamble==b,"large gzip output changed");
            }
        }
        else if (name == "catalog_error")
        {
            const auto p=root/"bad.tez"; const std::string xml="<Polymesh><Points><Point><X>1</X>"; save(p,Bytes(xml.begin(),xml.end()));
            tekla::db1::ShapeGeometry geometry; check(!tekla::db1::parseShapeGeometry(p,geometry,error),"truncated XML accepted"); check(!error.empty(),"catalog diagnostic cleared");
        }
        else throw std::runtime_error("unknown test");
        std::cout << "PASS " << name << '\n'; return 0;
    }
    catch (const std::exception& e) { std::cerr << name << ": " << e.what() << '\n'; return 1; }
}
