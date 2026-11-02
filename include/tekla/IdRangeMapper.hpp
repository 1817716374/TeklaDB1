#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tekla
{
// One observed 28-byte IDRM record. Named fields describe verified static-ID
// ranges only; runtime-ID conversion is not yet verified.
struct IdRangeMapperRecord
{
    std::array<std::uint8_t, 28> raw{};
    // Verified on public 7.82, 8.44 and 8.95 files. The identifier range is
    // represented as [staticFirstId, staticFirstId + idCount); these fields do
    // not convert a drawing's static ID to a database runtime ID.
    std::uint32_t staticFirstId = 0;
    std::array<std::uint8_t, 16> namespaceGuidBytes{};
    std::uint32_t namespaceFirstId = 0;
    std::uint32_t idCount = 0;
};

struct IdRangeMapperFile
{
    std::filesystem::path sourcePath;
    std::vector<std::uint8_t> rawBytes; // decoded bytes, including unclassified trailing data
    std::uint32_t headerValueAt13 = 0;
    std::uint32_t marker = 0;
    std::vector<IdRangeMapperRecord> records;
    std::vector<std::uint8_t> trailingBytes;
    std::vector<std::string> diagnostics;
    // On all currently pinned native samples this is the exclusive upper
    // bound of a contiguous sequence of static-ID ranges.
    std::uint32_t staticIdBoundary = 0;
    bool staticRangesMatchBoundary = false;
};

// Reads the observed IdRangeMapper container, including gzip-wrapped files.
// It does not convert IDs or associate ranges with DB1/xslib objects.
bool parseIdRangeMapperFile(const std::filesystem::path& path, IdRangeMapperFile& result,
                            std::string& error, std::size_t maxDecodedBytes = 64 * 1024 * 1024);
}
