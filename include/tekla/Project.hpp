#pragma once
#include <tekla/db1/Parser.hpp>
#include <tekla/db1/Catalogs.hpp>
#include <tekla/Drawing.hpp>
#include <tekla/DrawingMetadata.hpp>
#include <tekla/Numbering.hpp>
#include <tekla/Environment.hpp>
#include <tekla/GuidMappings.hpp>
#include <optional>

namespace tekla
{
enum class FileRole { Model, ComponentLibrary, Numbering, Environment, Options, Catalog, Drawing, History, Other, IdentityMapping, DrawingMetadata };
enum class ReadLevel { Discovered, Raw, Semantic, Failed, External, PartialSemantic };
struct ProjectFile
{
    std::filesystem::path path;
    FileRole role = FileRole::Other;
    ReadLevel level = ReadLevel::Discovered;
    std::string diagnostic;
};
struct FileAssociation
{
    std::filesystem::path source;
    std::filesystem::path target;
    std::string reason;
};
struct ProjectOptions
{
    // Optional exact main DB1, relative to the project directory or absolute.
    std::filesystem::path mainDatabase;
    // Search precedence: model directory, then these explicitly supplied roots.
    // Metadata paths are reported but never silently expanded on this machine.
    std::vector<std::filesystem::path> resourceDirectories;
    bool readComponentLibrary = true;
    bool readRawCompanions = true;
    bool strictCompanions = false;
    db1::ModelReadOptions modelOptions;
    db1::RawDatabaseOptions rawOptions{true};
    bool readNumbering = true;
    bool readDrawings = true;
    bool readEnvironment = true;
    bool readOptions = true;
    // Caller explicitly trusts same-directory, same-basename 7.30/7.82 DB1/DB2 files
    // when both lack GUIDs. Never bypasses a GUID or storage-version conflict.
    bool trustLegacyNumberingBasenames = false;
    bool readGuidMappings = true;
    bool readDrawingMetadata = true;
};
struct DrawingModelAssociation
{
    std::filesystem::path drawing;
    std::uint32_t drawingRecordId = 0;
    std::uint32_t modelObjectId = 0;
    // For numeric DG identities this is the matched DB1 identity's GUID.
    std::string modelGuid;
    std::uint32_t drawingContextId = 0;
};
struct AttributeDefinitionAssociation
{
    std::uint32_t modelObjectId = 0;
    std::size_t propertyIndex = 0;
    std::uint32_t attributeDefinitionId = 0;
    // Exact case-sensitive name and storage-type match only. This does not
    // establish class applicability or supply an object's missing/default value.
};
struct SurfaceMaterialAssociation
{
    std::filesystem::path database;
    std::uint32_t surfaceTreatmentId = 0;
    std::size_t materialIndex = 0;
};
enum class NumberingPairEvidence { DatabaseGuid, ExplicitLegacyBasename };
struct ObjectNumberingSeriesAssociation
{
    std::filesystem::path database;
    std::filesystem::path numberingDatabase;
    std::uint32_t objectId = 0;
    std::uint32_t numberingRecordId = 0;
    std::size_t seriesIndex = 0; // index into Project::numbering.at(numberingDatabase).series
    NumberingPairEvidence pairingEvidence = NumberingPairEvidence::DatabaseGuid;
};
struct OptionSettingAssociation
{
    std::filesystem::path optionsDatabase;
    OptionValueMatch match; // index into Project::optionSettings->settings
};
struct DrawingMetadataAssociation
{
    std::filesystem::path metadata;
    std::filesystem::path drawing;
    // Present only when an existing scoped DG-to-DB1 subject association agrees
    // with MainObjectGuid. Metadata never supplies missing DG subject identity.
    std::optional<std::uint32_t> modelObjectId;
};
struct Project
{
    db1::Model model;
    std::optional<db1::Model> componentLibrary;
    std::optional<db1::MaterialCatalog> materials;
    std::optional<db1::BoltCatalog> bolts;
    std::optional<db1::BoltAssemblyCatalog> boltAssemblies;
    std::optional<db1::ProfitabCatalog> profileRules;
    db1::ShapeCatalog shapes;
    std::map<std::filesystem::path, db1::RawDatabase> rawCompanions;
    std::vector<ProjectFile> files;
    std::vector<FileAssociation> associations;
    std::vector<std::string> diagnostics;
    std::map<std::filesystem::path, NumberingDatabase> numbering;
    std::map<std::filesystem::path, Drawing> drawings;
    std::vector<DrawingModelAssociation> drawingModelAssociations;
    std::optional<EnvironmentDatabase> environment;
    std::map<std::filesystem::path, OptionsDatabase> optionsDatabases;
    std::vector<AttributeDefinitionAssociation> attributeDefinitionAssociations;
    std::vector<DrawingModelAssociation> drawingSubjectAssociations;
    // Exact unique name matches into materials->materials, scoped by DB1 path.
    std::vector<SurfaceMaterialAssociation> surfaceMaterialAssociations;
    std::vector<ObjectNumberingSeriesAssociation> objectNumberingSeriesAssociations;
    std::optional<OptionSettingsFile> optionSettings;
    std::vector<OptionSettingAssociation> optionSettingAssociations;
    std::optional<GuidMappingFile> guidMappings;
    std::vector<GuidMappingTarget> guidMappingTargets;
    std::map<std::filesystem::path,DrawingVersionMetadata> drawingMetadata;
    // Same-directory filename pairing plus matching DG saved filename, sheet
    // and subject type. Conflicting/missing fields are diagnosed, not guessed.
    // No DrawingGuid equality or "latest version" selection is inferred.
    std::vector<DrawingMetadataAssociation> drawingMetadataAssociations;
};
// True means the main model was read. Check file levels and diagnostics for
// partial companions. PartialSemantic is intentionally distinct from Semantic.
bool readProject(const std::filesystem::path& directory, Project& result,
                 std::string& error, const ProjectOptions& options = {});
}
