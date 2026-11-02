#include <tekla/db1/Catalogs.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void check(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    check(offset + 4 <= bytes.size(), "pgdb fixture write out of bounds");
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

void putDouble(std::vector<std::uint8_t>& bytes, std::size_t offset, double value)
{
    check(offset + sizeof(value) <= bytes.size(), "pgdb fixture double out of bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<std::uint8_t> payload(std::size_t size)
{
    return std::vector<std::uint8_t>(size, 0);
}

void appendTable(std::vector<std::uint8_t>& file, std::uint32_t rowSize,
                 const std::vector<std::vector<std::uint8_t>>& rows)
{
    const auto header = file.size();
    file.resize(header + 8, 0);
    put32(file, header, static_cast<std::uint32_t>(rows.size()));
    put32(file, header + 4, rowSize);
    for (const auto& row : rows)
    {
        check(row.size() == rowSize, "pgdb fixture row size");
        file.push_back(4);
        file.insert(file.end(), row.begin(), row.end());
    }
}

std::vector<std::uint8_t> fixture(const std::string& mode)
{
    std::vector<std::uint8_t> file;
    auto profile = payload(100);
    std::memcpy(profile.data(), "L", 1);
    put32(profile, 12, 100);
    put32(profile, 16, 500);
    put32(profile, 20, 7);
    putDouble(profile, 24, mode == "pgdb_solved_nan" ? std::numeric_limits<double>::quiet_NaN() : 10.5);
    putDouble(profile, 32, 20.5);
    putDouble(profile, 40, 30.5);
    putDouble(profile, 48, 40.5);

    auto text1 = payload(36);
    put32(text1, 0, mode == "pgdb_zero_string" ? 0 : 100);
    std::memcpy(text1.data() + 4, "150", 3);
    put32(text1, 32, 200);
    auto text2 = payload(36);
    put32(text2, 0, mode == "pgdb_duplicate_string" ? 100 : 200);
    std::memcpy(text2.data() + 4, "*6", 2);

    auto block = payload(52);
    put32(block, 0, mode == "pgdb_zero_block" ? 0 : 500);
    put32(block, 8, 701);
    put32(block, 48, mode == "pgdb_cycle" ? 500 : 0);

    auto point = payload(24);
    put32(point, 0, mode == "pgdb_zero_point" ? 0 : 701);
    put32(point, 4, mode == "pgdb_missing_chamfer" ? 999 : 901);
    putDouble(point, 8, mode == "pgdb_point_nan" ? std::numeric_limits<double>::infinity() : 3.5);
    putDouble(point, 16, 4.5);

    auto chamfer = payload(44);
    put32(chamfer, 0, mode == "pgdb_zero_chamfer" ? 0 : 901);
    put32(chamfer, 4, 2);
    putDouble(chamfer, 8, mode == "pgdb_chamfer_nan" ? std::numeric_limits<double>::quiet_NaN() :
                      mode == "pgdb_chamfer_range" ? std::numeric_limits<double>::max() : 0.125);
    putDouble(chamfer, 16, 0.25);
    putDouble(chamfer, 24, 1.125);
    putDouble(chamfer, 32, 2.25);
    put32(chamfer, 40, 0xA5A50007);

    appendTable(file, 100, mode == "pgdb_duplicate_profile" ?
                std::vector<std::vector<std::uint8_t>>{profile, profile} :
                std::vector<std::vector<std::uint8_t>>{profile});
    appendTable(file, 36, {text1, text2});
    appendTable(file, 52, mode == "pgdb_duplicate_block" ?
                std::vector<std::vector<std::uint8_t>>{block, block} :
                std::vector<std::vector<std::uint8_t>>{block});
    appendTable(file, 24, mode == "pgdb_duplicate_point" ?
                std::vector<std::vector<std::uint8_t>>{point, point} :
                std::vector<std::vector<std::uint8_t>>{point});
    appendTable(file, 44, mode == "pgdb_duplicate_chamfer" ?
                std::vector<std::vector<std::uint8_t>>{chamfer, chamfer} :
                std::vector<std::vector<std::uint8_t>>{chamfer});
    if (mode == "pgdb_truncated") file.pop_back();
    if (mode == "pgdb_trailing") file.push_back(0xEE);
    return file;
}

void save(const std::filesystem::path& path, const std::vector<std::uint8_t>& file)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
    check(bool(output), "cannot write pgdb fixture");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("usage: pgdb_test <directory> <case>");
        const std::string mode = argv[2];
        const auto path = std::filesystem::u8path(argv[1]) / mode / "pgdb.bin";
        const auto bytes = fixture(mode);
        save(path, bytes);

        tekla::db1::ProfileGeometryCatalog catalog;
        std::string error;
        const bool ok = tekla::db1::parseProfileGeometryCatalog(path, catalog, error);
        const bool shouldPass = mode == "pgdb_valid" || mode == "pgdb_cycle" || mode == "pgdb_missing_chamfer";
        check(ok == shouldPass, "unexpected pgdb parse result");
        if (!shouldPass)
        {
            check(!error.empty() && catalog.profiles.empty() && catalog.rawTables[0].empty(),
                  "failed pgdb parse retained partial output or omitted diagnostic");
            return 0;
        }

        check(error.empty(), "successful pgdb parse returned an error");
        check(catalog.profiles.size() == 1 && catalog.profiles.count("L150*6"),
              "profile string chain was not resolved");
        const auto& profile = catalog.profiles.at("L150*6");
        check(profile.flags == 7 && profile.solvedValues == std::array<double, 4>{10.5, 20.5, 30.5, 40.5},
              "saved solved profile values changed");
        check(profile.contour.size() == 1 && profile.contour.front().value == std::array<double, 2>{3.5, 4.5},
              "point block/point association changed");
        if (mode == "pgdb_valid")
        {
            const auto& point = profile.contour.front();
            check(point.chamferType == 2 && point.chamferX == 0.125f && point.chamferY == 0.25f &&
                  point.chamferDz1 == 1.125 && point.chamferDz2 == 2.25 &&
                  point.chamferFlags == 0xA5A50007,
                  "Sketch Solver chamfer fields were not fully exposed");
            check(catalog.chamfers.at(901).dz1 == 1.125 && catalog.chamfers.at(901).dz2 == 2.25 &&
                  catalog.rawTables[4].front().payload.size() == 44,
                  "raw chamfer evidence was not retained");
            check(catalog.diagnostics.empty(), "valid pgdb fixture produced diagnostics");
        }
        else
        {
            const auto contains = [&](const std::string& needle) {
                for (const auto& diagnostic : catalog.diagnostics)
                    if (diagnostic.find(needle) != std::string::npos) return true;
                return false;
            };
            check(contains(mode == "pgdb_cycle" ? "cycle in pgdb.bin point block" : "missing chamfer"),
                  "pgdb partial-link diagnostic is missing");
        }
        return 0;
    }
    catch (const std::exception& exception)
    {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
