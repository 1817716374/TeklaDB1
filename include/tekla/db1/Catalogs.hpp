#pragma once

#include <tekla/db1/Model.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <optional>
#include <utility>
#include <unordered_map>
#include <vector>

namespace tekla::db1
{
struct CatalogTableInfo
{
    std::uint32_t recordCount = 0;
    std::uint32_t payloadSize = 0;
};

struct CatalogRawRecord
{
    std::uint8_t rowTag = 0;
    std::vector<std::uint8_t> payload;
};

struct ProfileCatalog
{
    std::uint32_t version = 0;
    std::unordered_map<std::string, Profile> profiles;
    // These names have multiple profdb.bin definition records. The legacy map
    // keeps one record for compatibility; callers must not treat it as unique.
    std::vector<std::string> ambiguousProfileNames;
};

// Numeric fixed-profile properties from either the observed third-party
// PROFILE DATABASE .lis dialect or documented Tekla PROFILE CATALOG exports.
// Tekla cross-section point/chamfer fields are also exposed below; tree/rule
// sections and unrecognized content remain in rawText.
// Property names are preserved verbatim; unknown names are not dropped.
struct ProfileCatalogLisEntry
{
    std::string name;
    std::uint32_t type = 0;
    std::uint32_t subType = 0;
    double storedCoordinate = 0.0;
    std::map<std::string, double> properties;
    std::string rawRecord;
};

struct ProfileCatalogLisCrossSectionPoint
{
    std::uint32_t pointNumber = 0;
    double x = 0.0;
    double y = 0.0;
    std::uint32_t chamferType = 0;
    double chamferX = 0.0;
    double chamferY = 0.0;
    bool hasChamferType = false;
    bool hasChamferX = false;
    bool hasChamferY = false;
};

struct ProfileCatalogLisCrossSection
{
    std::string name;
    std::vector<ProfileCatalogLisCrossSectionPoint> points;
    std::string rawRecord;
};

enum class ProfileCatalogLisFormat
{
    Unspecified,
    TeklaProfileCatalogExport,
    ThirdPartyProfileDatabaseExportV2
};

struct ProfileCatalogLis
{
    std::uint32_t version = 0;
    std::vector<ProfileCatalogLisEntry> entries;
    std::vector<std::string> ambiguousProfileNames;
    std::filesystem::path sourcePath;
    std::string rawText;
    // Documented CROSS_SECTION_NAME / POINT_* records from native exports.
    // SUB_TYPE-to-name resolution and chamfer geometry are not inferred.
    std::vector<ProfileCatalogLisCrossSection> crossSections;
    // Both accepted .lis dialects share this container for compatibility;
    // callers can still distinguish them without reparsing rawText.
    ProfileCatalogLisFormat format = ProfileCatalogLisFormat::Unspecified;
};

struct SketchProfileChamfer
{
    std::uint32_t id = 0;
    std::uint32_t type = 0;
    double x = 0.0;
    double y = 0.0;
    double dz1 = 0.0;
    double dz2 = 0.0;
    std::uint32_t flags = 0;
};

struct SketchProfilePoint
{
    std::uint32_t id = 0;
    std::uint32_t chamferId = 0;
    std::array<double, 2> value{};
};

struct SketchProfile
{
    std::string prefix;
    std::string suffix;
    std::string name;
    std::uint32_t firstPointBlockId = 0;
    std::uint32_t flags = 0;
    std::array<double, 4> solvedValues{};
    std::vector<ProfileContourPoint> contour;
    std::vector<std::uint8_t> rawPayload;
};

// pgdb.bin is the model-local geometry database produced by Tekla Sketch
// Solver.  Its five fixed-layout tables are retained as public primitives as
// well as joined into solved profile contours.
struct ProfileGeometryCatalog
{
    std::array<CatalogTableInfo, 5> tables{};
    std::array<std::vector<CatalogRawRecord>, 5> rawTables;
    std::unordered_map<std::uint32_t, SketchProfilePoint> points;
    std::unordered_map<std::uint32_t, SketchProfileChamfer> chamfers;
    std::unordered_map<std::string, SketchProfile> profiles;
    std::vector<std::string> diagnostics;
};

struct Material
{
    std::uint8_t rowTag = 0;
    std::uint32_t category = 0;
    std::string name;
    std::string alias;
    std::string grade;
    double density = 0.0;
    double secondaryDensity = 0.0;
    double elasticModulus = 0.0;
    double poissonRatio = 0.0;
    double thermalExpansion = 0.0;
    std::uint32_t flags = 0;
    std::vector<std::uint8_t> rawPayload;
};

struct MaterialIntegerAttribute
{
    std::uint8_t rowTag = 0;
    std::string materialName;
    std::string attributeName;
    std::uint32_t value = 0;
};

struct MaterialAttributeReference
{
    std::uint8_t rowTag = 0;
    std::string materialName;
    std::string attributeName;
};

struct MaterialStringAttribute
{
    std::uint8_t rowTag = 0;
    std::string materialName;
    std::string attributeName;
    std::string value;
};

struct MaterialAttributeDefinition
{
    std::uint8_t rowTag = 0;
    std::uint32_t id = 0;
    std::uint32_t valueKind = 0;
    std::uint32_t scope = 0;
    std::uint32_t flags = 0;
    std::string name;
    std::string label;
    std::vector<std::uint8_t> rawPayload;
};

struct MaterialLabel
{
    std::uint8_t rowTag = 0;
    std::uint32_t id = 0;
    std::string key;
    std::string text;
};

struct MaterialCatalog
{
    std::uint32_t version = 0;
    std::array<CatalogTableInfo, 6> tables{};
    std::vector<Material> materials;
    std::vector<MaterialIntegerAttribute> integerAttributes;
    std::vector<MaterialAttributeReference> attributeReferences;
    std::vector<MaterialStringAttribute> stringAttributes;
    std::vector<MaterialAttributeDefinition> attributeDefinitions;
    std::vector<MaterialLabel> labels;
};

struct BoltCatalogEntry
{
    std::uint8_t rowTag = 0;
    std::string name;
    float diameter = 0.0f;
    float length = 0.0f;
    std::array<float, 7> trailingDimensions{};
    std::vector<std::uint8_t> rawPayload;
    // Exact stored bolt element standard; assdb component slots refer to
    // standards, which can each resolve to many size-specific bolt entries.
    std::string standard;
};

struct BoltCatalog
{
    std::uint32_t version = 0;
    CatalogTableInfo table;
    std::vector<BoltCatalogEntry> bolts;
};

struct BoltAssemblyCatalogEntry
{
    std::uint8_t rowTag = 0;
    std::string name;
    std::array<std::string, 7> componentNames;
    std::vector<std::uint8_t> rawPayload;
};

struct BoltAssemblyCatalog
{
    std::uint32_t version = 0;
    CatalogTableInfo table;
    std::vector<BoltAssemblyCatalogEntry> assemblies;
};

struct ParametricProfileRule
{
    std::string prefix;
    std::string type;
    int sortOrder = 0;
    int unit = 0;
    int minimumNumbers = 0;
    int maximumNumbers = 0;
    std::string generator;
    // Trimble documents G3-NAME values of the form libraryId.SectionName.
    // These are populated only when the value contains exactly one usable dot.
    std::string generatorLibraryId;
    std::string generatorSectionName;
    std::string namePattern;
    std::string sourceLine;
};

struct ProfitabCatalog
{
    std::vector<ParametricProfileRule> rules;
    std::vector<std::string> diagnostics;
};

struct ClbStatement
{
    std::string keyword;
    std::vector<std::string> arguments;
    std::vector<ClbStatement> children;
    std::size_t line = 0;
};

struct ClbNamedValue
{
    std::string name;
    std::string description;
    std::string type; // stored CLB type token; enum meanings are not normalized
    std::string defaultValue;
    std::string formula; // retained expression text; never evaluated
    std::size_t line = 0;
};

struct ClbPointReference
{
    std::optional<std::uint32_t> stationIndex; // documented 0=start, 1=end
    std::string yReference; // expression identifier, not a computed coordinate
    std::string zReference;
    std::size_t line = 0;
};

struct ClbFace
{
    std::optional<std::uint32_t> index;
    std::vector<ClbPointReference> points;
    std::size_t line = 0;
};

struct ClbGeometry
{
    std::string name;
    std::vector<ClbFace> faces;
    std::size_t line = 0;
};

struct ClbSection
{
    std::string name;
    std::vector<ClbNamedValue> baseAttributes;
    std::vector<ClbNamedValue> expressions;
    std::vector<ClbGeometry> geometries;
    std::size_t line = 0;
};

struct ClbInclude
{
    std::string target;
    std::size_t line = 0;
};

struct ClbDocument
{
    std::vector<ClbStatement> statements;
    // Extracted only from a unique top-level library_id statement. The full
    // generic syntax tree remains available in statements.
    std::string libraryId;
    std::vector<ClbSection> sections;
    // Top-level Include statements, as documented for components.clb.
    std::vector<ClbInclude> includes;
    std::vector<std::string> diagnostics;
};

struct ShapeDefinition
{
    std::string name;
    std::string brepStorageId;
    std::string guid;
    Vec3 minimum{};
    Vec3 maximum{};
    std::uint32_t geometryType = 0;
    std::string fingerprint;
    std::filesystem::path sourcePath;
    bool hasGeometryType = false; // geometryType is a default when absent
    std::optional<bool> isSolid; // saved declaration, not a topology verdict
    std::string rawXml;
};

struct ShapeFace
{
    std::vector<std::uint32_t> outerLoop;
    std::vector<std::vector<std::uint32_t>> innerLoops;
};

enum class ShapeEdgeType
{
    Unknown,
    Visible,
    Invisible
};

struct ShapeEdge
{
    std::uint32_t firstVertex = 0;
    std::uint32_t secondVertex = 0;
    ShapeEdgeType type = ShapeEdgeType::Unknown;
    std::string rawType;
};

struct ShapeGeometry
{
    std::string storageId;
    std::vector<Vec3> points;
    std::vector<ShapeFace> faces;
    std::vector<ShapeEdge> edges;
    std::filesystem::path sourcePath;
    std::string rawXml;
};

struct ShapeReadOptions
{
    std::size_t maxDecodedBytes = 128ULL * 1024 * 1024;
};

struct ShapeCatalog
{
    std::unordered_map<std::string, ShapeDefinition> definitionsByGuid;
    std::unordered_map<std::string, ShapeGeometry> geometriesByStorageId;
    std::vector<std::string> diagnostics;
};

struct RebarShapeCategory
{
    std::string name;
    std::vector<std::string> shapeIds;
};

// Tekla reinforcement shape catalog XML (.rscx). Root-level shapes are
// retained separately because the format permits entries outside categories.
struct RebarShapeCatalog
{
    std::vector<RebarShapeCategory> categories;
    std::vector<std::string> uncategorizedShapeIds;
    std::filesystem::path sourcePath;
    std::string rawXml;
};

// Ordered entries observed in rebar_schedule_config.inp. Values remain raw
// expressions: this API does not evaluate Tekla's formula language.
struct RebarScheduleConfigEntry
{
    std::string internalBendingType;
    std::uint32_t index = 0;
    std::string rawValue;
    std::size_t lineNumber = 0;
};
struct RebarScheduleConfig
{
    std::vector<RebarScheduleConfigEntry> entries;
    std::vector<std::pair<std::size_t, std::string>> unparsedLines;
    std::filesystem::path sourcePath;
    std::string rawText;
};

bool parseProfileCatalog(const std::filesystem::path& path, ProfileCatalog& result, std::string& error);
// Reads the documented PROFILE CATALOG EXPORT dialect and, for compatibility,
// the observed third-party PROFILE DATABASE EXPORT VERSION 2 dialect. The
// result.format field identifies which header was accepted.
bool parseProfileCatalogLis(const std::filesystem::path& path, ProfileCatalogLis& result, std::string& error);
// UTF-8 TeklaStructuresModels with one direct Model; decoded size <= 16 MiB.
// Retains raw XML. Unsupported DTD/entities and ambiguous known fields fail.
bool parseModelMetadata(const std::filesystem::path& path, ModelMetadata& result, std::string& error);
bool parseProfileGeometryCatalog(const std::filesystem::path& path, ProfileGeometryCatalog& result,
                                 std::string& error);
bool parseMaterialCatalog(const std::filesystem::path& path, MaterialCatalog& result, std::string& error);
bool parseBoltCatalog(const std::filesystem::path& path, BoltCatalog& result, std::string& error);
bool parseBoltAssemblyCatalog(const std::filesystem::path& path, BoltAssemblyCatalog& result, std::string& error);
bool parseProfitabCatalog(const std::filesystem::path& path, ProfitabCatalog& result, std::string& error);
bool parseClbDocument(const std::filesystem::path& path, ClbDocument& result, std::string& error);
// UTF-8 ImportPart/Info and Polymesh; raw XML and unknown extensions retained.
// Existing three-argument calls use the default decoded size limit.
bool parseShapeDefinition(const std::filesystem::path& path, ShapeDefinition& result, std::string& error);
bool parseShapeDefinition(const std::filesystem::path& path, ShapeDefinition& result, std::string& error,
                          const ShapeReadOptions& options);
bool parseShapeGeometry(const std::filesystem::path& path, ShapeGeometry& result, std::string& error);
bool parseShapeGeometry(const std::filesystem::path& path, ShapeGeometry& result, std::string& error,
                        const ShapeReadOptions& options);
bool parseShapeCatalog(const std::filesystem::path& directory, ShapeCatalog& result, std::string& error);
bool parseRebarShapeCatalog(const std::filesystem::path& path, RebarShapeCatalog& result, std::string& error);
// Partial reader for BEND_TYPE_<internal type>[index]=value entries. Input is
// limited to 64 MiB. Unknown non-empty lines are retained; expressions are never evaluated.
bool parseRebarScheduleConfig(const std::filesystem::path& path, RebarScheduleConfig& result, std::string& error);
}
