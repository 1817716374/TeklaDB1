#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace tekla::db1
{
using Vec3 = std::array<double, 3>;

struct Property
{
    enum class Kind { Integer, Double, String };
    std::string name;
    Kind kind = Kind::String;
    int32_t integerValue = 0;
    double doubleValue = 0.0;
    std::string stringValue;
    std::string group;
};

struct Identity
{
    uint32_t id = 0;
    uint32_t ownerId = 0;
    uint32_t contextId = 0;
    uint32_t type = 0;
    uint32_t flags = 0;
    uint8_t rowTag = 0;
    std::string guid;
};

struct Point
{
    uint32_t id = 0;
    uint32_t type = 0;
    Vec3 value{};
};

struct Frame
{
    uint32_t id = 0;
    Vec3 axis{};
    Vec3 secondary{};
    Vec3 normal{};
};

struct ContourPoint
{
    Vec3 value{};
    uint32_t chamferType = 0;
    float chamferX = 0.0f;
    float chamferY = 0.0f;
    float chamferDz1 = 0.0f;
    float chamferDz2 = 0.0f;
};

struct ProfileContourPoint
{
    uint32_t index = 0;
    std::array<double, 2> value{};
    uint32_t chamferType = 0;
    float chamferX = 0.0f;
    float chamferY = 0.0f;
};

enum class ProfileSource
{
    Unknown,
    Catalog,
    SketchSolver
};

struct Profile
{
    uint32_t id = 0;
    uint32_t type = 0;
    uint32_t shapeId = 0;
    ProfileSource source = ProfileSource::Unknown;
    std::string name;
    std::map<uint32_t, double> parameters;
    std::vector<std::vector<ProfileContourPoint>> fixedContours;
};

struct PartDefinition
{
    uint32_t id = 0;
    uint32_t subtype = 0;
    std::string name;
    std::string secondaryName;
    std::string profile;
    std::string material;
    std::string classNumber;
};

struct Part
{
    uint32_t id = 0;
    uint32_t internalType = 0;
    uint32_t definitionId = 0;
    uint32_t ownerId = 0;
    uint32_t contextId = 0;
    uint32_t startPointId = 0;
    uint32_t endPointId = 0;
    uint32_t geometryReferenceId = 0;
    uint32_t orientationId = 0;
    std::string guid;
    std::string name;
    std::string profile;
    std::string material;
    std::string classNumber;
    Vec3 start{};
    Vec3 end{};
    Vec3 origin{};
    Vec3 axis{};
    Vec3 secondary{};
    Vec3 normal{};
    double length = 0.0;
    bool contourIsPath = false;
    // Unknown modern contour subtypes must not silently become plates.
    bool contourKindUnverified = false;
    std::vector<ContourPoint> contour;
    std::vector<Property> properties;
};

struct PlaneOperation
{
    uint32_t id = 0;
    uint32_t fatherPartId = 0;
    uint32_t internalType = 0;
    std::string guid;
    Vec3 origin{};
    Vec3 axis{};
    Vec3 secondary{};
    Vec3 normal{};
    double length = 0.0;
    std::vector<Property> properties;
};

struct BooleanOperation
{
    uint32_t id = 0;
    uint32_t fatherPartId = 0;
    bool additive = false;
};

struct BoltDefinition
{
    uint32_t id = 0;
    uint32_t count = 0;
    std::string name;
    std::string standard;
    std::string classNumber;
    float diameter = 0.0f;
    float tolerance = 0.0f;
    float length = 0.0f;
    float extraLength = 0.0f;
    uint32_t boltType = 0;
};

struct BoltLayer
{
    uint32_t sequence = 0;
    uint32_t partId = 0;
    uint32_t flags = 0;
    std::array<double, 4> values{};
};

struct BoltGroup
{
    uint32_t id = 0;
    uint32_t definitionId = 0;
    std::string guid;
    Vec3 first{};
    Vec3 second{};
    Vec3 origin{};
    Vec3 axis{};
    Vec3 secondary{};
    Vec3 normal{};
    double placementLength = 0.0;
    std::vector<Vec3> positions;
    std::vector<BoltLayer> layers;
    std::vector<uint32_t> connectedPartIds;
    std::vector<Property> properties;
};

struct WeldDefinition
{
    uint32_t id = 0;
    float size = 0.0f;
    uint32_t type = 0;
};

// Xsteel 7.82 stores each bolt as a type-10 Part, not a modern BoltGroup.
// id resolves into Model::parts for its definition, stored profile parameters,
// placement, contour and properties. Head/nut/hole geometry is not yet decoded.
struct IndividualBolt
{
    uint32_t id = 0;
    std::vector<uint32_t> connectedPartIds;
};

struct Weld
{
    uint32_t id = 0;
    uint32_t definitionId = 0;
    uint32_t secondaryDefinitionId = 0;
    std::string guid;
    Vec3 origin{};
    Vec3 axis{};
    Vec3 secondary{};
    Vec3 normal{};
    double length = 0.0;
    std::vector<uint32_t> connectedPartIds;
};

struct Assembly
{
    uint32_t id = 0;
    std::string guid;
    std::string name;
    std::vector<uint32_t> memberIds;
    std::vector<Property> properties;
};

struct Component
{
    uint32_t id = 0;
    uint32_t ownerId = 0;
    uint32_t primaryObjectId = 0;
    uint32_t referenceObjectId = 0;
    uint32_t number = 0;
    std::string guid;
    std::string name;
    std::vector<uint32_t> childIds;
    std::vector<Property> properties;
};

struct ControlLine
{
    uint32_t id = 0;
    std::string guid;
    std::vector<uint32_t> pointIds;
    std::vector<Property> properties;
};

struct ParameterDefinition
{
    uint32_t id = 0;
    uint32_t ownerId = 0;
    uint32_t valueType = 0;
    std::string guid;
    std::string name;
    std::string label;
    // Tekla stores both literal defaults and formulas in the same chained
    // string pool.  The original expression is therefore preserved verbatim.
    std::string expression;
};

struct CustomComponentDefinition
{
    uint32_t id = 0;
    uint32_t kind = 0;
    uint32_t classificationCode = 0;
    std::array<uint32_t, 3> referenceIds{};
    std::string guid;
    std::string name;
    std::vector<uint32_t> parameterIds;
    std::vector<uint32_t> childObjectIds;
};

struct ModelMetadata
{
    std::string name;
    std::string designer;
    std::string description;
    std::string version;
    std::string productVersion;
    std::string language;
    std::string templateName;
    std::string environment;
    bool isTemplate = false;
    std::string projectSearchPath;
    std::string firmSearchPath;
    std::string systemSearchPath;
    std::string connectedId;
};

struct Model
{
    std::filesystem::path directory;
    std::filesystem::path databasePath;
    std::string databaseGuid;
    std::string storageVersion;
    ModelMetadata metadata;
    std::unordered_map<uint32_t, Identity> identities;
    std::unordered_map<uint32_t, Point> points;
    std::unordered_map<uint32_t, Frame> frames;
    std::unordered_map<std::string, Profile> profiles;
    std::unordered_map<uint32_t, PartDefinition> definitions;
    std::unordered_map<uint32_t, Part> parts;
    std::vector<uint32_t> actualPartIds;
    std::vector<uint32_t> operativePartIds;
    std::vector<PlaneOperation> fittings;
    std::vector<PlaneOperation> cutPlanes;
    std::vector<BooleanOperation> booleans;
    std::unordered_map<uint32_t, BoltDefinition> boltDefinitions;
    std::vector<BoltGroup> boltGroups;
    std::unordered_map<uint32_t, WeldDefinition> weldDefinitions;
    std::vector<Weld> welds;
    std::vector<Assembly> assemblies;
    std::vector<Component> components;
    std::vector<ControlLine> controlLines;
    std::unordered_map<uint32_t, ParameterDefinition> parameterDefinitions;
    std::vector<CustomComponentDefinition> customComponentDefinitions;
    std::unordered_map<uint32_t, std::vector<Property>> properties;
    std::vector<std::string> diagnostics;
    std::map<std::uint32_t, std::size_t> identityTypeCounts;
    std::vector<std::uint32_t> unhandledPartIds;
    std::vector<IndividualBolt> individualBolts;
};
}
