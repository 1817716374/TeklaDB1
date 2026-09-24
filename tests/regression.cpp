#include <tekla/Project.hpp>
#include <zlib.h>
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
        if (name == "valid_model" || name == "short_trailer")
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
            save(path,encode(onePart())); Bytes db2(64); str(db2,0,"Xsteel 9.52"); save(root/"model.db2",db2);
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
                Bytes b(size); str(b,0,"Xsteel 9.52"); const auto p=root/"model.db2"; save(p,gzip(b));
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
