#pragma once

#include <tekla/db1/Model.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
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

struct ClbDocument
{
    std::vector<ClbStatement> statements;
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
};

struct ShapeCatalog
{
    std::unordered_map<std::string, ShapeDefinition> definitionsByGuid;
    std::unordered_map<std::string, ShapeGeometry> geometriesByStorageId;
    std::vector<std::string> diagnostics;
};

bool parseProfileCatalog(const std::filesystem::path& path, ProfileCatalog& result, std::string& error);
bool parseModelMetadata(const std::filesystem::path& path, ModelMetadata& result, std::string& error);
bool parseProfileGeometryCatalog(const std::filesystem::path& path, ProfileGeometryCatalog& result,
                                 std::string& error);
bool parseMaterialCatalog(const std::filesystem::path& path, MaterialCatalog& result, std::string& error);
bool parseBoltCatalog(const std::filesystem::path& path, BoltCatalog& result, std::string& error);
bool parseBoltAssemblyCatalog(const std::filesystem::path& path, BoltAssemblyCatalog& result, std::string& error);
bool parseProfitabCatalog(const std::filesystem::path& path, ProfitabCatalog& result, std::string& error);
bool parseClbDocument(const std::filesystem::path& path, ClbDocument& result, std::string& error);
bool parseShapeDefinition(const std::filesystem::path& path, ShapeDefinition& result, std::string& error);
bool parseShapeGeometry(const std::filesystem::path& path, ShapeGeometry& result, std::string& error);
bool parseShapeCatalog(const std::filesystem::path& directory, ShapeCatalog& result, std::string& error);
}
