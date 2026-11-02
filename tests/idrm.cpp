#include <tekla/IdRangeMapper.hpp>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void put32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value)
{
    check(offset + 4 <= bytes.size(), "fixture bounds");
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}
std::vector<std::uint8_t> fixture(std::uint32_t count = 1)
{
    std::vector<std::uint8_t> bytes(25 + static_cast<std::size_t>(count) * 28, 0);
    const std::string signature = "IdRangeMapper";
    std::copy(signature.begin(), signature.end(), bytes.begin());
    put32(bytes, 13, count == 2 ? 200U : 100U);
    put32(bytes, 17, 0xdbcec0e1U);
    put32(bytes, 21, count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto offset = 25U + static_cast<std::size_t>(i) * 28U;
        put32(bytes, offset, i == 0 ? 1U : 100U);
        for (std::size_t j = 0; j < 16; ++j) bytes[offset + 4 + j] = static_cast<std::uint8_t>(j);
        put32(bytes, offset + 20, i == 0 ? 0U : 99U);
        put32(bytes, offset + 24, i == 0 ? 99U : 100U);
    }
    return bytes;
}
void save(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
{
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    check(bool(output), "cannot write IDRM test fixture");
}
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 3) throw std::runtime_error("usage: idrm_test <directory> <case>");
        const std::filesystem::path directory = argv[1];
        std::filesystem::create_directories(directory);
        const std::string mode = argv[2];
        auto bytes = fixture(mode == "idrm_two_records" ? 2U : 1U);
        if (mode == "idrm_truncated") bytes.pop_back();
        if (mode == "idrm_bad_signature") bytes[0] = 'X';
        if (mode == "idrm_bad_marker") put32(bytes, 17, 0);
        if (mode == "idrm_count_overflow") put32(bytes, 21, 0xffffffffU);
        if (mode == "idrm_trailing") bytes.push_back(0xaa);
        const auto path = directory / "fixture.idrm";
        save(path, bytes);
        tekla::IdRangeMapperFile parsed;
        std::string error;
        const bool ok = tekla::parseIdRangeMapperFile(path, parsed, error,
            mode == "idrm_limit" ? bytes.size() - 1 : 1024);
        const bool shouldPass = mode == "idrm_valid" || mode == "idrm_two_records" || mode == "idrm_trailing";
        check(ok == shouldPass, "unexpected parse result");
        if (!shouldPass)
        {
            check(!error.empty() && parsed.rawBytes.empty(), "failed parse leaked output or omitted diagnostic");
        }
        else
        {
            check(parsed.rawBytes == bytes, "raw bytes changed");
            check(parsed.records.size() == (mode == "idrm_two_records" ? 2U : 1U), "record count mismatch");
            check(parsed.trailingBytes.size() == (mode == "idrm_trailing" ? 1U : 0U), "trailing bytes mismatch");
            check(parsed.staticIdBoundary == (mode == "idrm_two_records" ? 200U : 100U), "static ID boundary mismatch");
            check(parsed.staticRangesMatchBoundary, "synthetic static ranges did not match their boundary");
            check(parsed.records.front().staticFirstId == 1 && parsed.records.front().idCount == 99 &&
                  parsed.records.front().namespaceFirstId == 0 && parsed.records.front().namespaceGuidBytes[15] == 15,
                  "typed IDRM range fields mismatch");
        }
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
