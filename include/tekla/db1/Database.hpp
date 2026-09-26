#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tekla::db1
{
enum class DatabaseLayout
{
    Unknown,
    ModernSections,
    LegacyTables,
    Opaque,
    SequentialTables
};

enum class DatabaseKind
{
    Unknown,
    Model,
    ComponentLibrary,
    Environment,
    Numbering,
    Drawing
};

struct RawRecord
{
    std::uint8_t allocationTag = 0;
    std::vector<std::uint8_t> payload;
    std::vector<std::uint8_t> allocatorMetadata;
    std::size_t fileOffset = 0;
};

struct RawTable
{
    std::uint32_t ordinal = 0;
    std::uint32_t payloadSize = 0;
    std::vector<std::uint32_t> fieldDescriptors;
    std::vector<RawRecord> records;
    std::vector<std::uint8_t> trailer;
    // Populated when a section does not follow the regular fixed-row schema.
    // This keeps vendor/private sections available without guessing their layout.
    std::vector<std::uint8_t> opaqueSection;
    std::size_t fileOffset = 0;
    bool schemaValid = false;
};

struct RawDatabase
{
    std::filesystem::path sourcePath;
    DatabaseLayout layout = DatabaseLayout::Unknown;
    DatabaseKind kind = DatabaseKind::Unknown;
    std::string storageVersion;
    // DBV's u32 after its magic is the container-name length, NOT a version.
    std::string containerName;
    std::string databaseGuid;
    std::vector<std::uint8_t> preamble;
    std::vector<RawTable> tables;
    std::vector<std::uint8_t> decompressedFileImage;
    std::vector<std::string> diagnostics;
};

struct RawDatabaseOptions
{
    // Retaining the decompressed image permits byte-for-byte research and
    // reserialization, but can roughly double peak memory for a large model.
    bool retainDecompressedFileImage = false;
    // Applies to plain and GZIP input. A larger research budget can be explicit.
    std::size_t maxDecodedBytes = 1024ULL * 1024 * 1024;
};

bool parseRawDatabase(const std::filesystem::path& path, RawDatabase& database,
                      std::string& error, const RawDatabaseOptions& options = {});
}
