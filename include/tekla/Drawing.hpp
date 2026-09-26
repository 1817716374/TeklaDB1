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
    bool complete = true;
    std::uint32_t missingContinuationId = 0;
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
    // 7.82 stores an unscoped model object ID instead of a GUID. This alone
    // is insufficient to choose a model database for an automatic join.
    std::uint32_t modelObjectId = 0;
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
    // Preserves conflicting legacy stored names without choosing precedence.
    std::vector<std::string> storedPropertySetNames;
};
enum class DrawingSubjectKind { Unknown, SinglePart, Assembly, GeneralArrangement };
struct DrawingSubject
{
    std::uint32_t recordId = 0;
    std::uint32_t typeCode = 0;
    DrawingSubjectKind kind = DrawingSubjectKind::Unknown;
    std::string modelGuid;
    std::uint32_t modelObjectId = 0;
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
    // DG type 322. 9.54 carries GUIDs; 7.82 carries unscoped numeric IDs.
    // 7.30 has no type 322; its other model reference layouts remain in raw.
    // Referenced entities are not necessarily ordinary parts.
    std::vector<DrawingModelReference> modelReferences;
    std::vector<std::string> diagnostics;
    std::map<std::uint32_t,DrawingView> viewsByContext;
    std::optional<DrawingSubject> subject;
    // Complete decoded view set. 7.82 contexts can repeat; viewsByContext only
    // contains unambiguous contexts and must not be used to count all views.
    std::map<std::uint32_t,DrawingView> viewsByRecordId;
    std::vector<std::uint32_t> unhandledViewRecordIds;
};
// Partial DG 7.30/7.82/9.54 semantics: strings (including mark XML), properties, sheet size,
// project identity, subject, view coordinate bases/volumes and model references.
// Paper positioning, scale/shortening, dimensions and rendered primitives remain raw.
// 7.30 exposes numeric subjects; other model references and type-2 subjects are unverified.
bool parseDrawing(const std::filesystem::path& path, Drawing& result, std::string& error,
                  const db1::RawDatabaseOptions& options = {});
}
