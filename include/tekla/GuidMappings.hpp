#pragma once
#include <tekla/db1/Model.hpp>

namespace tekla
{
struct GuidMapping
{
    std::size_t lineNumber = 0;
    std::string sourceGuid; // canonical lowercase UUID, without optional ID prefix
    std::string targetGuid;
};
struct GuidMappingBatch
{
    std::size_t lineNumber = 0;
    std::string modelName; // literal header bytes; never opened as a path
    std::vector<GuidMapping> mappings;
};
struct GuidMappingFile
{
    std::filesystem::path sourcePath;
    std::string rawText; // complete decompressed source, including original line endings
    std::vector<GuidMappingBatch> batches;
    std::vector<std::string> diagnostics;
};
struct GuidMappingTarget
{
    std::size_t batchIndex = 0;
    std::size_t mappingIndex = 0;
    std::uint32_t modelObjectId = 0;
};
struct GuidMappedObjectPair
{
    std::size_t batchIndex = 0;
    std::size_t mappingIndex = 0;
    std::uint32_t sourceObjectId = 0;
    std::uint32_t targetObjectId = 0;
};
// Plain or gzip text. No history selection or transitive GUID replacement.
bool parseGuidMappingFile(const std::filesystem::path& path, GuidMappingFile& result,
                          std::string& error, std::size_t maxDecodedBytes = 64 * 1024 * 1024);
// Exact canonical target GUID matches only; ambiguous model GUIDs are excluded.
// These are evidence links, not permission to apply a historical remapping.
std::vector<GuidMappingTarget> matchGuidMappingTargets(const GuidMappingFile& mappings,
                                                      const db1::Model& model);
// Explicitly supplied snapshots; both GUID endpoints must resolve uniquely.
std::vector<GuidMappedObjectPair> matchGuidMappingObjects(const GuidMappingFile& mappings,
    const db1::Model& sourceModel, const db1::Model& targetModel);
}
