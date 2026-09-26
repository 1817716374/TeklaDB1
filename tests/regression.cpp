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
    define(245,52,14,{0,1});
    define(211,12,4,{0,1,2,3}); define(322,68,9,{0,1,2});
    define(323,76,11,{0,1,2}); define(324,44,7,{0,1,2});
    define(183,32,9,{0,1,2,4,5,6,7}); define(184,56,12,{0,1,2,3,4,5,6,7,8});
    define(205,120,18,{0,1,2}); define(206,60,16,{0,1,2});
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
    const std::array<std::array<std::size_t,4>,23> roles{{
        {{61,40,32,6}}, {{64,43,40,7}}, {{65,44,52,8}}, {{75,53,45,6}},
        {{122,95,44,6}}, {{154,126,104,22}}, {{160,132,24,7}}, {{161,133,24,7}},
        {{190,160,24,7}}, {{191,161,60,7}}, {{192,162,60,7}}, {{209,179,63,8}},
        {{207,177,380,31}}, {{121,94,332,84}}, {{116,90,116,6}}, {{193,163,56,11}},
        {{226,196,60,16}}, {{181,151,88,8}}, {{145,117,78,12}}, {{146,118,292,29}},
        {{211,181,12,4}}, {{215,185,64,8}}, {{216,186,72,10}}}};
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
        for (const auto& text : std::vector<std::pair<std::uint32_t,std::string>>{
            {13,"D1"},{14,"Length"},{16,"proPOSITION1"},{17,"PlaneXY"},{18,"WIDTH+2"}})
        {
            auto chunk=row(at(75),text.first); str(chunk,17,text.second); at(75).rows.push_back(chunk);
        }
        auto& d=tables[147]; d.payload=64; d.fields.assign(14,0); d.fields[0]=1;
        auto distance=row(d,70); put<std::uint32_t>(distance,5,13); put<std::uint32_t>(distance,9,14);
        put<double>(distance,17,12.5); put<double>(distance,25,30); put<std::uint32_t>(distance,41,0xffffffff);
        put<std::uint32_t>(distance,57,16); put<std::uint32_t>(distance,61,17); d.rows={distance};
        auto& f=tables[156]; f.payload=97; f.fields={1,0,0,0,0,0};
        auto formula=row(f,80); put<std::uint32_t>(formula,5,70); put<std::uint32_t>(formula,9,2);
        put<std::uint32_t>(formula,13,18); str(formula,17,"proVALUE"); f.rows={formula};
        for (auto id : {70U,80U})
        {
            auto identity=ident; put<std::uint32_t>(identity,1,id); at(209).rows.insert(at(209).rows.end()-1,identity);
        }
        for (const auto& spec : std::vector<std::array<std::uint32_t,4>>{
            {71,58,70,5},{72,58,70,50},{81,59,80,70},{82,59,80,60}})
        {
            auto a=row(at(192),spec[0]); put<std::uint32_t>(a,5,spec[1]);
            put<std::uint32_t>(a,9,spec[2]); put<std::uint32_t>(a,13,spec[3]); at(192).rows.push_back(a);
        }
    }
    return tables;
}
std::vector<Table> componentLibrary(bool older895)
{
    const auto main=onePart();
    std::vector<Table> tables(older895 ? 290 : 319);
    for (const auto& pair : std::vector<std::pair<std::size_t,std::size_t>>{
        {61,40},{64,43},{65,44},{75,53},{122,95},{154,126},{160,132},{161,133},{190,160},{191,161},{192,162},
        {355,older895?260:318},{341,older895?264:305},{328,older895?94:292},{340,older895?90:304},
        {274,242},{270,238},{228,198},{300,265},{351,older895?223:314},{310,274},{294,261},{245,215},
        {211,181},{322,286},{323,287},{324,288},{183,153},{184,154},{205,175},{206,176}})
        tables[pair.second]=main[pair.first];
    if (!older895) tables[296]=main[332];
    const auto define=[&](std::size_t n,std::uint32_t width,std::size_t count,std::initializer_list<std::size_t> refs) {
        auto& t=tables[n]; t.payload=width; t.fields.assign(count,0); t.rows.clear();
        for (auto ref : refs) t.fields[ref]=1;
    };
    if (older895)
    {
        define(260,55,6,{0,1,2,3,4}); define(264,332,19,{0,1,12});
        define(94,332,84,{0,1}); define(90,116,6,{0,1,5}); define(223,308,28,{0,1});
        define(279,28,8,{0,1,6,7});
        auto def=row(tables[264],4); str(def,55,"TEST"); str(def,145,"PL10*10"); tables[264].rows={def};
    }
    auto legacy=onePart782(true);
    for (auto n : {53U,68U,147U,156U}) tables[n]=legacy[n];
    tables[53].fields={1,1,1,0,0,0}; tables[68].fields={1,1,0,0,1,0};
    tables[147].fields={1,1,1,1,0,0,0,0,0,0,0,0,1,1}; tables[156].fields={1,1,1,0,1,0};
    // Keep references to two different component owners to exercise cross-owner inputs.
    define(226,36,10,{0,1,7,8,9});
    auto custom=row(tables[226],50); put<std::uint32_t>(custom,5,4); put<std::uint32_t>(custom,29,12); tables[226].rows={custom};
    auto component=row(tables[126],90); put<std::uint32_t>(component,13,12); tables[126].rows={component};
    const auto identityTable=older895?260U:318U; tables[identityTable].rows.clear();
    for (const auto& spec : std::vector<std::array<std::uint32_t,3>>{{5,50,2},{50,50,4},{60,90,64},{70,50,58},{80,50,59},{90,90,3}})
    {
        auto identity=row(tables[identityTable],spec[0]);
        if (older895)
        {
            const auto classId=100+spec[2];
            auto cls=row(tables[279],classId); put<std::uint32_t>(cls,5,spec[2]); tables[279].rows.push_back(cls);
            put<std::uint32_t>(identity,5,classId); put<std::uint32_t>(identity,9,spec[1]);
            str(identity,17,"01234567-0000-0000-0000-000000000001");
        }
        else { put<std::uint32_t>(identity,5,spec[1]); put<std::uint32_t>(identity,29,spec[2]); }
        tables[identityTable].rows.push_back(identity);
    }
    auto aux=row(tables[215],1000);
    put<float>(aux,5,-25.0f); put<float>(aux,17,25.0f);
    put<std::uint32_t>(aux,29,2); put<float>(aux,33,-4.0f);
    put<std::uint32_t>(aux,45,1); put<float>(aux,49,3.3f);
    put<std::uint32_t>(aux,9,0x7fc01234U); // opaque bytes are not guessed to be floats
    tables[215].rows={aux}; put<std::uint32_t>(tables[242].rows[0],9,1000);
    tables[162].rows.assign(legacy[162].rows.begin()+1,legacy[162].rows.end());
    for (const auto& spec : std::vector<std::pair<std::uint32_t,std::uint32_t>>{{91,1},{92,2},{93,5}})
    {
        auto ref=row(tables[162],spec.first); put<std::uint32_t>(ref,5,4);
        put<std::uint32_t>(ref,9,50); put<std::uint32_t>(ref,13,spec.second); tables[162].rows.push_back(ref);
    }
    // Reference points also have identity records in real libraries.
    for (auto id : {1U,2U})
    {
        auto identity=row(tables[identityTable],id);
        if (older895)
        {
            if (id==1) { auto cls=row(tables[279],101); put<std::uint32_t>(cls,5,1); tables[279].rows.push_back(cls); }
            put<std::uint32_t>(identity,5,101); put<std::uint32_t>(identity,9,50);
        }
        else { put<std::uint32_t>(identity,5,50); put<std::uint32_t>(identity,29,1); }
        tables[identityTable].rows.push_back(identity);
    }
    return tables;
}
}

#include "ObjectNumberingRegression.hpp"
#include "ReinforcementNumberingRegression.hpp"
#include "ReinforcementRegression.hpp"
#include "BoltPositionRegression.hpp"
#include "LegacyNumberingRegression.hpp"
#include "InlineNumbering730Regression.hpp"

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
        if (name.rfind("inline_number_730_",0)==0) inlineNumbering730Regression(path,name);
        else if (name.rfind("legacy_number_",0)==0) legacyNumberingRegression(path,name);
        else if (name.rfind("rebar_number_",0)==0) reinforcementNumberingRegression(path,name);
        else if (name.rfind("object_number_",0)==0) objectNumberingRegression(path,name);
        else if (name.rfind("reinforcement_",0)==0) reinforcementRegression(path,name);
        else if (name.rfind("bolt_position_",0)==0) boltPositionRegression(path,name);
        else if (name.rfind("inline_position_",0)==0)
        {
            const bool library=name!="inline_position_main";
            auto s=onePart782(library);
            const auto def=library?177U:207U, part=library?163U:193U;
            auto& d=s[def].rows[0];
            put<float>(d,21,-12.5f); put<float>(d,33,25.25f);
            put<std::uint32_t>(d,45,2); put<float>(d,49,13.5f);
            put<std::uint32_t>(d,61,1); put<float>(d,65,-7.25f);
            const std::array<std::size_t,6> offsets{{25,29,37,41,53,57}};
            for (std::size_t i=0;i<offsets.size();++i) put<std::uint32_t>(d,offsets[i],0x7fc01234U+static_cast<std::uint32_t>(i));
            // A shared definition and an unused definition must both survive.
            put<std::uint32_t>(s[part].rows[1],5,4);
            if (name=="inline_position_start_nan") put<float>(d,21,std::numeric_limits<float>::quiet_NaN());
            if (name=="inline_position_end_inf") put<float>(d,33,std::numeric_limits<float>::infinity());
            if (name=="inline_position_depth_nan") put<float>(d,49,std::numeric_limits<float>::quiet_NaN());
            if (name=="inline_position_plane_inf") put<float>(d,65,std::numeric_limits<float>::infinity());
            if (name=="inline_position_unused_nan") put<float>(s[def].rows[1],21,std::numeric_limits<float>::quiet_NaN());
            if (name=="inline_position_unknown") {put<std::uint32_t>(d,45,99);put<std::uint32_t>(d,61,100);}
            save(path,encode(s,"7.82",library));
            const auto readModel=[&] {return library?tekla::db1::parseComponentLibrary(path,model,error):parse();};
            const bool valid=name=="inline_position_main" || name=="inline_position_library" || name=="inline_position_unknown";
            const bool ok=readModel();
            if (valid)
            {
                check(ok,error.c_str());
                check(model.partPositions.empty() && model.partDefinitionPositions.size()==2,"inline position records lost or namespaces mixed");
                const auto& p=model.partDefinitionPositions.at(4);
                check(p.id==4 && p.startAxialOffset==-12.5f && p.endAxialOffset==25.25f && p.depthOffset==13.5f && p.planeOffset==-7.25f,"inline position values lost");
                check(p.depthCode==(name=="inline_position_unknown"?99U:2U) && p.planeCode==(name=="inline_position_unknown"?100U:1U),"inline position codes changed");
                for (std::size_t i=0;i<6;++i) check(p.rawFields[i]==0x7fc01234U+i,"opaque inline position bits lost");
                check(model.partDefinitionPositions.at(14).id==14,"unused inline definition discarded");
                for (auto id:{5U,15U}) check(model.parts.at(id).definitionId==4 && model.parts.at(id).auxiliaryReferenceId==0 && model.parts.at(id).origin[0]==123 && model.parts.at(id).length==100,"shared position or stored geometry changed");
                if (name=="inline_position_unknown") check(std::any_of(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& x){return x.find("unverified part position code")!=std::string::npos;}),"unknown inline code not diagnosed");
                // A failed read on a reused model must clear the new position map too.
                save(path,{1,2,3}); check(!readModel() && model.partDefinitionPositions.empty() && model.parts.empty(),"failed reread kept inline positions");
            }
            else check(!ok && error.find("non-finite part position")!=std::string::npos && model.partDefinitionPositions.empty() && model.definitions.empty(),"invalid inline position accepted or partial model retained");
        }
        else if (name.rfind("surface_",0)==0)
        {
            const bool library=name!="surface_main" && name!="surface_project_no_catalog";
            auto s=onePart782(library);
            const auto surface=library?117U:145U, definition=library?118U:146U, identity=library?179U:209U;
            const auto association=library?161U:191U, contour=library?94U:121U, propertyLink=library?132U:160U;
            auto def=row(s[definition],101); str(def,69,"7"); str(def,91,"TEST SURFACE"); str(def,113,"PL1.587");
            str(def,175,"Zero_Density"); str(def,207,"TS1 - Tile surface 1"); put<std::uint32_t>(def,269,3);
            put<std::uint32_t>(def,5,196); put<std::uint32_t>(def,61,6); put<std::uint32_t>(def,273,99); s[definition].rows={def};
            auto object=row(s[surface],100); put<std::uint32_t>(object,5,101); put<std::uint32_t>(object,9,1);
            put<std::uint32_t>(object,13,2); put<std::uint32_t>(object,17,8); put<std::uint32_t>(object,21,3);
            put<double>(object,25,20); put<double>(object,49,30); object[57]=0xab; object[78]=0xcd; s[surface].rows={object};
            auto ident=s[identity].rows[0]; put<std::uint32_t>(ident,1,100); s[identity].rows.push_back(ident);
            auto polygon=s[contour].rows[0]; put<std::uint32_t>(polygon,1,8); put<std::uint32_t>(polygon,221,0);
            put<std::uint32_t>(polygon,225,0x7fffffff); put<float>(polygon,21,9); put<float>(polygon,61,9); s[contour].rows.push_back(polygon);
            auto father=row(s[association],102); put<std::uint32_t>(father,5,73); put<std::uint32_t>(father,9,5);
            put<std::uint32_t>(father,13,100); s[association].rows.push_back(father);
            auto property=s[propertyLink].rows[0]; put<std::uint32_t>(property,1,103); put<std::uint32_t>(property,9,100); s[propertyLink].rows.push_back(property);
            if (library)
            {
                put<std::uint32_t>(s[156].rows[0],5,100);
                for (auto& r:s[162].rows)
                {
                    std::uint32_t id=0; std::memcpy(&id,r.data()+1,4);
                    if (id==71 || id==81) put<std::uint32_t>(r,13,100);
                }
            }
            if (name=="surface_fields") s[surface].fields[1]=1;
            if (name=="surface_definition_fields") s[definition].fields[1]=1;
            if (name=="surface_width") {s[surface].payload=80;s[surface].rows.clear();}
            if (name=="surface_definition_missing") s[definition].rows.clear();
            if (name=="surface_point_missing") put<std::uint32_t>(s[surface].rows[0],9,999);
            if (name=="surface_frame_missing") put<std::uint32_t>(s[surface].rows[0],21,999);
            if (name=="surface_contour_missing") put<std::uint32_t>(s[surface].rows[0],17,999);
            if (name=="surface_identity_missing") s[identity].rows.pop_back();
            if (name=="surface_father_missing") s[association].rows.pop_back();
            if (name=="surface_father_unknown") put<std::uint32_t>(s[association].rows.back(),9,999);
            if (name=="surface_father_duplicate") s[association].rows.push_back(s[association].rows.back());
            if (name=="surface_target_missing") {auto extra=father;put<std::uint32_t>(extra,13,999);s[association].rows.push_back(extra);}
            if (name=="surface_duplicate") s[surface].rows.push_back(s[surface].rows[0]);
            if (name=="surface_definition_duplicate") s[definition].rows.push_back(s[definition].rows[0]);
            if (name=="surface_origin_nan") put<double>(s[surface].rows[0],25,std::numeric_limits<double>::quiet_NaN());
            if (name=="surface_length_inf") put<double>(s[surface].rows[0],49,std::numeric_limits<double>::infinity());
            if (name=="surface_contour_nan") put<float>(s[contour].rows.back(),13,std::numeric_limits<float>::quiet_NaN());
            if (name=="surface_chamfer_nan") {put<std::uint32_t>(s[contour].rows.back(),213,20);put<float>(s[contour].rows.back(),133,std::numeric_limits<float>::quiet_NaN());}
            if (name=="surface_unknown_profile") s[definition].rows[0][113]='X';
            save(path,encode(s,"7.82"));
            const bool ok=library?tekla::db1::parseComponentLibrary(path,model,error):parse();
            if (name=="surface_main" || name=="surface_library" || name=="surface_unknown_profile" || name=="surface_project_no_catalog")
            {
                check(ok,error.c_str()); const auto& treatment=model.surfaceTreatments.at(100);
                const auto& definitionValue=model.surfaceTreatmentDefinitions.at(101);
                check(model.parts.size()==2 && !model.parts.count(100),"surface fabricated a structural part");
                check(treatment.fatherPartId==5 && treatment.ownerId==50 && treatment.definitionId==101 && treatment.contour.size()==3,"surface object joins lost");
                check(treatment.startPointId==1 && treatment.endPointId==2 && treatment.orientationId==3 && treatment.origin[0]==20 && treatment.storedLength==30,"surface geometry overwritten");
                check(model.surfaceTreatmentIdsByFather.at(5)==std::vector<std::uint32_t>{100},"surface reverse father join lost");
                check(treatment.rawTail.front()==0xab && treatment.rawTail.back()==0xcd && definitionValue.rawHeader[0]==196 && definitionValue.rawHeader[14]==6 && definitionValue.rawTail[0]==99,"surface opaque bytes lost");
                check(definitionValue.name=="TEST SURFACE" && definitionValue.classNumber=="7" && definitionValue.material=="Zero_Density" && definitionValue.typeCode==3 && definitionValue.typeName=="TS1 - Tile surface 1","surface definition fields lost");
                check(name=="surface_unknown_profile"?!definitionValue.thicknessFromProfile:(definitionValue.thicknessFromProfile && *definitionValue.thicknessFromProfile==1.587),"surface profile thickness guessed");
                check(treatment.properties.size()==1 && treatment.properties[0].name=="PHASE","surface properties lost");
                if (library) check(treatment.distanceParameterIds==std::vector<std::uint32_t>{70} && treatment.formulaBindingIds==std::vector<std::uint32_t>{80},"surface variable graph lost");
                if (name=="surface_project_no_catalog")
                {
                    tekla::Project project; tekla::ProjectOptions options; options.mainDatabase=path.filename(); options.readComponentLibrary=false;
                    check(tekla::readProject(path.parent_path(),project,error,options),error.c_str());
                    check(project.model.surfaceTreatments.size()==1 && project.surfaceMaterialAssociations.empty(),"surface lost or material association fabricated without catalog");
                    check(std::any_of(project.diagnostics.begin(),project.diagnostics.end(),[](const auto& x){return x.find("no material catalog")!=std::string::npos;}),"missing surface catalog diagnostic lost");
                }
            }
            else check(!ok && !error.empty() && model.surfaceTreatments.empty() && model.surfaceTreatmentDefinitions.empty() && model.surfaceTreatmentIdsByFather.empty() && model.parts.empty(),"malformed surface accepted or partial result retained");
        }
        else if (name.rfind("ownership_",0)==0)
        {
            const bool older=name.find("895")!=std::string::npos;
            auto s=componentLibrary(older); const auto ident=older?260U:318U;
            if (name=="ownership_other_scope") { s[126].rows.clear(); put<std::uint32_t>(s[ident].rows[5],29,60); }
            if (name=="ownership_895_class_fields") s[279].fields[7]=0;
            if (name=="ownership_895_class_width") { s[279].payload=32; s[279].rows.clear(); }
            if (name=="ownership_895_class_missing") put<std::uint32_t>(s[ident].rows[0],5,999);
            if (name=="ownership_895_class_duplicate") s[279].rows.push_back(s[279].rows[0]);
            if (name=="ownership_895_owner_missing") put<std::uint32_t>(s[ident].rows[0],9,999);
            if (name=="ownership_aux_fields") s[215].fields[2]=1;
            if (name=="ownership_aux_missing") put<std::uint32_t>(s[242].rows[0],9,999);
            if (name=="ownership_aux_duplicate") s[215].rows.push_back(s[215].rows[0]);
            if (name=="ownership_position_start_nan") put<float>(s[215].rows[0],5,std::numeric_limits<float>::quiet_NaN());
            if (name=="ownership_position_end_inf") put<float>(s[215].rows[0],17,std::numeric_limits<float>::infinity());
            if (name=="ownership_position_depth_nan") put<float>(s[215].rows[0],33,std::numeric_limits<float>::quiet_NaN());
            if (name=="ownership_position_plane_inf") put<float>(s[215].rows[0],49,std::numeric_limits<float>::infinity());
            if (name=="ownership_position_unknown") put<std::uint32_t>(s[215].rows[0],29,99);
            if (name=="ownership_distance_fields") s[147].fields[12]=0;
            if (name=="ownership_formula_fields") s[156].fields[4]=0;
            if (name=="ownership_formula_cycle") put<std::uint32_t>(s[53].rows.back(),5,18);
            if (name=="ownership_formula_missing_chunk") put<std::uint32_t>(s[53].rows.back(),5,999);
            if (name=="ownership_custom_reference_missing") put<std::uint32_t>(s[162].rows.back(),13,999);
            save(path,encode(s,older?"8.95":"9.52"));
            const bool ok=tekla::db1::parseComponentLibrary(path,model,error);
            if (name=="ownership_895" || name=="ownership_modern" || name=="ownership_other_scope" || name=="ownership_position_unknown")
            {
                check(ok,error.c_str());
                check(model.identities.at(5).ownerId==50 && model.parts.at(5).ownerId==50 && model.parts.at(5).auxiliaryReferenceId==1000,"owner/auxiliary conflated");
                const auto& position=model.partPositions.at(1000);
                check(position.startAxialOffset==-25.0f && position.endAxialOffset==25.0f && position.depthOffset==-4.0f && position.planeOffset==3.3f,"part position values lost");
                check(position.planeCode==1 && position.depthCode==(name=="ownership_position_unknown"?99U:2U) && position.rawFields[0]==0x7fc01234U,"position codes/opaque fields changed");
                check(model.parts.at(5).length==10.0 && model.parts.at(5).origin==tekla::db1::Vec3{},"stored geometry adjusted twice");
                if (name=="ownership_position_unknown") check(std::any_of(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& x){return x.find("unverified part position code")!=std::string::npos;}),"unknown position code not diagnosed");
                check(model.customComponentDefinitions.size()==1,"custom definition missing");
                const auto& c=model.customComponentDefinitions[0];
                check(c.parameterIds.empty() && c.childObjectIds==std::vector<std::uint32_t>{1,2,5,70,80},"definition ownership lost");
                check(model.controlLines.empty() && c.referenceObjectIds==std::vector<std::uint32_t>{1,2,5},"custom references misclassified or non-point target lost");
                check(model.customComponentReferences.at(50)==c.referenceObjectIds,"custom reference index lost");
                check(c.distanceParameterIds==std::vector<std::uint32_t>{70} && c.formulaBindingIds==std::vector<std::uint32_t>{80},"definition variable graph lost");
                if (name=="ownership_other_scope") check(model.components.empty(),"non-component scope fabricated a component");
                else check(model.components[0].id==90 && model.components[0].parameterIds==std::vector<std::uint32_t>{60},"component parameter ownership lost");
                check(model.variablesByOwner.at(90).parameterIds==std::vector<std::uint32_t>{60},"generic variable scope lost");
                check(model.variablesByOwner.at(50).distanceParameterIds==std::vector<std::uint32_t>{70} && model.variablesByOwner.at(50).formulaBindingIds==std::vector<std::uint32_t>{80},"generic formula/distance scope lost");
                check(model.parameterDefinitions.at(60).ownerId==90 && model.distanceParameters.at(70).ownerId==50,"variable owner offset");
                check(model.formulaBindings.at(80).inputObjectIds==std::vector<std::uint32_t>{60},"cross-owner input removed");
                if (older)
                {
                    check(model.identities.at(5).classReferenceId==102 && model.identityClasses.at(102).recordKind==2,"identity class join");
                    check(model.identityClasses.at(104).recordKind==4 && model.controlLines.empty(),"class kind confused with association type");
                }
                else check(model.identities.at(5).contextId==0 && model.identityClasses.empty(),"modern identity read with legacy offsets");
            }
            else check(!ok && !error.empty() && model.parts.empty() && model.identityClasses.empty() && model.formulaBindings.empty() && model.variablesByOwner.empty() && model.partPositions.empty(),"malformed ownership accepted or partial output retained");
        }
        else if (name.rfind("782_",0)==0)
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
            if (name=="782_library_distance_fields") s[147].fields[2]=1;
            if (name=="782_library_distance_width") { s[147].payload=68; s[147].rows.clear(); }
            if (name=="782_library_distance_string") put<std::uint32_t>(s[147].rows[0],61,999);
            if (name=="782_library_distance_identity") put<std::uint32_t>(s[147].rows[0],1,999);
            if (name=="782_library_distance_nan") put<double>(s[147].rows[0],17,std::numeric_limits<double>::quiet_NaN());
            if (name=="782_library_distance_secondary_nan") put<double>(s[147].rows[0],25,std::numeric_limits<double>::infinity());
            if (name=="782_library_distance_bindings") s[162].rows.erase(s[162].rows.begin()+1);
            if (name=="782_library_distance_duplicate") s[147].rows.push_back(s[147].rows[0]);
            if (name=="782_library_formula_fields") s[156].fields[2]=1;
            if (name=="782_library_formula_width") { s[156].payload=101; s[156].rows.clear(); }
            if (name=="782_library_formula_string") put<std::uint32_t>(s[156].rows[0],13,999);
            if (name=="782_library_formula_target") put<std::uint32_t>(s[156].rows[0],5,999);
            if (name=="782_library_formula_identity") put<std::uint32_t>(s[156].rows[0],1,999);
            if (name=="782_library_formula_duplicate") s[156].rows.push_back(s[156].rows[0]);
            if (name=="782_library_formula_refs") put<std::uint32_t>(s[162].rows.back(),13,999);
            if (name=="782_library_formula_output") s[162].rows.erase(s[162].rows.begin()+3);
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
                    check(c.parameterIds==std::vector<std::uint32_t>{60} && c.childObjectIds==std::vector<std::uint32_t>{5,15,60,70,80},"7.82 definition owner joins");
                    check(c.distanceParameterIds==std::vector<std::uint32_t>{70} && c.formulaBindingIds==std::vector<std::uint32_t>{80},"7.82 variable owner joins");
                    const auto& d=model.distanceParameters.at(70);
                    check(d.name=="D1" && d.label=="Length" && d.ownerId==50 && d.guid==model.identities.at(70).guid,"distance names/identity");
                    check(d.storedDistance==12.5 && d.secondaryStoredValue==30 && d.rawFields[3]==0xffffffff,"distance values/opaque fields");
                    check(d.propertyToken=="proPOSITION1" && d.planeToken=="PlaneXY" && d.boundObjectIds==std::vector<std::uint32_t>{5,50},"distance bindings");
                    check(d.formulaBindingIds==std::vector<std::uint32_t>{80},"distance formula join");
                    const auto& f=model.formulaBindings.at(80);
                    check(f.ownerId==50 && f.targetObjectId==70 && f.storedIndex==2 && f.expression=="WIDTH+2" && f.propertyName=="proVALUE","formula fields");
                    check(f.referencedObjectIds==std::vector<std::uint32_t>{60,70} && f.inputObjectIds==std::vector<std::uint32_t>{60},"formula input/output references");
                    check(model.formulaBindingIdsByTarget.at(70)==std::vector<std::uint32_t>{80},"formula target index");
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
            else
            {
                check(!ok,"malformed 7.82 accepted");
                check(!error.empty() && model.parts.empty() && model.distanceParameters.empty() && model.formulaBindings.empty() &&
                      model.formulaBindingIdsByTarget.empty(),"7.82 failure contract");
            }
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
