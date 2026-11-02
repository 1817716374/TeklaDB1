#include <tekla/db1/Catalogs.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <zlib.h>

namespace
{
using Bytes = std::vector<std::uint8_t>;

void check(bool condition, const std::string& message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class T>
void put(Bytes& bytes, std::size_t offset, T value)
{
    check(offset + sizeof(T) <= bytes.size(), "fixture offset outside buffer");
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

void text(Bytes& bytes, std::size_t offset, const std::string& value)
{
    check(offset + value.size() <= bytes.size(), "fixture text outside buffer");
    std::copy(value.begin(), value.end(), bytes.begin() + static_cast<std::ptrdiff_t>(offset));
}

void save(const std::filesystem::path& path, const Bytes& bytes)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(static_cast<bool>(stream), "could not write fixture");
}

Bytes gzip(const Bytes& bytes)
{
    z_stream stream{};
    check(deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, 8,
                       Z_DEFAULT_STRATEGY) == Z_OK, "could not initialize gzip fixture encoder");
    Bytes result(bytes.size() + 1024);
    stream.next_in = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(bytes.data()));
    stream.avail_in = static_cast<uInt>(bytes.size());
    stream.next_out = reinterpret_cast<Bytef*>(result.data());
    stream.avail_out = static_cast<uInt>(result.size());
    const auto status = deflate(&stream, Z_FINISH);
    const auto written = stream.total_out;
    deflateEnd(&stream);
    check(status == Z_STREAM_END && written <= result.size(), "could not finish gzip fixture encoding");
    result.resize(static_cast<std::size_t>(written));
    return result;
}

Bytes profileCatalog(const std::string& mode)
{
    const auto version = mode == "profile_v10" ? 10U : mode == "profile_bad_version" ? 11U : 9U;
    std::array<std::uint32_t, 10> widths{872, 152, 16, 24, 16, 52, 560, 296, 45, 1572};
    if (version == 10) widths[0] = 1068;
    if (mode == "profile_bad_width") ++widths[1];
    Bytes result(4, 0);
    put<std::uint32_t>(result, 0, version);
    for (std::size_t table = 0; table < widths.size(); ++table)
    {
        std::vector<Bytes> rows;
        if (table == 1)
        {
            rows.emplace_back(widths[table], 0);
            text(rows.back(), 0, "H400*400");
            put<std::uint32_t>(rows.back(), 64, 3);
            put<std::uint32_t>(rows.back(), 68, 42);
            if (mode == "profile_duplicate")
            {
                rows.emplace_back(widths[table], 0);
                text(rows.back(), 0, "H400*400");
                put<std::uint32_t>(rows.back(), 64, 3);
                put<std::uint32_t>(rows.back(), 68, 43);
            }
        }
        else if (table == 3)
        {
            rows.emplace_back(widths[table], 0);
            put<std::uint32_t>(rows.back(), 0, 77);
            put<std::uint32_t>(rows.back(), 4, 1);
            put<std::uint32_t>(rows.back(), 8, 9);
            put<double>(rows.back(), 16, 125.0);
        }
        else if (table == 5)
        {
            rows.emplace_back(widths[table], 0);
            text(rows.back(), 4, "H400*400");
            put<std::uint32_t>(rows.back(), 28, 1);
            put<float>(rows.back(), 32, 400.0f);
            put<float>(rows.back(), 36, 200.0f);
        }
        else if (table == 8)
        {
            rows.emplace_back(widths[table], 0);
            put<std::uint32_t>(rows.back(), 0, 77);
            text(rows.back(), 16, "H400*400");
        }
        Bytes header(8, 0);
        put<std::uint32_t>(header, 0, static_cast<std::uint32_t>(rows.size()));
        put<std::uint32_t>(header, 4, widths[table]);
        result.insert(result.end(), header.begin(), header.end());
        for (auto& row : rows)
        {
            result.push_back(mode == "profile_bad_tag" && table == 1 ? 12 : 4);
            result.insert(result.end(), row.begin(), row.end());
        }
    }
    return gzip(result);
}

Bytes boltCatalog()
{
    Bytes bytes(12 + 117, 0);
    put<std::uint32_t>(bytes, 0, 3);
    put<std::uint32_t>(bytes, 4, 1);
    put<std::uint32_t>(bytes, 8, 116);
    bytes[12] = 4;
    text(bytes, 13, "A325T 1/2 x 2");
    put<float>(bytes, 12 + 45, 0.5f);
    put<float>(bytes, 12 + 49, 2.0f);
    text(bytes, 12 + 53, "A325T");
    return bytes;
}

Bytes assemblyCatalog()
{
    Bytes bytes(12 + 356, 0);
    put<std::uint32_t>(bytes, 0, 3);
    put<std::uint32_t>(bytes, 4, 1);
    put<std::uint32_t>(bytes, 8, 355);
    bytes[12] = 4;
    text(bytes, 13, "A325T BOLT SET");
    text(bytes, 12 + 32, "A325T");
    return bytes;
}

int run(const std::filesystem::path& root, const std::string& mode)
{
    std::filesystem::create_directories(root);
    const auto path = root / (mode.find("profile") == 0 ? "profdb.bin" :
                              mode.find("assembly") == 0 ? "assdb.db" : "screwdb.db");
    std::string error;
    if (mode.rfind("profile_", 0) == 0)
    {
        if (mode.rfind("profile_lis_", 0) == 0)
        {
            const std::string source = mode == "profile_lis_valid"
                ? "PROFILE DATABASE EXPORT VERSION = 2\n\n"
                  "PROFILE_NAME = \"KKR30*3\";\n{\n"
                  "TYPE = 8; SUB_TYPE = 8002; COORDINATE = 0.000;\n{\n"
                  "\"HEIGHT\" 3.000000000E+001\n\"UNKNOWN_TEST_PROPERTY\" -2.5e-3\n}\n}\n"
                  "PROFILE_NAME = \"KKR40*4\";\n{\n"
                  "TYPE = 8; SUB_TYPE = 8002; COORDINATE = 1.25;\n{\n"
                  "\"WIDTH\" 4.000000000E+001\n}\n}\n"
                : mode == "profile_lis_duplicate_property"
                    ? "PROFILE DATABASE EXPORT VERSION = 2\nPROFILE_NAME = \"X\";\n{\nTYPE = 1; SUB_TYPE = 2; COORDINATE = 0;\n{\n\"HEIGHT\" 1\n\"HEIGHT\" 2\n}\n}\n"
                    : mode == "profile_lis_bad_version"
                        ? "PROFILE DATABASE EXPORT VERSION = 3\n"
                        : "PROFILE DATABASE EXPORT VERSION = 2\nPROFILE_NAME = \"X\";\n{\nTYPE = 1; SUB_TYPE = nope; COORDINATE = 0;\n{\n}\n}\n";
            save(path, Bytes(source.begin(), source.end()));
            tekla::db1::ProfileCatalogLis catalog;
            catalog.entries.push_back({});
            error = "stale diagnostic";
            const bool ok = tekla::db1::parseProfileCatalogLis(path, catalog, error);
            check(ok == (mode == "profile_lis_valid"), "unexpected profile .lis parse result");
            if (!ok)
                check(!error.empty() && catalog.entries.empty() && catalog.rawText.empty(),
                      "invalid profile .lis leaked partial output");
            else
            {
                check(error.empty() && catalog.version == 2 && catalog.entries.size() == 2 &&
                      catalog.ambiguousProfileNames.empty() && catalog.rawText == source,
                      "profile .lis header, entry count, ambiguity, or raw source changed");
                const auto& first = catalog.entries[0];
                const auto& second = catalog.entries[1];
                check(first.name == "KKR30*3" && first.type == 8 && first.subType == 8002 &&
                      first.storedCoordinate == 0.0 && first.properties.size() == 2 &&
                      first.properties.at("HEIGHT") == 30.0 &&
                      first.properties.at("UNKNOWN_TEST_PROPERTY") == -0.0025 &&
                      second.name == "KKR40*4" && second.storedCoordinate == 1.25 &&
                      second.properties.at("WIDTH") == 40.0 &&
                      first.rawRecord.find("UNKNOWN_TEST_PROPERTY") != std::string::npos,
                      "profile .lis numeric fields or unknown property preservation changed");
            }
            return 0;
        }
        save(path, profileCatalog(mode));
        tekla::db1::ProfileCatalog catalog;
        catalog.profiles.emplace("stale", tekla::db1::Profile{});
        error = "stale diagnostic";
        const bool ok = tekla::db1::parseProfileCatalog(path, catalog, error);
        const bool shouldPass = mode == "profile_v9" || mode == "profile_v10" || mode == "profile_duplicate";
        check(ok == shouldPass, "unexpected profdb.bin parse result");
        if (!shouldPass)
            check(!error.empty() && catalog.profiles.empty(), "invalid profdb.bin leaked partial output");
        else
        {
            const auto version = mode == "profile_v10" ? 10U : 9U;
            check(error.empty() && catalog.version == version && catalog.profiles.size() == 1,
                  "profdb.bin version or profile count changed");
            check(catalog.ambiguousProfileNames == (mode == "profile_duplicate"
                      ? std::vector<std::string>{"H400*400"} : std::vector<std::string>{}),
                  "profdb.bin duplicate definition names were not reported");
            const auto& profile = catalog.profiles.at("H400*400");
            const auto expectedShapeId = mode == "profile_duplicate" ? 43U : 42U;
            check(profile.id == 77 && profile.type == 3 && profile.shapeId == expectedShapeId &&
                  profile.parameters.size() == 1 && profile.parameters.at(9) == 125.0 &&
                  profile.fixedContours.size() == 1 && profile.fixedContours[0].size() == 1 &&
                  profile.fixedContours[0][0].value == std::array<double, 2>{400.0, 200.0},
                  "profdb.bin profile links, parameter, or contour changed");
        }
    }
    else if (mode == "bolt_standard")
    {
        const auto bytes = boltCatalog();
        save(path, bytes);
        tekla::db1::BoltCatalog catalog;
        check(tekla::db1::parseBoltCatalog(path, catalog, error), error);
        check(catalog.version == 3 && catalog.table.payloadSize == 116 && catalog.bolts.size() == 1,
              "screwdb header or record count");
        const auto& bolt = catalog.bolts.front();
        check(bolt.name == "A325T 1/2 x 2" && bolt.standard == "A325T" &&
              bolt.diameter == 0.5f && bolt.length == 2.0f && bolt.rawPayload.size() == 116,
              "screwdb standard field or preserved payload");
        check(std::string(bolt.rawPayload.begin() + 52, bolt.rawPayload.begin() + 57) == "A325T",
              "screwdb exposed standard differs from raw payload");
    }
    else if (mode == "bolt_truncated")
    {
        auto bytes = boltCatalog();
        bytes.pop_back();
        save(path, bytes);
        tekla::db1::BoltCatalog catalog;
        catalog.bolts.emplace_back();
        check(!tekla::db1::parseBoltCatalog(path, catalog, error) && catalog.bolts.empty() && !error.empty(),
              "truncated screwdb accepted or leaked partial output");
    }
    else if (mode == "bolt_schema")
    {
        auto bytes = boltCatalog();
        put<std::uint32_t>(bytes, 8, 115);
        save(path, bytes);
        tekla::db1::BoltCatalog catalog;
        check(!tekla::db1::parseBoltCatalog(path, catalog, error) && catalog.bolts.empty() && !error.empty(),
              "unsupported screwdb record width accepted");
    }
    else if (mode == "assembly_components")
    {
        save(path, assemblyCatalog());
        tekla::db1::BoltAssemblyCatalog catalog;
        check(tekla::db1::parseBoltAssemblyCatalog(path, catalog, error), error);
        check(catalog.version == 3 && catalog.table.payloadSize == 355 && catalog.assemblies.size() == 1,
              "assdb header or record count");
        const auto& assembly = catalog.assemblies.front();
        check(assembly.name == "A325T BOLT SET" && assembly.componentNames[0] == "A325T" &&
              assembly.rawPayload.size() == 355,
              "assdb component slot or preserved payload");
    }
    else if (mode == "assembly_truncated")
    {
        auto bytes = assemblyCatalog();
        bytes.resize(bytes.size() - 2);
        save(path, bytes);
        tekla::db1::BoltAssemblyCatalog catalog;
        catalog.assemblies.emplace_back();
        check(!tekla::db1::parseBoltAssemblyCatalog(path, catalog, error) &&
              catalog.assemblies.empty() && !error.empty(),
              "truncated assdb accepted or leaked partial output");
    }
    else if (mode == "profitab_valid" || mode == "profitab_invalid_integer")
    {
        const std::string source = mode == "profitab_valid"
            ? "PNL_A ! USER ! +4 ! ! 1 ! 3 ! G3.Rect ! h*b\n"
              "PNL_B ! USER ! ! 2 ! 0 ! 4 ! ! h*b\n"
            : "PNL_A ! USER ! 4 ! ! 1 ! 3 ! G3.Rect ! h*b\n"
              "PNL_BAD ! USER ! nope ! 1 ! 1 ! 2 ! G3.Bad ! x\n"
              "PNL_OVERFLOW ! USER ! 999999999999999999999999 ! 1 ! 1 ! 2 ! G3.Bad ! x\n";
        save(path, Bytes(source.begin(), source.end()));
        tekla::db1::ProfitabCatalog catalog;
        check(tekla::db1::parseProfitabCatalog(path, catalog, error), error);
        if (mode == "profitab_valid")
        {
            check(catalog.rules.size() == 2 && catalog.diagnostics.empty(),
                  "valid profitab rows were rejected");
            check(catalog.rules[0].sortOrder == 4 && catalog.rules[0].unit == 0 &&
                  catalog.rules[0].minimumNumbers == 1 && catalog.rules[0].maximumNumbers == 3 &&
                  catalog.rules[0].generatorLibraryId == "G3" &&
                  catalog.rules[0].generatorSectionName == "Rect",
                  "profitab numeric or generator fields changed");
            check(catalog.rules[1].sortOrder == 0 && catalog.rules[1].unit == 2,
                  "blank profitab integer field was not preserved as zero");
        }
        else
        {
            check(catalog.rules.size() == 1 && catalog.diagnostics.size() == 2 &&
                  catalog.diagnostics[0].find("line 2") != std::string::npos &&
                  catalog.diagnostics[1].find("line 3") != std::string::npos,
                  "malformed profitab integer rows were silently accepted");
        }
    }
    else throw std::runtime_error("unknown catalog test mode: " + mode);
    return 0;
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("usage: catalogs <fixture-root> <mode>");
        return run(std::filesystem::u8path(argv[1]) / argv[2], argv[2]);
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
