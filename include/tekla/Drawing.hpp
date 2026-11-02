#pragma once
#include <tekla/db1/Database.hpp>
#include <cstddef>
#include <map>
#include <array>
#include <optional>

namespace tekla
{
// Best-effort structure for a Tekla drawing mark template. The original XML
// remains available in DrawingString::text; text and attributes here are XML
// decoded, and template expressions are not evaluated.
struct DrawingMarkNode
{
    std::string name;
    std::map<std::string,std::string> attributes;
    std::string text;
    std::vector<DrawingMarkNode> children;
};
struct DrawingMarkTemplate
{
    std::vector<DrawingMarkNode> children;
};
struct DrawingString
{
    std::uint32_t id = 0;
    std::string text;
    bool complete = true;
    std::uint32_t missingContinuationId = 0;
    std::optional<DrawingMarkTemplate> markTemplate;
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
// A type-304 drawing index row. The first two words identify a table type
// and record ID; the third word is preserved without assigning a universal
// owner/context meaning. The remaining bytes are currently unclassified.
struct DrawingRecordAssociationIndexEntry
{
    std::uint32_t recordTypeCode = 0;
    std::uint32_t recordId = 0;
    std::uint32_t relatedId = 0;
    std::array<std::uint8_t,12> unclassifiedBytes{};
    // Physical rows with matching type@0 and ID@4 in the target table. Empty
    // means unresolved; multiple offsets preserve duplicate-ID ambiguity.
    std::vector<std::uint64_t> candidateTargetRecordOffsets;
};
// DG type-311 records expose a stable pair of stored XY coordinates at
// payload offsets 24..55 in the pinned drawing schemas. The line-segment
// interpretation is corroborated by type-281 links and dimension-anchor
// intersections in the 9.54 sample; style, context, and remaining fields stay
// available only through Drawing::raw.
struct DrawingLineSegment
{
    std::uint32_t recordId = 0;
    std::array<double,2> startPoint{};
    std::array<double,2> endPoint{};
    std::size_t rawRecordOffset = 0;
};
// Observed DG 9.54 type-281 rows that point to type-311 lines. The link-row
// ID and target ID are exposed as stored; the relationship's owner/context
// meaning is unknown. Other payload words remain unclassified.
struct DrawingType281LineReference
{
    std::uint32_t referenceRecordId = 0;
    std::uint32_t lineRecordId = 0;
    std::array<std::uint32_t,5> unclassifiedWords{};
    std::size_t rawRecordOffset = 0;
    // Physical type-311 rows with this ID. Empty preserves an unresolved
    // reference; multiple offsets preserve duplicate-ID ambiguity.
    std::vector<std::uint64_t> candidateLineRecordOffsets;
};
// DG 7.30/7.82 type-309 rows store a drawing record ID at payload@0 and a
// numeric DB1 part ID at payload@12. The other words and drawing context are
// not yet interpreted. These IDs are unscoped and require explicit project
// scope before they can be linked to a model.
struct DrawingLegacyModelReference
{
    std::uint32_t recordId = 0;
    std::uint32_t modelObjectId = 0;
    std::size_t rawRecordOffset = 0;
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
    // 7.82, 8.95 and 9.08 store numeric IDs instead of GUIDs. Automatic pairing
    // is verified for 9.08 with matching project GUID AND storage version.
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
    // Present only for DG 9.54, whose type-260 offsets have been inspected.
    // The scalar is the stored value; its direction in paper transforms is
    // inconsistent across observed drawings, so no coordinate conversion is
    // inferred from it.
    std::optional<std::array<double,2>> storedPaperPosition;
    std::optional<double> storedScaleValue;
    std::string propertySetName;
    // Preserves conflicting legacy stored names without choosing precedence.
    std::vector<std::string> storedPropertySetNames;
    // False for DG 9.50, where only the stable ID/context prefix is decoded.
    // Appended to preserve positional aggregate initialization of prior fields.
    bool geometryDecoded = false;
};
enum class DrawingSubjectKind { Unknown, SinglePart, Assembly, GeneralArrangement };
struct DrawingStraightDimensionSet
{
    std::uint32_t recordId = 0;
    std::uint32_t contextId = 0;
    // Source record order, not an inferred spatial/chain order. Includes IDs
    // retained in unhandledDimensionRecordIds for unknown subtypes.
    std::vector<std::uint32_t> dimensionIds;
};
struct DrawingStraightDimensionSetOccurrence
{
    std::uint32_t recordId = 0;
    std::uint32_t contextId = 0;
    // Absolute offset of this physical type-257 record in the decoded image.
    std::size_t rawRecordOffset = 0;
};
struct DrawingStraightDimension
{
    std::uint32_t recordId = 0;
    std::uint32_t contextId = 0;
    std::uint32_t dimensionSetId = 0;
    std::uint32_t subtypeCode = 0;
    DrawingPoint3 startPoint{}, endPoint{}, upDirection{};
    // Stored distance from startPoint to the dimension line. No view scaling
    // is applied here; see the drawing dimension evidence for unit boundaries.
    double distance = 0;
    // Absolute XY endpoint difference perpendicular to a unit XY upDirection.
    // Not formatted/rounded/overridden text or a complete rendered dimension.
    std::optional<double> projectedLength;
};
struct DrawingStraightDimensionOccurrence
{
    DrawingStraightDimension dimension;
    // Absolute offset of this physical type-256 record in the decoded image.
    // This distinguishes legacy records whose recordId is repeated.
    std::size_t rawRecordOffset = 0;
};
struct DrawingSubject
{
    // DG 9.50 decodes only observed type codes 1/2/7; type 7 remains Unknown.
    // 9.50 GUIDs for types 1/2 are raw 16-byte identity values. Their presence
    // does not by itself establish project scope or a DB1 object association.
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
    // DG type 322. 9.54 carries GUIDs; 7.82/8.95/9.08 carry numeric IDs.
    // Automatic numeric pairing is currently verified only for scoped 9.08.
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
    // DG 7.30 subtype 0, DG 7.82 subtypes 0/1, and DG 8.44/9.54 subtype 1.
    // Duplicate legacy identities and unsupported subtypes remain raw.
    // Legacy context IDs need not resolve to a unique decoded view.
    // ID-keyed maps contain only unique, unambiguous records.
    std::map<std::uint32_t,DrawingStraightDimensionSet> straightDimensionSets;
    std::map<std::uint32_t,DrawingStraightDimension> straightDimensions;
    // Repeated dimension-set IDs are listed here for backward compatibility.
    // In DG 7.82 byte-identical duplicate rows are coalesced in the ID map and
    // all physical rows are exposed below; conflicting rows remain unmapped.
    std::vector<std::uint32_t> unhandledDimensionSetRecordIds;
    std::vector<std::uint32_t> unhandledDimensionRecordIds;
    // DG 7.82 known-layout dimensions with repeated IDs. Every physical row
    // is decoded here; the ID-keyed map remains empty for those ambiguous IDs.
    std::vector<DrawingStraightDimensionOccurrence> duplicateStraightDimensionRecords;
    // DG 7.82 repeated type-257 rows with known ID/context fields. Byte-identical
    // groups have one canonical map entry; conflicting groups remain unmapped.
    std::vector<DrawingStraightDimensionSetOccurrence> duplicateStraightDimensionSetRecords;
    // Appended to preserve positional aggregate initialization of prior fields.
    std::vector<DrawingRecordAssociationIndexEntry> recordAssociationIndex;
    // Verified type-309 numeric part references in DG 7.30/7.82. No project
    // scope or view context is implied; use explicit legacy scope to link.
    std::vector<DrawingLegacyModelReference> legacyModelReferences;
    // Type-311 stored XY line endpoints, observed at payload offsets 24..55.
    // Preserve all rows; repeated/degenerate identities are not deduplicated.
    std::vector<DrawingLineSegment> lineSegments;
    // DG 9.54 type-281-to-311 stored references; no owner/context is implied.
    std::vector<DrawingType281LineReference> type281LineReferences;
};
// Partial DG 7.30/7.82/9.50/9.54 semantics: strings (including mark XML), properties, sheet size,
// project identity, subject, view coordinate bases/volumes and model references.
// DG 7.30/7.82/9.54 expose known straight dimension anchors; set ownership is
// linked only when both legacy IDs and drawing contexts are unambiguous.
// DG 9.50 exposes view ID/context prefixes and subtype-1 straight dimension
// anchors/sets; its view geometry, paper rendering, dimension styling, and
// subtype 0 remain raw.
// DG 8.44 additionally exposes its observed view coordinate bases/ranges and
// subtype-1 straight dimensions/sets; subject and model references remain raw.
// DG 8.95/9.08 additionally expose numeric subjects/references; their view and
// dimension geometry stays raw. Read project scope before joining numeric IDs.
// 9.54 type-260 stored paper coordinates and scale scalar are surfaced without
// applying a transform; dimension styling and non-line primitives remain raw.
// Type-309 model-part references are exposed only for DG 7.30/7.82; type-307/310
// references and type-2 subject semantics remain unverified. Legacy numeric IDs
// require explicit same-version project scope before linking. Type-311 XY line
// endpoints are exposed across pinned DG schemas; remaining graphic semantics
// stay raw.
bool parseDrawing(const std::filesystem::path& path, Drawing& result, std::string& error,
                  const db1::RawDatabaseOptions& options = {});
}
