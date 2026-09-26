#pragma once
#include <tekla/db1/Database.hpp>
#include <map>
#include <array>
#include <optional>

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
using DrawingPoint3 = std::array<double,3>;
struct DrawingCoordinateSystem
{
    DrawingPoint3 origin{};
    DrawingPoint3 axisX{};
    DrawingPoint3 axisY{};
    // Right-handed normal; stored axis endpoints are converted to vectors.
    DrawingPoint3 axisZ{};
};
struct DrawingViewVolume
{
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    double depthNegative = 0, depthPositive = 0;
};
struct DrawingView
{
    std::uint32_t recordId = 0;
    std::uint32_t contextId = 0;
    std::string modelGuid;
    DrawingCoordinateSystem viewCoordinates;
    DrawingCoordinateSystem displayCoordinates;
    DrawingViewVolume restriction;
    // Second stored range/depth block. Its interaction with automatic sizing
    // is not yet verified; never substitute it for an effective clipping box.
    DrawingViewVolume storedAttributeVolume;
    std::string propertySetName;
};
enum class DrawingSubjectKind { Unknown, SinglePart, Assembly };
struct DrawingSubject
{
    std::uint32_t recordId = 0;
    std::uint32_t typeCode = 0;
    DrawingSubjectKind kind = DrawingSubjectKind::Unknown;
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
    std::map<std::uint32_t,DrawingView> viewsByContext;
    std::optional<DrawingSubject> subject;
};
// Partial DG semantics: strings (including mark XML), properties, sheet size,
// project identity, subject, view coordinate bases/volumes and model references.
// Paper positioning, scale/shortening, dimensions and rendered primitives remain raw.
bool parseDrawing(const std::filesystem::path& path, Drawing& result, std::string& error,
                  const db1::RawDatabaseOptions& options = {});
}
