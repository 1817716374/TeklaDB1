#pragma once
#include <tekla/db1/Database.hpp>
#include <map>

namespace tekla
{
struct DrawingString
{
    std::uint32_t id = 0;
    std::string text;
};
struct DrawingProperty
{
    std::uint32_t id = 0;
    std::string name;
    std::string stringValue;
    double numericValue = 0;
    bool isString = true;
};
struct DrawingPropertyLink
{
    std::uint32_t id = 0;
    std::uint32_t propertyId = 0;
    std::uint32_t ownerId = 0;
};
struct DrawingSheet
{
    std::uint32_t id = 0;
    double width = 0;
    double height = 0;
};
struct DrawingModelReference
{
    std::uint32_t recordId = 0;
    std::uint32_t drawingContextId = 0;
    std::string modelGuid;
};
struct Drawing
{
    db1::RawDatabase raw;
    // Stored property values, not inferred from the containing folder/filename.
    std::string projectGuid;
    std::string storedFileName;
    // Only root strings are assembled; continuation chunks remain in raw.
    std::map<std::uint32_t, DrawingString> strings;
    std::map<std::uint32_t, DrawingProperty> properties;
    std::vector<DrawingPropertyLink> propertyLinks;
    std::vector<DrawingSheet> sheets;
    // DG type 322; these GUIDs have been verified against DB1 identities.
    // They include multiple model object types, not exclusively parts.
    std::vector<DrawingModelReference> modelReferences;
    std::vector<std::string> diagnostics;
};
// Partial DG semantics: strings (including mark XML), properties, sheet size,
// project identity and type-322 model references. Views, dimensions, and rendered
// primitives remain raw.
bool parseDrawing(const std::filesystem::path& path, Drawing& result, std::string& error,
                  const db1::RawDatabaseOptions& options = {});
}
