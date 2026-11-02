#pragma once
#include <tekla/db1/Parser.hpp>
#include <tekla/db1/Catalogs.hpp>
#include <tekla/Drawing.hpp>
#include <tekla/DrawingMetadata.hpp>
#include <tekla/AnalysisSettings.hpp>
#include <tekla/Numbering.hpp>
#include <tekla/Environment.hpp>
#include <tekla/GuidMappings.hpp>
#include <tekla/IdRangeMapper.hpp>
#include <tekla/History.hpp>
#include <optional>

namespace tekla
{
enum class FileRole { Model, ComponentLibrary, Numbering, Environment, Options, Catalog, Drawing, History, Other, IdentityMapping, DrawingMetadata, Analysis, AnalysisSettings, IdRangeMapper, AnalysisResults, PlanDatabase, ModelMetadata };
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
    // Caller explicitly trusts same-directory, same-basename 7.30/7.64/7.82 DB1/DB2 files
    // when both lack GUIDs. Never bypasses a GUID or storage-version conflict.
    bool trustLegacyNumberingBasenames = false;
    // Caller explicitly scopes all DG files discovered under this readProject
    // root to its selected legacy DB1. Enables numeric subject/reference links
    // only when both files lack project GUIDs and storage versions match.
    bool trustLegacyDrawingObjectIds = false;
    bool readGuidMappings = true;
    bool readDrawingMetadata = true;
    bool readAnalysisSettings = true;
    bool readProfileLibraries = true;
    bool readRebarScheduleConfig = true;
    bool readIdRangeMappers = true;
    bool readHistory = true;
};
enum class DrawingPairEvidence
{
    ProjectGuidAndObjectIdentity,
    ExplicitLegacyNumericScope,
    // A DG subject's stored GUID uniquely matched one DB1 identity, but the
    // drawing's project GUID did not match. This links only that subject; it
    // does not scope other numeric/model references in the drawing.
    DirectSubjectGuidIdentity
};
struct DrawingModelAssociation
{
    std::filesystem::path drawing;
    std::uint32_t drawingRecordId = 0;
    std::uint32_t modelObjectId = 0;
    // For numeric DG identities this is the matched DB1 identity's GUID.
    std::string modelGuid;
    std::uint32_t drawingContextId = 0;
    DrawingPairEvidence pairingEvidence = DrawingPairEvidence::ProjectGuidAndObjectIdentity;
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
struct PartMaterialAssociation
{
    std::filesystem::path database;
    std::uint32_t objectId = 0;
    std::string materialName;
    std::size_t materialIndex = 0;
};
struct ProfileCatalogAssociation
{
    std::filesystem::path database;
    std::uint32_t objectId = 0;
    std::string profileName;
    std::filesystem::path catalog;
    db1::ProfileSource source = db1::ProfileSource::Unknown;
};
struct ProfileRuleSectionAssociation
{
    std::filesystem::path profitab;
    std::size_t ruleIndex = 0;
    std::filesystem::path clb;
    std::size_t sectionIndex = 0; // index into Project::clbDocuments.at(clb).sections
};
struct ClbIncludeAssociation
{
    std::filesystem::path components;
    std::size_t includeIndex = 0; // index into Project::clbDocuments.at(components).includes
    std::filesystem::path clb;
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
struct ReinforcementNumberingSeriesAssociation
{
    std::filesystem::path database;
    std::filesystem::path numberingDatabase;
    std::uint32_t numberingRecordId = 0;
    std::size_t seriesIndex = 0; // index into Project::numbering.at(numberingDatabase).series
    // Exact raw equality only: DB1 reinforcement payload @8 and DB2 series
    // payload @72, scoped by the same exact prefix and start number.
    std::uint32_t db1RawField = 0;
    std::uint32_t db2RawField = 0;
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
    // File pairing can be proven by exact sidecar/DG filename and sheet, even
    // when this DG version has no decoded subject. This optional object link is
    // present only when the scoped DG subject agrees with MainObjectGuid.
    std::optional<std::uint32_t> modelObjectId;
    enum class Evidence { FilenameAndSheet, SubjectIdentity } evidence = Evidence::SubjectIdentity;
};
struct BoltAssemblyStandardAssociation
{
    std::filesystem::path assemblyCatalog;
    std::size_t assemblyIndex = 0;
    std::size_t componentIndex = 0; // index into the assembly's seven component slots
    std::string standard;
    std::filesystem::path boltCatalog;
    // All size-specific screwdb entries with the exact stored standard.
    std::vector<std::size_t> boltEntryIndices;
};
struct HistoryObjectAssociation
{
    std::filesystem::path historyDatabase;
    std::int64_t historyItemId = 0;
    std::uint32_t modelObjectId = 0;
    std::string modelGuid;
};
struct Project
{
    db1::Model model;
    std::optional<db1::Model> componentLibrary;
    std::optional<db1::MaterialCatalog> materials;
    std::optional<db1::BoltCatalog> bolts;
    std::optional<db1::BoltAssemblyCatalog> boltAssemblies;
    std::optional<db1::ProfitabCatalog> profileRules;
    std::optional<db1::RebarShapeCatalog> rebarShapeCatalog;
    std::optional<db1::RebarScheduleConfig> rebarScheduleConfig;
    std::map<std::filesystem::path, db1::ClbDocument> clbDocuments;
    std::vector<ClbIncludeAssociation> clbIncludeAssociations;
    std::vector<ProfileRuleSectionAssociation> profileRuleSectionAssociations;
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
    // Exact profile-name matches to one catalog source. Ambiguous names across
    // catalogs are left unlinked and diagnosed instead of selecting a source.
    std::vector<ProfileCatalogAssociation> profileCatalogAssociations;
    std::vector<ObjectNumberingSeriesAssociation> objectNumberingSeriesAssociations;
    std::vector<ReinforcementNumberingSeriesAssociation> reinforcementNumberingSeriesAssociations;
    std::optional<OptionSettingsFile> optionSettings;
    std::vector<OptionSettingAssociation> optionSettingAssociations;
    std::optional<GuidMappingFile> guidMappings;
    std::vector<GuidMappingTarget> guidMappingTargets;
    std::map<std::filesystem::path,DrawingVersionMetadata> drawingMetadata;
    // Same-directory filename pairing plus matching DG saved filename, sheet
    // and subject type. Conflicting/missing fields are diagnosed, not guessed.
    // No DrawingGuid equality or "latest version" selection is inferred.
    std::vector<DrawingMetadataAssociation> drawingMetadataAssociations;
    std::map<std::filesystem::path,AnalysisModelSettings> analysisSettings;
    std::map<std::filesystem::path, IdRangeMapperFile> idRangeMappers;
    // Exact unique part-material name matches into materials->materials.
    // Missing or duplicate catalog names remain unlinked.
    std::vector<PartMaterialAssociation> partMaterialAssociations;
    // Exact assdb component-standard matches into every screwdb entry with that standard.
    std::vector<BoltAssemblyStandardAssociation> boltAssemblyStandardAssociations;
    std::map<std::filesystem::path, HistoryDatabase> historyDatabases;
    std::vector<HistoryObjectAssociation> historyObjectAssociations;
};
// True means the main model was read. Check file levels and diagnostics for
// partial companions. PartialSemantic is intentionally distinct from Semantic.
bool readProject(const std::filesystem::path& directory, Project& result,
                 std::string& error, const ProjectOptions& options = {});
}
