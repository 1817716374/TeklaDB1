#pragma once
#include <tekla/db1/Parser.hpp>
#include <tekla/db1/Catalogs.hpp>
#include <tekla/Drawing.hpp>
#include <tekla/Numbering.hpp>
#include <tekla/Environment.hpp>
#include <optional>

namespace tekla
{
enum class FileRole { Model, ComponentLibrary, Numbering, Environment, Options, Catalog, Drawing, History, Other };
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
};
struct DrawingModelAssociation
{
    std::filesystem::path drawing;
    std::uint32_t drawingRecordId = 0;
    std::uint32_t modelObjectId = 0;
    std::string modelGuid;
};
struct AttributeDefinitionAssociation
{
    std::uint32_t modelObjectId = 0;
    std::size_t propertyIndex = 0;
    std::uint32_t attributeDefinitionId = 0;
    // Exact case-sensitive name and storage-type match only. This does not
    // establish class applicability or supply an object's missing/default value.
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
};
// True means the main model was read. Check file levels and diagnostics for
// partial companions. PartialSemantic is intentionally distinct from Semantic.
bool readProject(const std::filesystem::path& directory, Project& result,
                 std::string& error, const ProjectOptions& options = {});
}
