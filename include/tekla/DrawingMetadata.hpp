#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace tekla
{
// DrawingVersionMetadata XML accompanying a DG. This is a saved version's
// metadata, not proof of the active/current drawing or its lifecycle.
struct DrawingVersionMetadata
{
    // Canonical lowercase GUIDs; absent/empty values stay empty. Neither GUID
    // is inferred from a filename. rawXml preserves spelling and field presence.
    std::string drawingGuid, mainObjectGuid;
    std::string author, mark, name, title1, title2, title3;
    std::optional<double> width, height;
    std::optional<std::uint32_t> drawingType;
    // Exact stored integers for IssueDate, NextDate, IssueId, Flag, Revision,
    // PlotDate, ModifyDate, CreateDate, PartsNo, UpToDate and Lock. Presence is
    // retained; no timestamp units, bit meanings, currentness or precedence
    // are inferred from these values. Zero is not replaced with a missing value.
    std::map<std::string,std::int64_t> storedIntegers;
    std::string rawXml;
    std::vector<std::string> diagnostics;
};
bool parseDrawingVersionMetadata(const std::filesystem::path& path,
    DrawingVersionMetadata& result, std::string& error,
    std::size_t maxDecodedBytes = 16 * 1024 * 1024);
}
