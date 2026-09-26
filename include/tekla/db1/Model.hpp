#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <optional>
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
    // 8.95: reference to IdentityClass, not an owner ID.
    uint32_t classReferenceId = 0;
};

struct IdentityClass
{
    uint32_t id = 0;
    uint32_t recordKind = 0;
    std::array<uint32_t, 5> rawFields{};
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

struct PartPosition
{
    // Record ID in partPositions; definition ID in partDefinitionPositions.
    uint32_t id = 0;
    float startAxialOffset = 0.0f;
    float endAxialOffset = 0.0f;
    uint32_t depthCode = 0;
    float depthOffset = 0.0f;
    uint32_t planeCode = 0;
    float planeOffset = 0.0f;
    // Modern payload offsets 8,12,20,24,36,40; 7.82 definition offsets
    // 24,28,36,40,52,56. Their meanings are unverified.
    std::array<uint32_t, 6> rawFields{};
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
    // Modern payload offset 8, indexes Model::partPositions when supported.
    // Stored origin/length already contain position adjustments; do not reapply.
    uint32_t auxiliaryReferenceId = 0;
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

struct SurfaceTreatmentDefinition
{
    uint32_t id = 0;
    std::string classNumber;
    std::string name;
    std::string profile;
    std::string material;
    std::string typeName;
    uint32_t typeCode = 0;
    // Derived only from an exact PL<number> profile, not an independent field.
    std::optional<double> thicknessFromProfile;
    // Uninterpreted payload words at 4..64 and 272..288 (7.82 layout).
    std::array<uint32_t, 16> rawHeader{};
    std::array<uint32_t, 5> rawTail{};
};

struct SurfaceTreatment
{
    uint32_t id = 0;
    uint32_t definitionId = 0;
    uint32_t fatherPartId = 0;
    uint32_t ownerId = 0;
    uint32_t startPointId = 0;
    uint32_t endPointId = 0;
    uint32_t contourId = 0;
    uint32_t orientationId = 0;
    std::string guid;
    Vec3 start{}, end{}, origin{}, axis{}, secondary{}, normal{};
    double storedLength = 0.0;
    // Stored local contour; origin and basis are kept separately. No clipping.
    std::vector<ContourPoint> contour;
    std::array<uint8_t, 22> rawTail{};
    std::vector<Property> properties;
    std::vector<uint32_t> distanceParameterIds;
    std::vector<uint32_t> formulaBindingIds;
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
    std::vector<uint32_t> parameterIds;
    std::vector<uint32_t> distanceParameterIds;
    std::vector<uint32_t> formulaBindingIds;
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
    // Decoded description for 7.82; other layouts currently retain raw references.
    std::string description;
    std::vector<uint32_t> distanceParameterIds;
    std::vector<uint32_t> formulaBindingIds;
    // Type-4 association targets can be points or other objects, not a line.
    std::vector<uint32_t> referenceObjectIds;
};

// Component distance variables in 7.82 and verified modern layouts. Values and flags retain their stored form;
// secondaryStoredValue is not established as a default or an effective value.
struct DistanceParameter
{
    uint32_t id = 0;
    uint32_t ownerId = 0;
    std::string guid;
    std::string name;
    std::string label;
    double storedDistance = 0.0;
    double secondaryStoredValue = 0.0;
    std::string propertyToken;
    std::string planeToken;
    std::array<uint32_t, 7> rawFields{}; // payload offsets 12,32,36,40,44,48,52
    std::vector<uint32_t> boundObjectIds; // type-58 targets; no endpoint order inferred
    std::vector<uint32_t> formulaBindingIds;
};

// Stored formula-to-property bindings; formulas are never executed by parsing.
struct FormulaBinding
{
    uint32_t id = 0;
    uint32_t ownerId = 0;
    uint32_t targetObjectId = 0;
    uint32_t storedIndex = 0;
    std::string guid;
    std::string propertyName;
    std::string expression;
    // Type-59 targets include the output target exactly once in the known schema.
    // Other references may cross owner boundaries; they are not lexical tokens.
    std::vector<uint32_t> referencedObjectIds;
    // Non-target references only. An expression can also read its own target;
    // these IDs do not by themselves establish evaluation order or acyclicity.
    std::vector<uint32_t> inputObjectIds;
};

// Variables can belong to other identity objects as well as custom components.
struct VariableOwnership
{
    std::vector<uint32_t> parameterIds;
    std::vector<uint32_t> distanceParameterIds;
    std::vector<uint32_t> formulaBindingIds;
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
    std::unordered_map<uint32_t, DistanceParameter> distanceParameters;
    std::unordered_map<uint32_t, FormulaBinding> formulaBindings;
    std::unordered_map<uint32_t, std::vector<uint32_t>> formulaBindingIdsByTarget;
    std::unordered_map<uint32_t, IdentityClass> identityClasses;
    std::unordered_map<uint32_t, VariableOwnership> variablesByOwner;
    std::unordered_map<uint32_t, std::vector<uint32_t>> customComponentReferences;
    std::unordered_map<uint32_t, PartPosition> partPositions;
    std::unordered_map<uint32_t, SurfaceTreatmentDefinition> surfaceTreatmentDefinitions;
    std::unordered_map<uint32_t, SurfaceTreatment> surfaceTreatments;
    std::unordered_map<uint32_t, std::vector<uint32_t>> surfaceTreatmentIdsByFather;
    // 7.82 inline positions, keyed by Part::definitionId, including unused definitions.
    // Independent namespace from modern partPositions/auxiliaryReferenceId.
    std::unordered_map<uint32_t, PartPosition> partDefinitionPositions;
};
}
