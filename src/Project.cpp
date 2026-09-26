#include <tekla/Project.hpp>
#include "Path.hpp"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace tekla
{
namespace
{
std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}
std::filesystem::path findFile(const std::filesystem::path& root, const std::string& name)
{
    std::vector<std::filesystem::path> matches;
    if (!std::filesystem::is_directory(root)) return {};
    for (const auto& entry : std::filesystem::directory_iterator(root))
        if (entry.is_regular_file() && lower(db1::detail::pathUtf8(entry.path().filename())) == lower(name))
            matches.push_back(entry.path());
    if (matches.size() > 1) throw std::runtime_error("ambiguous case-insensitive resource " + name);
    return matches.empty() ? std::filesystem::path{} : matches.front();
}
FileRole roleFor(const std::filesystem::path& path)
{
    const auto name = lower(db1::detail::pathUtf8(path.filename()));
    const auto ext = lower(db1::detail::pathUtf8(path.extension()));
    if (name == "xslib.db1") return FileRole::ComponentLibrary;
    if (ext == ".db1") return FileRole::Model;
    if (ext == ".db2") return FileRole::Numbering;
    if (ext == ".dg") return FileRole::Drawing;
    if (name == "environment.db") return FileRole::Environment;
    if (name == "options_model.db" || name == "options_drawings.db") return FileRole::Options;
    if (name == "history.db") return FileRole::History;
    if (name == "profdb.bin" || name == "pgdb.bin" || name == "matdb.bin" || name == "screwdb.db" ||
        name == "assdb.db" || name == "profitab.inp") return FileRole::Catalog;
    return FileRole::Other;
}
}

bool readProject(const std::filesystem::path& directory, Project& result, std::string& error,
                 const ProjectOptions& options)
{
    try
    {
        error.clear(); result = {};
        const auto root = std::filesystem::absolute(directory).lexically_normal();
        if (!std::filesystem::is_directory(root)) throw std::runtime_error("project input is not a directory");
        const auto ok = options.mainDatabase.empty()
            ? db1::parseModelDirectory(root, result.model, error, options.modelOptions)
            : db1::parseModelFile(options.mainDatabase.is_absolute() ? options.mainDatabase : root / options.mainDatabase, result.model, error, options.modelOptions);
        if (!ok) { result = {}; return false; }
        if (!std::filesystem::equivalent(result.model.directory, root))
            throw std::runtime_error("explicit main database must belong to the supplied project directory");
        result.diagnostics = result.model.diagnostics;
        const auto mark = [&](const std::filesystem::path& path, ReadLevel level, const std::string& diagnostic = std::string{}) {
            const auto canonical = std::filesystem::absolute(path).lexically_normal();
            auto it = std::find_if(result.files.begin(), result.files.end(), [&](const auto& item) { return item.path == canonical; });
            if (it == result.files.end()) result.files.push_back({canonical, roleFor(path), level, diagnostic});
            else { it->level = level; it->diagnostic = diagnostic; }
        };
        const auto failure = [&](const std::filesystem::path& path, const std::string& diagnostic) {
            mark(path, ReadLevel::Failed, diagnostic);
            const auto message = db1::detail::pathUtf8(path) + ": " + diagnostic;
            result.diagnostics.push_back(message);
            if (options.strictCompanions) throw std::runtime_error(message);
        };
        for (const auto& entry : std::filesystem::directory_iterator(root))
            if (entry.is_regular_file())
            {
                const auto role = roleFor(entry.path());
                if (role == FileRole::Other) continue;
                mark(entry.path(), role == FileRole::History ? ReadLevel::External : ReadLevel::Discovered,
                     role == FileRole::History ? "SQLite history: use a SQLite reader" : "");
            }
        const auto drawings = root / "drawings";
        if (std::filesystem::is_directory(drawings))
            for (const auto& entry : std::filesystem::directory_iterator(drawings))
                if (entry.is_regular_file() && roleFor(entry.path()) == FileRole::Drawing)
                    mark(entry.path(), ReadLevel::Discovered, "DG reading is optional; full drawing semantics are not implemented");
        mark(result.model.databasePath, result.model.storageVersion == "7.82" ? ReadLevel::PartialSemantic : ReadLevel::Semantic,
             result.model.storageVersion == "7.82" ? "7.82 object graph decoded; individual bolt geometry and non-plate contours remain partial" : "");

        const auto library = findFile(root, "xslib.db1");
        if (!library.empty() && options.readComponentLibrary)
        {
            db1::Model model; std::string diagnostic;
            if (db1::parseComponentLibrary(library, model, diagnostic, options.modelOptions))
            {
                if (!model.databaseGuid.empty() && !result.model.databaseGuid.empty() && model.databaseGuid != result.model.databaseGuid)
                    result.diagnostics.push_back("component library database GUID differs from main model");
                for (const auto& item : model.diagnostics) result.diagnostics.push_back("xslib: " + item);
                result.componentLibrary = std::move(model);
                mark(library, result.componentLibrary->storageVersion == "7.82" ? ReadLevel::PartialSemantic : ReadLevel::Semantic,
                     result.componentLibrary->storageVersion == "7.82" ? "7.82 objects and parameters decoded; custom definition tables remain unnamed" : "");
                result.associations.push_back({result.model.databasePath, library, "model-local component library"});
            }
            else failure(library, diagnostic);
        }
        if (options.readRawCompanions)
        {
            // Copy the work list: mark() may append files and invalidate references.
            const auto files = result.files;
            for (const auto& file : files)
                if (file.role == FileRole::Numbering || file.role == FileRole::Environment || file.role == FileRole::Options)
                {
                    db1::RawDatabase raw; std::string diagnostic;
                    if (!db1::parseRawDatabase(file.path, raw, diagnostic, options.rawOptions)) { failure(file.path, diagnostic); continue; }
                    mark(file.path, ReadLevel::Raw, "container only; stable semantic field mapping is not implemented");
                    if (file.role == FileRole::Numbering)
                    {
                        const auto stem = lower(db1::detail::pathUtf8(file.path.stem()));
                        if (!raw.databaseGuid.empty() && !result.model.databaseGuid.empty() && lower(raw.databaseGuid)!=lower(result.model.databaseGuid))
                            result.diagnostics.push_back("numbering database GUID differs from main model: "+db1::detail::pathUtf8(file.path));
                        else if (stem == lower(db1::detail::pathUtf8(result.model.databasePath.stem())))
                            result.associations.push_back({result.model.databasePath, file.path, "matching DB1/DB2 basename and no conflicting database GUID"});
                        else if (stem == "xslib" && !library.empty())
                            result.associations.push_back({library, file.path, "component DB1/DB2 pair; numbering fields remain raw"});
                        else result.diagnostics.push_back("unpaired numbering database: " + db1::detail::pathUtf8(file.path));
                    }
                    result.rawCompanions.emplace(file.path, std::move(raw));
                }
        }

        const auto companions=result.files;
        std::map<std::string,std::vector<std::uint32_t>> modelIdsByGuid;
        for (const auto& entry:result.model.identities)
            if (!entry.second.guid.empty()) modelIdsByGuid[lower(entry.second.guid)].push_back(entry.first);
        for (const auto& file:companions)
        {
            if (file.role==FileRole::Environment && options.readEnvironment)
            {
                EnvironmentDatabase environment; std::string diagnostic;
                if (!parseEnvironmentDatabase(file.path,environment,diagnostic,options.rawOptions)) { failure(file.path,diagnostic); continue; }
                if (result.environment) throw std::runtime_error("ambiguous project environment database");
                for (const auto& item:environment.diagnostics) result.diagnostics.push_back(db1::detail::pathUtf8(file.path)+": "+item);
                mark(file.path,ReadLevel::PartialSemantic,"attribute definitions, classes and integer choices decoded; metadata flags remain raw");
                result.associations.push_back({result.model.databasePath,file.path,"model-local attribute definitions; object class applicability is not inferred"});
                result.environment=std::move(environment);
            }
            if (file.role==FileRole::Options && options.readOptions)
            {
                OptionsDatabase database; std::string diagnostic;
                if (!parseOptionsDatabase(file.path,database,diagnostic,options.rawOptions)) { failure(file.path,diagnostic); continue; }
                for (const auto& item:database.diagnostics) result.diagnostics.push_back(db1::detail::pathUtf8(file.path)+": "+item);
                mark(file.path,ReadLevel::PartialSemantic,"typed option slots decoded; effective/default precedence unverified");
                result.associations.push_back({result.model.databasePath,file.path,"model-local stored options; no effective-value precedence inferred"});
                result.optionsDatabases.emplace(file.path,std::move(database));
            }
            if (file.role==FileRole::Numbering && options.readNumbering)
            {
                NumberingDatabase numbering; std::string diagnostic;
                if (!parseNumberingDatabase(file.path,numbering,diagnostic,options.rawOptions)) { failure(file.path,diagnostic); continue; }
                for (const auto& item:numbering.diagnostics) result.diagnostics.push_back(db1::detail::pathUtf8(file.path)+": "+item);
                mark(file.path,ReadLevel::PartialSemantic,"numbering series/counters decoded; comparison snapshots remain raw");
                // Association also works when raw companions were explicitly disabled.
                if (!options.readRawCompanions)
                {
                    const auto stem=lower(db1::detail::pathUtf8(file.path.stem()));
                    const auto guidMatches=numbering.raw.databaseGuid.empty() || result.model.databaseGuid.empty() || lower(numbering.raw.databaseGuid)==lower(result.model.databaseGuid);
                    if (stem==lower(db1::detail::pathUtf8(result.model.databasePath.stem())) && guidMatches)
                        result.associations.push_back({result.model.databasePath,file.path,"matching DB1/DB2 basename and no conflicting database GUID"});
                    else if (!guidMatches) result.diagnostics.push_back("numbering database GUID differs from main model: "+db1::detail::pathUtf8(file.path));
                }
                result.numbering.emplace(file.path,std::move(numbering));
            }
            if (file.role==FileRole::Drawing && options.readDrawings)
            {
                Drawing drawing; std::string diagnostic;
                if (!parseDrawing(file.path,drawing,diagnostic,options.rawOptions)) { failure(file.path,diagnostic); continue; }
                if (!drawing.projectGuid.empty() && !result.model.databaseGuid.empty() && lower(drawing.projectGuid)==lower(result.model.databaseGuid))
                {
                    result.associations.push_back({result.model.databasePath,file.path,"DG grProjectGuid matches model database GUID"});
                    if (drawing.subject)
                    {
                        const auto& subject=*drawing.subject;
                        const auto found=modelIdsByGuid.find(lower(subject.modelGuid));
                        if (found!=modelIdsByGuid.end() && found->second.size()==1)
                        {
                            const auto id=found->second.front();
                            const auto matches=subject.kind==DrawingSubjectKind::Unknown ||
                                (subject.kind==DrawingSubjectKind::SinglePart && result.model.parts.count(id)) ||
                                (subject.kind==DrawingSubjectKind::Assembly && std::any_of(result.model.assemblies.begin(),result.model.assemblies.end(),
                                    [&](const auto& assembly) { return assembly.id==id; }));
                            if (matches) result.drawingSubjectAssociations.push_back({file.path,subject.recordId,id,subject.modelGuid});
                            else result.diagnostics.push_back("drawing subject type differs from matching model object: "+subject.modelGuid);
                        }
                        else result.diagnostics.push_back("drawing subject GUID is unresolved or ambiguous: "+subject.modelGuid);
                    }
                    for (const auto& reference:drawing.modelReferences)
                    {
                        const auto found=modelIdsByGuid.find(lower(reference.modelGuid));
                        if (found!=modelIdsByGuid.end() && found->second.size()==1)
                            result.drawingModelAssociations.push_back({file.path,reference.recordId,found->second.front(),reference.modelGuid,reference.drawingContextId});
                        else result.diagnostics.push_back("drawing model GUID is unresolved or ambiguous: "+reference.modelGuid);
                    }
                }
                else result.diagnostics.push_back("drawing project GUID is missing or differs from main model: "+db1::detail::pathUtf8(file.path));
                for (const auto& item:drawing.diagnostics) result.diagnostics.push_back(db1::detail::pathUtf8(file.path)+": "+item);
                mark(file.path,ReadLevel::PartialSemantic,"drawing subject, view bases/volumes, properties, strings and sheet size decoded; graphics remain raw");
                result.drawings.emplace(file.path,std::move(drawing));
            }
        }

        if (result.environment)
        {
            std::map<std::string,const AttributeDefinition*> definitions;
            for (const auto& entry:result.environment->attributes) definitions.emplace(entry.second.name,&entry.second);
            for (const auto& entry:result.model.properties)
                for (std::size_t i=0;i<entry.second.size();++i)
                {
                    const auto& property=entry.second[i]; const auto found=definitions.find(property.name);
                    if (found==definitions.end()) continue;
                    const auto kind=property.kind==db1::Property::Kind::Integer ? StoredValueKind::Integer :
                        property.kind==db1::Property::Kind::Double ? StoredValueKind::Real : StoredValueKind::String;
                    if (kind==found->second->storageKind) result.attributeDefinitionAssociations.push_back({entry.first,i,found->second->id});
                    else result.diagnostics.push_back("model attribute storage type differs from environment definition: "+property.name);
                }
            std::sort(result.attributeDefinitionAssociations.begin(),result.attributeDefinitionAssociations.end(),[](const auto& a,const auto& b) {
                return std::tie(a.modelObjectId,a.propertyIndex)<std::tie(b.modelObjectId,b.propertyIndex);
            });
        }

        std::vector<std::filesystem::path> roots{root};
        for (const auto& item : options.resourceDirectories)
        {
            const auto path = std::filesystem::absolute(item.is_absolute() ? item : root / item).lexically_normal();
            if (!std::filesystem::is_directory(path)) { failure(path, "resource directory does not exist"); continue; }
            if (std::find(roots.begin(), roots.end(), path) == roots.end()) roots.push_back(path);
        }
        for (const auto& resourceRoot : roots)
        {
            const auto load = [&](const std::string& name, auto& destination, auto parser) {
                if (destination) return;
                const auto path = findFile(resourceRoot, name); if (path.empty()) return;
                typename std::decay_t<decltype(destination)>::value_type value; std::string diagnostic;
                if (!parser(path, value, diagnostic)) { failure(path, diagnostic); return; }
                destination = std::move(value); mark(path, ReadLevel::Semantic);
                result.associations.push_back({result.model.databasePath, path, "resource search precedence"});
            };
            load("matdb.bin", result.materials, db1::parseMaterialCatalog);
            load("screwdb.db", result.bolts, db1::parseBoltCatalog);
            load("assdb.db", result.boltAssemblies, db1::parseBoltAssemblyCatalog);
            load("profitab.inp", result.profileRules, db1::parseProfitabCatalog);
            for (const auto* name : {"pgdb.bin", "profdb.bin"})
            {
                const auto path = findFile(resourceRoot, name); if (path.empty()) continue;
                std::string diagnostic;
                if (std::string(name) == "pgdb.bin")
                {
                    db1::ProfileGeometryCatalog catalog;
                    if (!db1::parseProfileGeometryCatalog(path, catalog, diagnostic)) { failure(path, diagnostic); continue; }
                    for (const auto& entry : catalog.profiles)
                    {
                        db1::Profile profile; profile.type = 998; profile.name = entry.first;
                        profile.source = db1::ProfileSource::SketchSolver;
                        if (!entry.second.contour.empty()) profile.fixedContours.push_back(entry.second.contour);
                        result.model.profiles.emplace(entry.first, std::move(profile));
                    }
                    for (const auto& item : catalog.diagnostics) result.diagnostics.push_back(db1::detail::pathUtf8(path) + ": " + item);
                }
                else
                {
                    db1::ProfileCatalog catalog;
                    if (!db1::parseProfileCatalog(path, catalog, diagnostic)) { failure(path, diagnostic); continue; }
                    for (auto& entry : catalog.profiles) result.model.profiles.emplace(entry.first, std::move(entry.second));
                }
                mark(path, ReadLevel::Semantic);
                result.associations.push_back({result.model.databasePath, path, "profile name lookup; earlier resources take precedence"});
            }
            for (const auto* name : {"Shapes", "ShapeGeometries"})
            {
                const auto path = resourceRoot / name;
                if (!std::filesystem::is_directory(path)) continue;
                db1::ShapeCatalog catalog; std::string diagnostic;
                if (!db1::parseShapeCatalog(path, catalog, diagnostic)) { failure(path, diagnostic); continue; }
                // Geometry may be in the other sibling directory; validate after merging.
                for (auto& entry : catalog.definitionsByGuid) result.shapes.definitionsByGuid.emplace(entry.first, std::move(entry.second));
                for (auto& entry : catalog.geometriesByStorageId) result.shapes.geometriesByStorageId.emplace(entry.first, std::move(entry.second));
                for (const auto& item : catalog.diagnostics)
                    if (item.find("references missing geometry") == std::string::npos)
                        result.diagnostics.push_back(db1::detail::pathUtf8(path) + ": " + item);
                mark(path, ReadLevel::Semantic);
            }
        }
        const auto linkNumberingSeries = [&](const db1::Model& model) {
            if (!options.readNumbering || model.objectNumberingRecords.empty()) return;
            const auto pair = findFile(root, db1::detail::pathUtf8(model.databasePath.stem()) + ".db2");
            const auto found = result.numbering.find(pair);
            if (found==result.numbering.end())
            {
                result.diagnostics.push_back("object numbering has no parsed DB2 pair: " + db1::detail::pathUtf8(model.databasePath));
                return;
            }
            const auto& database = found->second;
            if (model.storageVersion!=database.raw.storageVersion)
            {
                result.diagnostics.push_back("object numbering DB1/DB2 storage version differs: " + db1::detail::pathUtf8(model.databasePath));
                return;
            }
            const bool legacyPair = options.trustLegacyNumberingBasenames && model.storageVersion=="7.82" && database.raw.storageVersion=="7.82" && model.databaseGuid.empty() && database.raw.databaseGuid.empty();
            if (!legacyPair && (model.databaseGuid.empty() || database.raw.databaseGuid.empty() || lower(model.databaseGuid)!=lower(database.raw.databaseGuid)))
            {
                result.diagnostics.push_back("object numbering DB1/DB2 GUID scope is unverified: " + db1::detail::pathUtf8(model.databasePath));
                return;
            }
            std::map<std::pair<std::string,std::uint32_t>,std::vector<std::size_t>> indices;
            for (std::size_t i=0; i<database.series.size(); ++i)
                indices[{database.series[i].prefix,database.series[i].startNumber}].push_back(i);
            std::size_t unresolved = 0;
            for (const auto& entry : model.objectNumberingReferences)
            {
                const auto record = model.objectNumberingRecords.find(entry.second.numberingRecordId);
                if (record==model.objectNumberingRecords.end() || record->second.kind==db1::ObjectNumberingKind::Unverified) continue;
                const auto series = indices.find({record->second.prefix,record->second.startNumber});
                if (series==indices.end() || series->second.size()!=1) { ++unresolved; continue; }
                result.objectNumberingSeriesAssociations.push_back({model.databasePath,pair,entry.first,record->first,series->second.front(),legacyPair?NumberingPairEvidence::ExplicitLegacyBasename:NumberingPairEvidence::DatabaseGuid});
            }
            if (unresolved) result.diagnostics.push_back(std::to_string(unresolved) + " object numbering references have missing or ambiguous DB2 series: " + db1::detail::pathUtf8(model.databasePath));
        };
        linkNumberingSeries(result.model);
        if (result.componentLibrary) linkNumberingSeries(*result.componentLibrary);
        std::sort(result.objectNumberingSeriesAssociations.begin(),result.objectNumberingSeriesAssociations.end(),[](const auto& a,const auto& b) {
            return std::tie(a.database,a.objectId) < std::tie(b.database,b.objectId);
        });

        const auto linkSurfaceMaterials = [&](const db1::Model& model) {
            if (!result.materials)
            {
                if (!model.surfaceTreatments.empty()) result.diagnostics.push_back("surface treatments have no material catalog: " + db1::detail::pathUtf8(model.databasePath));
                return;
            }
            for (const auto& entry : model.surfaceTreatments)
            {
                const auto& definition = model.surfaceTreatmentDefinitions.at(entry.second.definitionId);
                std::size_t count = 0, matched = 0;
                for (std::size_t i=0; i<result.materials->materials.size(); ++i)
                    if (result.materials->materials[i].name == definition.material) { ++count; matched = i; }
                if (count == 1) result.surfaceMaterialAssociations.push_back({model.databasePath, entry.first, matched});
                else result.diagnostics.push_back("missing or ambiguous surface material '" + definition.material + "' for " +
                    db1::detail::pathUtf8(model.databasePath) + ":" + std::to_string(entry.first));
            }
        };
        linkSurfaceMaterials(result.model);
        if (result.componentLibrary) linkSurfaceMaterials(*result.componentLibrary);
        std::sort(result.surfaceMaterialAssociations.begin(),result.surfaceMaterialAssociations.end(),[](const auto& a,const auto& b) {
            return std::tie(a.database,a.surfaceTreatmentId)<std::tie(b.database,b.surfaceTreatmentId);
        });
        for (const auto& entry : result.shapes.definitionsByGuid)
            if (!result.shapes.geometriesByStorageId.count(entry.second.brepStorageId))
                result.diagnostics.push_back("shape " + entry.second.name + " references missing geometry " + entry.second.brepStorageId);
        if (result.profileRules)
            for (const auto& item : result.profileRules->diagnostics) result.diagnostics.push_back("profitab: " + item);
        std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
        std::sort(result.drawingModelAssociations.begin(),result.drawingModelAssociations.end(),[](const auto& a,const auto& b) {
            return std::tie(a.drawing,a.drawingRecordId,a.modelObjectId)<std::tie(b.drawing,b.drawingRecordId,b.modelObjectId);
        });
        std::sort(result.drawingSubjectAssociations.begin(),result.drawingSubjectAssociations.end(),[](const auto& a,const auto& b) {
            return std::tie(a.drawing,a.drawingRecordId)<std::tie(b.drawing,b.drawingRecordId);
        });
        std::sort(result.associations.begin(), result.associations.end(), [](const auto& a, const auto& b) {
            return std::tie(a.source, a.target, a.reason) < std::tie(b.source, b.target, b.reason);
        });
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what(); result = {}; return false;
    }
}
}
