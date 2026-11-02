#include <tekla/Project.hpp>
#include "Path.hpp"
#include "ShapeCatalogPrivate.hpp"
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
    if (name == "teklastructuresmodel.xml") return FileRole::ModelMetadata;
    if (name == "xslib.db1") return FileRole::ComponentLibrary;
    if (ext == ".db1") return FileRole::Model;
    if (ext == ".db2") return FileRole::Numbering;
    if (ext == ".db6") return FileRole::Analysis;
    if (name == "analysis_results.db5") return FileRole::AnalysisResults;
    if (ext == ".db3") return FileRole::PlanDatabase;
    if (ext == ".admodel") return FileRole::AnalysisSettings;
    if (ext == ".dg") return FileRole::Drawing;
    if (ext == ".metadata" && lower(db1::detail::pathUtf8(path.stem().extension()))==".dg") return FileRole::DrawingMetadata;
    if (name == "environment.db") return FileRole::Environment;
    if (name == "options_model.db" || name == "options_drawings.db" || name == "options.ini") return FileRole::Options;
    if (name == "history.db") return FileRole::History;
    if (name == "guid.mapper") return FileRole::IdentityMapping;
    if (name == "db.idrm" || name == "xslib.idrm") return FileRole::IdRangeMapper;
    constexpr char groupSuffix[] = ".shapecatalog.groups.xml";
    const bool userGroupFile = name.rfind("shapecatalog.groups.user.", 0) == 0 && ext == ".xml";
    const bool namedGroupFile = name == "shapecatalog.groups.xml" || userGroupFile ||
        (name.size() > sizeof(groupSuffix) - 1 &&
         name.compare(name.size() - (sizeof(groupSuffix) - 1), sizeof(groupSuffix) - 1, groupSuffix) == 0);
    if (name == "rebarshaperules.xml" || namedGroupFile || ext == ".shapecatalog") return FileRole::Catalog;
    if (name == "profdb.bin" || name == "pgdb.bin" || name == "matdb.bin" || name == "screwdb.db" ||
        name == "assdb.db" || name == "profitab.inp" || name == "rebar_database.inp" ||
        name == "mesh_database.inp" || name == "rebar_schedule_config.inp" || name == "rebarshapecatalog.rscx" ||
        ext == ".clb" || name == "shapes" || name == "shapegeometries") return FileRole::Catalog;
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
        db1::ModelReadOptions modelOptions = options.modelOptions;
        std::map<std::filesystem::path, IdRangeMapperFile> preloadedIdRangeMappers;
        std::map<std::filesystem::path, std::string> idRangeMapperErrors;
        if (options.readIdRangeMappers)
        {
            for (const auto& entry : std::filesystem::directory_iterator(root))
                if (entry.is_regular_file() && roleFor(entry.path()) == FileRole::IdRangeMapper)
                {
                    const auto path = std::filesystem::absolute(entry.path()).lexically_normal();
                    IdRangeMapperFile mapper;
                    std::string diagnostic;
                    if (!parseIdRangeMapperFile(path, mapper, diagnostic,
                        (std::min)(std::size_t{64 * 1024 * 1024}, options.rawOptions.maxDecodedBytes)))
                    {
                        idRangeMapperErrors.emplace(path, std::move(diagnostic));
                        continue;
                    }
                    for (const auto& record : mapper.records)
                        if (std::find(modelOptions.idRangeMapperNamespaceGuids.begin(),
                                      modelOptions.idRangeMapperNamespaceGuids.end(),
                                      record.namespaceGuidBytes) == modelOptions.idRangeMapperNamespaceGuids.end())
                            modelOptions.idRangeMapperNamespaceGuids.push_back(record.namespaceGuidBytes);
                    preloadedIdRangeMappers.emplace(path, std::move(mapper));
                }
        }
        const auto ok = options.mainDatabase.empty()
            ? db1::parseModelDirectory(root, result.model, error, modelOptions)
            : db1::parseModelFile(options.mainDatabase.is_absolute() ? options.mainDatabase : root / options.mainDatabase, result.model, error, modelOptions);
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
        const auto unparsedCatalogDiagnostic = [&](const std::filesystem::path& path) -> std::string {
            const auto name = lower(db1::detail::pathUtf8(path.filename()));
            if (name == "rebarshaperules.xml")
                return "rebar shape recognition rules discovered; semantic parsing is not implemented";
            if (name == "shapecatalog.groups.xml" ||
                (name.rfind("shapecatalog.groups.user.", 0) == 0 &&
                 lower(db1::detail::pathUtf8(path.extension())) == ".xml") ||
                (name.size() > std::string(".shapecatalog.groups.xml").size() &&
                 name.compare(name.size() - std::string(".shapecatalog.groups.xml").size(),
                              std::string(".shapecatalog.groups.xml").size(), ".shapecatalog.groups.xml") == 0))
                return "shape catalog group structure discovered; semantic parsing is not implemented";
            if (lower(db1::detail::pathUtf8(path.extension())) == ".shapecatalog")
                return "shape catalog exchange package discovered; semantic parsing is not implemented";
            return {};
        };
        for (const auto& entry : std::filesystem::directory_iterator(root))
            if (entry.is_regular_file())
            {
                const auto role = roleFor(entry.path());
                if (role == FileRole::Other) continue;
                const auto name=lower(db1::detail::pathUtf8(entry.path().filename()));
                if (role == FileRole::ModelMetadata)
                {
                    const auto path = std::filesystem::absolute(entry.path()).lexically_normal();
                    const auto expected = std::filesystem::absolute(root / "TeklaStructuresModel.xml").lexically_normal();
                    std::error_code equivalentError;
                    const bool selectedByModel =
                        std::filesystem::equivalent(path, expected, equivalentError) && !equivalentError &&
                        !result.model.metadata.rawXml.empty();
                    if (selectedByModel)
                    {
                        mark(path, ReadLevel::PartialSemantic,
                             "13 model metadata fields decoded; unknown XML extensions retained raw");
                        result.associations.push_back({path, result.model.databasePath,
                            "root TeklaStructuresModel.xml was parsed with the selected DB1; fields are available in Model::metadata"});
                    }
                    else
                        mark(path, ReadLevel::Discovered,
                             "model metadata file was not selected by the main DB1 metadata reader");
                    continue;
                }
                if (role == FileRole::History && options.readHistory)
                {
                    HistoryDatabase history;
                    std::string historyError;
                    if (!parseHistoryDatabase(entry.path(), history, historyError)) failure(entry.path(), historyError);
                    else
                    {
                        const auto path = std::filesystem::absolute(entry.path()).lexically_normal();
                        result.historyDatabases.emplace(path, std::move(history));
                        mark(entry.path(), ReadLevel::PartialSemantic, "SQLite history tables decoded; version_data remains opaque");
                    }
                    continue;
                }
                mark(entry.path(), ReadLevel::Discovered,
                     role == FileRole::History ? "SQLite history parsing disabled by project options" :
                     role == FileRole::AnalysisResults ? "DB5 saved analysis results discovered; semantic parsing is not implemented" :
                     role == FileRole::PlanDatabase ? "DB3 plan database discovered; semantic parsing is not implemented" :
                     name=="rebar_database.inp" ? "rebar catalog discovered; semantic parsing is not implemented" :
                     name=="mesh_database.inp" ? "reinforcement mesh catalog discovered; semantic parsing is not implemented" :
                     unparsedCatalogDiagnostic(entry.path()));
            }
        const auto drawings = root / "drawings";
        if (std::filesystem::is_directory(drawings))
            for (const auto& entry : std::filesystem::directory_iterator(drawings))
                if (entry.is_regular_file() && (roleFor(entry.path()) == FileRole::Drawing || roleFor(entry.path()) == FileRole::DrawingMetadata))
                    mark(entry.path(), ReadLevel::Discovered, "DG reading is optional; full drawing semantics are not implemented");
        // Directory spelling is case-insensitive, including on Linux. Keep
        // paths as separate inventories; no basename-based DB1 association.
        for (const auto& entry : std::filesystem::directory_iterator(root))
            if (entry.is_directory())
            {
                const auto directoryName=lower(db1::detail::pathUtf8(entry.path().filename()));
                if(directoryName=="analysis" || directoryName=="attributes")
                    for (const auto& file : std::filesystem::directory_iterator(entry.path()))
                        if(file.is_regular_file() &&
                            ((directoryName=="analysis" && (roleFor(file.path())==FileRole::Analysis || roleFor(file.path())==FileRole::AnalysisResults || roleFor(file.path())==FileRole::PlanDatabase)) ||
                             (directoryName=="attributes" && (roleFor(file.path())==FileRole::AnalysisSettings ||
                                                              lower(db1::detail::pathUtf8(file.path().filename()))=="rebarshapecatalog.rscx"))))
                            mark(file.path(),ReadLevel::Discovered,
                                 roleFor(file.path())==FileRole::AnalysisResults ?
                                     "DB5 saved analysis results discovered; semantic parsing is not implemented" :
                                     roleFor(file.path())==FileRole::PlanDatabase ?
                                     "DB3 plan database discovered; semantic parsing is not implemented" :
                                     (roleFor(file.path())==FileRole::Catalog ? "" : "analysis data and presets are not automatically associated with model objects"));
            }
        const auto modelLevel = [](const db1::Model& model) {
            return model.storageVersion == "7.64" || model.storageVersion == "7.82" || model.storageVersion == "8.37" ||
                   model.storageVersion == "9.08" ? ReadLevel::PartialSemantic : ReadLevel::Semantic;
        };
        const auto modelDiagnostic = [](const db1::Model& model) -> std::string {
            if (model.storageVersion == "7.64" || model.storageVersion == "7.82") return model.storageVersion + " object graph decoded; individual bolt geometry and non-plate contours remain partial";
            if (model.storageVersion == "8.37") return "8.37 common legacy object graph decoded; bolt tables and unverified table roles remain partial";
            if (model.storageVersion == "9.08") return "9.08 object graph decoded; bolt numeric parameters, generation flags and complete geometry remain partial";
            return {};
        };
        mark(result.model.databasePath, modelLevel(result.model), modelDiagnostic(result.model));

        const auto library = findFile(root, "xslib.db1");
        if (!library.empty() && options.readComponentLibrary)
        {
            db1::Model model; std::string diagnostic;
            if (db1::parseComponentLibrary(library, model, diagnostic, modelOptions))
            {
                if (!model.databaseGuid.empty() && !result.model.databaseGuid.empty() && model.databaseGuid != result.model.databaseGuid)
                    result.diagnostics.push_back("component library database GUID differs from main model");
                for (const auto& item : model.diagnostics) result.diagnostics.push_back("xslib: " + item);
                result.componentLibrary = std::move(model);
                mark(library, modelLevel(*result.componentLibrary),
                     result.componentLibrary->storageVersion == "7.82" ? "7.82 objects and parameters decoded; custom definition tables remain unnamed" : modelDiagnostic(*result.componentLibrary));
                result.associations.push_back({result.model.databasePath, library, "model-local component library"});
            }
            else if (options.readRawCompanions)
            {
                db1::RawDatabase raw;
                std::string rawDiagnostic;
                if (db1::parseRawDatabase(library, raw, rawDiagnostic, options.rawOptions) &&
                    raw.kind == db1::DatabaseKind::ComponentLibrary)
                {
                    result.diagnostics.push_back("xslib semantic projection unavailable: " + diagnostic);
                    mark(library, ReadLevel::Raw, "component library container retained; semantic projection unavailable");
                    result.rawCompanions.emplace(std::filesystem::absolute(library).lexically_normal(), std::move(raw));
                }
                else failure(library, diagnostic + "; raw fallback failed: " + rawDiagnostic);
            }
            else failure(library, diagnostic);
        }
        if (options.readRawCompanions)
        {
            // Copy the work list: mark() may append files and invalidate references.
            const auto files = result.files;
            for (const auto& file : files)
                if (file.role == FileRole::Numbering || file.role == FileRole::Environment || file.role == FileRole::Analysis ||
                    (file.role == FileRole::Options && lower(db1::detail::pathUtf8(file.path.filename()))!="options.ini"))
                {
                    db1::RawDatabase raw; std::string diagnostic;
                    if (!db1::parseRawDatabase(file.path, raw, diagnostic, options.rawOptions)) { failure(file.path, diagnostic); continue; }
                    if (file.role==FileRole::Analysis && raw.kind!=db1::DatabaseKind::Analysis)
                    { failure(file.path,"unsupported DB6 analysis container"); continue; }
                    mark(file.path, ReadLevel::Raw, "container only; stable semantic field mapping is not implemented");
                    if (file.role == FileRole::Numbering)
                    {
                        const auto stem = lower(db1::detail::pathUtf8(file.path.stem()));
                        const auto mainStem=lower(db1::detail::pathUtf8(result.model.databasePath.stem()));
                        const bool guidConflictsMain=!raw.databaseGuid.empty() && !result.model.databaseGuid.empty() &&
                            lower(raw.databaseGuid)!=lower(result.model.databaseGuid);
                        if (stem==mainStem)
                        {
                            if(guidConflictsMain)
                                result.diagnostics.push_back("numbering database GUID differs from main model: "+db1::detail::pathUtf8(file.path));
                            else result.associations.push_back({result.model.databasePath,file.path,
                                "matching DB1/DB2 basename and no conflicting database GUID"});
                        }
                        else if (stem=="xslib" && !library.empty())
                            result.associations.push_back({library,file.path,"component DB1/DB2 pair; numbering fields remain raw"});
                        else
                        {
                            auto siblingDb1=file.path;siblingDb1.replace_extension(".db1");
                            bool pairedSecondary=false;
                            if(std::filesystem::is_regular_file(siblingDb1) && roleFor(siblingDb1)==FileRole::Model &&
                               !raw.databaseGuid.empty())
                            {
                                db1::RawDatabase sibling;std::string siblingDiagnostic;
                                if(db1::parseRawDatabase(siblingDb1,sibling,siblingDiagnostic,options.rawOptions) &&
                                   sibling.kind==db1::DatabaseKind::Model && !sibling.databaseGuid.empty() &&
                                   lower(sibling.databaseGuid)==lower(raw.databaseGuid))
                                {
                                    result.associations.push_back({siblingDb1,file.path,
                                        "secondary DB1/DB2 basename and database GUID match"});
                                    pairedSecondary=true;
                                }
                            }
                            if(!pairedSecondary)
                            {
                                if(guidConflictsMain)
                                    result.diagnostics.push_back("numbering database GUID differs from main model: "+db1::detail::pathUtf8(file.path));
                                else result.diagnostics.push_back("unpaired numbering database: "+db1::detail::pathUtf8(file.path));
                            }
                        }
                    }
                    result.rawCompanions.emplace(file.path, std::move(raw));
                }
        }

        const auto companions=result.files;
        if(options.readOptions)(void)findFile(root,"options.ini"); // reject ambiguous case variants
        if (options.readGuidMappings)
        {
            const auto mapperPath = findFile(root, "guid.mapper");
            if (!mapperPath.empty())
            {
                GuidMappingFile mappings; std::string diagnostic;
                if (!parseGuidMappingFile(mapperPath, mappings, diagnostic,
                    (std::min)(std::size_t{64 * 1024 * 1024}, options.rawOptions.maxDecodedBytes)))
                {
                    failure(mapperPath, diagnostic);
                }
                else
                {
                    result.guidMappingTargets = matchGuidMappingTargets(mappings, result.model);
                    for (const auto& message : mappings.diagnostics) result.diagnostics.push_back(message);
                    result.guidMappings = std::move(mappings);
                    mark(mapperPath, ReadLevel::PartialSemantic,
                         "ordered GUID remapping batches; no automatic historical identity replacement");
                    if (!result.guidMappingTargets.empty())
                        result.associations.push_back({mapperPath, result.model.databasePath,
                            "exact unique target GUID matches in main model"});
                }
            }
        }
        if (options.readIdRangeMappers)
        {
            for (const auto& file : companions)
                if (file.role == FileRole::IdRangeMapper)
                {
                    IdRangeMapperFile mapper;
                    const auto path = std::filesystem::absolute(file.path).lexically_normal();
                    const auto preload = preloadedIdRangeMappers.find(path);
                    if (preload != preloadedIdRangeMappers.end())
                        mapper = preload->second;
                    else if (const auto failed = idRangeMapperErrors.find(path); failed != idRangeMapperErrors.end())
                    {
                        failure(file.path, failed->second);
                        continue;
                    }
                    else
                    {
                        std::string diagnostic;
                        if (!parseIdRangeMapperFile(file.path, mapper, diagnostic,
                            (std::min)(std::size_t{64 * 1024 * 1024}, options.rawOptions.maxDecodedBytes)))
                        {
                            failure(file.path, diagnostic);
                            continue;
                        }
                    }
                    for (const auto& message : mapper.diagnostics)
                        result.diagnostics.push_back(db1::detail::pathUtf8(file.path) + ": " + message);
                    mark(file.path, ReadLevel::PartialSemantic,
                         "static ranges and allocation-preamble namespace links decoded; object-ID conversion remains unverified");
                    const auto matches = [&](const db1::Model& model, const IdRangeMapperRecord& record) {
                        return std::find(model.matchedIdRangeMapperNamespaceGuids.begin(),
                                         model.matchedIdRangeMapperNamespaceGuids.end(),
                                         record.namespaceGuidBytes) != model.matchedIdRangeMapperNamespaceGuids.end();
                    };
                    std::set<std::filesystem::path> matchedDatabases;
                    for (const auto& record : mapper.records)
                    {
                        const bool mainMatch = matches(result.model, record);
                        const bool libraryMatch = result.componentLibrary && matches(*result.componentLibrary, record);
                        if (mainMatch && !libraryMatch) matchedDatabases.insert(result.model.databasePath);
                        else if (libraryMatch && !mainMatch) matchedDatabases.insert(result.componentLibrary->databasePath);
                        else if (mainMatch && libraryMatch)
                            result.diagnostics.push_back("IDRM namespace GUID is present in both main DB1 and xslib preambles; file association omitted");
                    }
                    for (const auto& database : matchedDatabases)
                        result.associations.push_back({file.path, database,
                            "IDRM namespace GUID bytes match this DB1 allocation preamble; static-to-runtime ID conversion is unverified"});
                    result.idRangeMappers.emplace(file.path, std::move(mapper));
                }
        }
        std::map<std::string,std::vector<std::uint32_t>> modelIdsByGuid;
        for (const auto& entry:result.model.identities)
            if (!entry.second.guid.empty()) modelIdsByGuid[lower(entry.second.guid)].push_back(entry.first);
        for (const auto& file:companions)
        {
            if(file.role==FileRole::AnalysisSettings && options.readAnalysisSettings)
            {
                AnalysisModelSettings settings;std::string diagnostic;
                if(!parseAnalysisModelSettings(file.path,settings,diagnostic,
                    (std::min)(std::size_t{4*1024*1024},options.rawOptions.maxDecodedBytes)))
                {failure(file.path,diagnostic);continue;}
                for(const auto& item:settings.diagnostics)result.diagnostics.push_back(db1::detail::pathUtf8(file.path)+": "+item);
                mark(file.path,ReadLevel::PartialSemantic,"named analysis preset values decoded; no effective-value or DB6 association inferred");
                result.analysisSettings.emplace(file.path,std::move(settings));
            }
            if(file.role==FileRole::DrawingMetadata && options.readDrawingMetadata)
            {
                DrawingVersionMetadata metadata;std::string diagnostic;
                if(!parseDrawingVersionMetadata(file.path,metadata,diagnostic,
                    (std::min)(std::size_t{16*1024*1024},options.rawOptions.maxDecodedBytes)))
                {failure(file.path,diagnostic);continue;}
                for(const auto& item:metadata.diagnostics)result.diagnostics.push_back(db1::detail::pathUtf8(file.path)+": "+item);
                mark(file.path,ReadLevel::PartialSemantic,"saved drawing version fields decoded; dates, flags and lifecycle precedence remain uninterpreted");
                result.drawingMetadata.emplace(file.path,std::move(metadata));
            }
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
                if(lower(db1::detail::pathUtf8(file.path.filename()))=="options.ini")
                {
                    if(result.optionSettings)throw std::runtime_error("ambiguous project option settings file");
                    OptionSettingsFile settings;std::string diagnostic;
                    if(!parseOptionSettingsFile(file.path,settings,diagnostic,(std::min)(std::size_t{4*1024*1024},options.rawOptions.maxDecodedBytes)))
                    {failure(file.path,diagnostic);continue;}
                    for(const auto& item:settings.diagnostics)result.diagnostics.push_back(db1::detail::pathUtf8(file.path)+": "+item);
                    mark(file.path,ReadLevel::PartialSemantic,"ordered option assignments decoded; effective precedence and unknown lines remain unresolved");
                    result.associations.push_back({result.model.databasePath,file.path,"model-local textual option settings; no runtime evaluation"});
                    result.optionSettings=std::move(settings);continue;
                }
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
                if (!parseDrawing(file.path,drawing,diagnostic,options.rawOptions))
                {
                    if (diagnostic.rfind("unsupported drawing semantic version ",0)==0)
                    {
                        db1::RawDatabase rawDrawing; std::string rawDiagnostic;
                        if (db1::parseRawDatabase(file.path,rawDrawing,rawDiagnostic,options.rawOptions) &&
                            rawDrawing.kind==db1::DatabaseKind::Drawing)
                        {
                            mark(file.path,ReadLevel::Raw,rawDrawing.storageVersion+" DG container retained; drawing field semantics are unavailable");
                            result.rawCompanions.emplace(file.path,std::move(rawDrawing));
                            continue;
                        }
                    }
                    failure(file.path,diagnostic); continue;
                }
                const bool drawingGuidMatches=!drawing.projectGuid.empty() && !result.model.databaseGuid.empty() &&
                    lower(drawing.projectGuid)==lower(result.model.databaseGuid);
                const bool explicitLegacyNumericScope=options.trustLegacyDrawingObjectIds &&
                    (drawing.raw.storageVersion=="7.30" || drawing.raw.storageVersion=="7.82") &&
                    drawing.raw.storageVersion==result.model.storageVersion &&
                    drawing.projectGuid.empty() && result.model.databaseGuid.empty();
                const bool directSubjectGuidCandidate=drawing.raw.storageVersion=="9.50" && drawing.subject &&
                    !drawing.subject->modelGuid.empty() &&
                    (drawing.subject->kind==DrawingSubjectKind::SinglePart || drawing.subject->kind==DrawingSubjectKind::Assembly);
                if (drawingGuidMatches || explicitLegacyNumericScope)
                {
                    result.associations.push_back({result.model.databasePath,file.path,drawingGuidMatches?
                        "DG grProjectGuid matches model database GUID":
                        "caller explicitly scopes GUID-less legacy drawing numeric IDs to this same-version model"});
                    // Verified exam DG layouts carry numeric IDs despite also
                    // storing a project GUID. Never pair these across versions.
                    const bool numericDrawing=drawing.raw.storageVersion=="8.95" || drawing.raw.storageVersion=="9.08" || explicitLegacyNumericScope;
                    const bool numericScope=(drawing.raw.storageVersion=="9.08" && drawing.raw.storageVersion==result.model.storageVersion) ||
                        explicitLegacyNumericScope;
                    const auto pairingEvidence=explicitLegacyNumericScope ? DrawingPairEvidence::ExplicitLegacyNumericScope :
                        DrawingPairEvidence::ProjectGuidAndObjectIdentity;
                    if(numericDrawing && !numericScope)result.diagnostics.push_back("numeric drawing/model version pairing is unverified: "+db1::detail::pathUtf8(file.path));
                    const auto resolveDrawingIdentity = [&](const std::string& guid,std::uint32_t numericId) -> std::optional<std::uint32_t> {
                        if(numericDrawing)
                        {
                            if(numericScope && numericId && result.model.identities.count(numericId))return numericId;
                            return {};
                        }
                        const auto found=modelIdsByGuid.find(lower(guid));
                        if(found!=modelIdsByGuid.end() && found->second.size()==1)return found->second.front();
                        return {};
                    };
                    if (drawing.subject)
                    {
                        const auto& subject=*drawing.subject;
                        if(subject.kind==DrawingSubjectKind::GeneralArrangement)
                        {
                            if(subject.modelObjectId)result.diagnostics.push_back("general arrangement drawing subject is not a single model object: "+std::to_string(subject.modelObjectId));
                        }
                        else
                        {
                            const auto found=resolveDrawingIdentity(subject.modelGuid,subject.modelObjectId);
                            if (found)
                            {
                                const auto id=*found;
                                const auto matches=subject.kind==DrawingSubjectKind::Unknown ||
                                    (subject.kind==DrawingSubjectKind::SinglePart && result.model.parts.count(id)) ||
                                    (subject.kind==DrawingSubjectKind::Assembly && std::any_of(result.model.assemblies.begin(),result.model.assemblies.end(),
                                        [&](const auto& assembly) { return assembly.id==id; }));
                                if (matches) result.drawingSubjectAssociations.push_back({file.path,subject.recordId,id,
                                    numericDrawing?result.model.identities.at(id).guid:subject.modelGuid,0,pairingEvidence});
                                else result.diagnostics.push_back(numericDrawing?
                                    "drawing subject type differs from matching model object: "+std::to_string(subject.modelObjectId):
                                    "drawing subject type differs from matching model object: "+subject.modelGuid);
                            }
                            else result.diagnostics.push_back(numericDrawing?"drawing subject numeric identity is unresolved: "+std::to_string(subject.modelObjectId):"drawing subject GUID is unresolved or ambiguous: "+subject.modelGuid);
                        }
                    }
                    for (const auto& reference:drawing.modelReferences)
                    {
                        const auto found=resolveDrawingIdentity(reference.modelGuid,reference.modelObjectId);
                        if (found)
                            result.drawingModelAssociations.push_back({file.path,reference.recordId,*found,
                                numericDrawing?result.model.identities.at(*found).guid:reference.modelGuid,reference.drawingContextId,pairingEvidence});
                        else result.diagnostics.push_back(numericDrawing?"drawing model numeric identity is unresolved: "+std::to_string(reference.modelObjectId):"drawing model GUID is unresolved or ambiguous: "+reference.modelGuid);
                    }
                    for (const auto& reference:drawing.legacyModelReferences)
                    {
                        const auto id=reference.modelObjectId;
                        const bool isActualPart=id && std::find(result.model.actualPartIds.begin(),result.model.actualPartIds.end(),id)!=result.model.actualPartIds.end();
                        if (explicitLegacyNumericScope && isActualPart && result.model.identities.count(id))
                            result.drawingModelAssociations.push_back({file.path,reference.recordId,id,
                                result.model.identities.at(id).guid,0,pairingEvidence});
                        else
                            result.diagnostics.push_back("legacy drawing type-309 model ID is unresolved or not an actual part: "+std::to_string(id));
                    }
                }
                else if (directSubjectGuidCandidate)
                {
                    // A stored subject GUID can identify one DB1 object even
                    // when grProjectGuid does not scope the whole drawing.
                    // Keep this narrow to the observed 9.50 subject layout:
                    // exact unique GUID, matching subject kind, and matching
                    // object-table membership. Do not pair the drawing file
                    // itself or any other model references on this evidence.
                    const auto& subject=*drawing.subject;
                    const auto found=modelIdsByGuid.find(lower(subject.modelGuid));
                    if(found!=modelIdsByGuid.end() && found->second.size()==1)
                    {
                        const auto id=found->second.front();
                        const bool objectKindMatches=subject.kind==DrawingSubjectKind::SinglePart ?
                            result.model.parts.count(id)!=0 :
                            std::any_of(result.model.assemblies.begin(),result.model.assemblies.end(),
                                [&](const auto& assembly){return assembly.id==id;});
                        if(objectKindMatches)
                            result.drawingSubjectAssociations.push_back({file.path,subject.recordId,id,subject.modelGuid,0,
                                DrawingPairEvidence::DirectSubjectGuidIdentity});
                        else
                            result.diagnostics.push_back("9.50 drawing subject GUID matches a DB1 identity but not the corresponding object table: "+subject.modelGuid);
                    }
                    else
                        result.diagnostics.push_back("9.50 drawing subject GUID is unresolved or ambiguous in the selected DB1: "+subject.modelGuid);
                    result.diagnostics.push_back("drawing project GUID differs from the selected DB1; only an exact unique 9.50 subject GUID match was considered: "+db1::detail::pathUtf8(file.path));
                }
                else result.diagnostics.push_back("drawing project GUID is missing or differs from main model: "+db1::detail::pathUtf8(file.path));
                for (const auto& item:drawing.diagnostics) result.diagnostics.push_back(db1::detail::pathUtf8(file.path)+": "+item);
                const auto drawingDiagnostic=drawing.raw.storageVersion=="9.50"?
                    "9.50 subject type/GUID, strings, properties and sheet decoded; views, dimensions and graphics remain raw":
                    (drawing.raw.storageVersion=="8.95" || drawing.raw.storageVersion=="9.08")?
                    "drawing numeric subject/references, properties, strings and sheet size decoded; views, dimensions and graphics remain raw":
                    (drawing.raw.storageVersion=="7.30" || drawing.raw.storageVersion=="7.82")?
                    "drawing subject, views, sheet and known straight dimension anchors decoded; ambiguous legacy IDs and graphics remain raw":
                    "drawing subject, view bases/volumes, properties, strings and sheet size decoded; graphics remain raw";
                mark(file.path,ReadLevel::PartialSemantic,drawingDiagnostic);
                result.drawings.emplace(file.path,std::move(drawing));
            }
        }

        // Compare sidecars only after DG parsing and its existing scoped model
        // associations. A metadata GUID must never repair a missing/wrong DG ID.
        std::map<std::string,std::size_t> drawingPathCounts,metadataPathCounts;
        const auto pathKey=[](const std::filesystem::path& p){return lower(db1::detail::pathUtf8(p.lexically_normal()));};
        for(const auto& file:companions)
        {
            if(file.role==FileRole::Drawing)++drawingPathCounts[pathKey(file.path)];
            if(file.role==FileRole::DrawingMetadata)++metadataPathCounts[pathKey(file.path.parent_path()/file.path.stem())];
        }
        for(const auto& entry:result.drawingMetadata)
        {
            const auto& metadata=entry.second;const auto key=pathKey(entry.first.parent_path()/entry.first.stem());
            const auto diagnostic=[&](const std::string& message){result.diagnostics.push_back(db1::detail::pathUtf8(entry.first)+": "+message);};
            if(drawingPathCounts[key]!=1 || metadataPathCounts[key]!=1)
            {diagnostic("drawing metadata filename pairing is missing or ambiguous");continue;}
            const auto found=std::find_if(result.drawings.begin(),result.drawings.end(),[&](const auto& d){return pathKey(d.first)==key;});
            if(found==result.drawings.end()){diagnostic("drawing metadata has no decoded DG companion");continue;}
            const auto& drawing=found->second;
            if(drawing.storedFileName.empty() || lower(drawing.storedFileName)!=lower(db1::detail::pathUtf8(found->first.filename())))
            {diagnostic("drawing metadata filename differs from DG stored filename");continue;}
            if(metadata.drawingGuid.empty() || !metadata.width || !metadata.height || drawing.sheets.size()!=1)
            {diagnostic("drawing metadata pairing lacks GUID or a unique sheet");continue;}
            if(*metadata.width!=drawing.sheets[0].width || *metadata.height!=drawing.sheets[0].height)
            {diagnostic("drawing metadata sheet dimensions conflict with DG");continue;}
            if(drawing.subject && (!metadata.drawingType || *metadata.drawingType!=drawing.subject->typeCode))
            {diagnostic("drawing metadata type conflicts with DG subject");continue;}
            DrawingMetadataAssociation association{entry.first,found->first,{}};
            association.evidence=DrawingMetadataAssociation::Evidence::FilenameAndSheet;
            if(!metadata.mainObjectGuid.empty())
            {
                const auto subject=std::find_if(result.drawingSubjectAssociations.begin(),result.drawingSubjectAssociations.end(),
                    [&](const auto& s){return s.drawing==found->first;});
                if(subject!=result.drawingSubjectAssociations.end() && lower(subject->modelGuid)==metadata.mainObjectGuid)
                {
                    association.modelObjectId=subject->modelObjectId;
                    association.evidence=DrawingMetadataAssociation::Evidence::SubjectIdentity;
                }
                else diagnostic("main object GUID retained without a matching scoped DG subject; file pairing uses filename and sheet only");
            }
            else if(drawing.subject && (drawing.subject->modelObjectId || !drawing.subject->modelGuid.empty()))
                diagnostic("drawing metadata omits the DG main object GUID; file pairing uses filename and sheet only");
            result.drawingMetadataAssociations.push_back(std::move(association));
            result.associations.push_back({entry.first,found->first,
                result.drawingMetadataAssociations.back().evidence==DrawingMetadataAssociation::Evidence::SubjectIdentity?
                    "DG metadata filename, saved sheet and scoped subject identity agree; no active-version inference":
                    "DG metadata filename and saved sheet match; no model-object identity inferred"});
        }

        if(result.optionSettings)
            for(const auto& database:result.optionsDatabases)
                for(auto match:matchOptionSettings(*result.optionSettings,database.second))
                    result.optionSettingAssociations.push_back({database.first,std::move(match)});

        for (const auto& history : result.historyDatabases)
            for (const auto& item : history.second.items)
            {
                if (item.category != 0) continue;
                const auto key = lower(historyGuidString(item.identifier));
                const auto found = modelIdsByGuid.find(key);
                if (found == modelIdsByGuid.end() || found->second.size() != 1) continue;
                const auto id = found->second.front();
                result.historyObjectAssociations.push_back({history.first, item.id, id,
                    result.model.identities.at(id).guid});
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
        const auto attributesRoot=root/"attributes";
        if(std::filesystem::is_directory(attributesRoot))roots.push_back(attributesRoot);
        for (const auto& item : options.resourceDirectories)
        {
            const auto path = std::filesystem::absolute(item.is_absolute() ? item : root / item).lexically_normal();
            if (!std::filesystem::is_directory(path)) { failure(path, "resource directory does not exist"); continue; }
            if (std::find(roots.begin(), roots.end(), path) == roots.end()) roots.push_back(path);
        }
        // Group files are separate shape-catalog resources. Model-level groups
        // were inventoried above; resource roots may also contain group files
        // next to the catalog or in one-level Shapes subfolders.
        for (const auto& resourceRoot : roots)
        {
            const auto discover = [&](const std::filesystem::path& path) {
                const auto diagnostic = unparsedCatalogDiagnostic(path);
                if (!diagnostic.empty()) mark(path, ReadLevel::Discovered, diagnostic);
            };
            for (const auto& entry : std::filesystem::directory_iterator(resourceRoot))
                if (entry.is_regular_file()) discover(entry.path());
            const auto shapesRoot = resourceRoot / "Shapes";
            if (std::filesystem::is_directory(shapesRoot))
            {
                for (const auto& entry : std::filesystem::directory_iterator(shapesRoot))
                {
                    if (entry.is_regular_file()) discover(entry.path());
                    else if (entry.is_directory())
                        for (const auto& nested : std::filesystem::directory_iterator(entry.path()))
                            if (nested.is_regular_file()) discover(nested.path());
                }
            }
        }
        using ProfileSource = std::pair<std::filesystem::path, db1::ProfileSource>;
        std::map<std::string, std::vector<ProfileSource>> profileSources;
        const auto addProfileSource = [&](const std::string& name, const std::filesystem::path& path,
                                          db1::ProfileSource source) {
            auto& candidates = profileSources[name];
            if (std::find(candidates.begin(), candidates.end(), ProfileSource{path, source}) == candidates.end())
                candidates.emplace_back(path, source);
        };
        std::set<std::string> claimedShapeDefinitions, claimedShapeGeometries;
        std::vector<std::filesystem::path> shapeDirectories;
        std::filesystem::path boltCatalogPath, boltAssemblyCatalogPath;
        for (const auto& resourceRoot : roots)
        {
            const auto load = [&](const std::string& name, auto& destination, auto parser) {
                if (destination) return std::filesystem::path{};
                const auto path = findFile(resourceRoot, name); if (path.empty()) return std::filesystem::path{};
                typename std::decay_t<decltype(destination)>::value_type value; std::string diagnostic;
                if (!parser(path, value, diagnostic)) { failure(path, diagnostic); return std::filesystem::path{}; }
                destination = std::move(value); mark(path, ReadLevel::Semantic);
                result.associations.push_back({result.model.databasePath, path, "resource search precedence"});
                return path;
            };
            load("matdb.bin", result.materials, db1::parseMaterialCatalog);
            const auto loadedBoltCatalog = load("screwdb.db", result.bolts, db1::parseBoltCatalog);
            if (!loadedBoltCatalog.empty()) boltCatalogPath = loadedBoltCatalog;
            const auto loadedAssemblyCatalog = load("assdb.db", result.boltAssemblies, db1::parseBoltAssemblyCatalog);
            if (!loadedAssemblyCatalog.empty()) boltAssemblyCatalogPath = loadedAssemblyCatalog;
            load("profitab.inp", result.profileRules, db1::parseProfitabCatalog);
            load("RebarShapeCatalog.rscx", result.rebarShapeCatalog, db1::parseRebarShapeCatalog);
            if (options.readRebarScheduleConfig && !result.rebarScheduleConfig)
            {
                const auto path=findFile(resourceRoot,"rebar_schedule_config.inp");
                if(!path.empty())
                {
                    db1::RebarScheduleConfig config;std::string diagnostic;
                    if(!db1::parseRebarScheduleConfig(path,config,diagnostic))failure(path,diagnostic);
                    else
                    {
                        result.rebarScheduleConfig=std::move(config);
                        mark(path,ReadLevel::PartialSemantic,
                             "BEND_TYPE mappings retained; unknown lines remain raw and formulas are not evaluated");
                        result.associations.push_back({result.model.databasePath,path,"resource search precedence"});
                    }
                }
            }
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
                        addProfileSource(entry.first, path, db1::ProfileSource::SketchSolver);
                    }
                    for (const auto& item : catalog.diagnostics) result.diagnostics.push_back(db1::detail::pathUtf8(path) + ": " + item);
                }
                else
                {
                    db1::ProfileCatalog catalog;
                    if (!db1::parseProfileCatalog(path, catalog, diagnostic)) { failure(path, diagnostic); continue; }
                    const std::set<std::string> ambiguous(catalog.ambiguousProfileNames.begin(), catalog.ambiguousProfileNames.end());
                    for (const auto& name : ambiguous)
                        result.diagnostics.push_back("profdb profile name is duplicated within one catalog; association omitted: " + name);
                    for (auto& entry : catalog.profiles)
                    {
                        if (ambiguous.count(entry.first)) continue;
                        addProfileSource(entry.first, path, db1::ProfileSource::Catalog);
                        result.model.profiles.emplace(entry.first, std::move(entry.second));
                    }
                }
                mark(path, ReadLevel::Semantic);
                result.associations.push_back({result.model.databasePath, path, "profile name lookup; earlier resources take precedence"});
            }
            std::vector<std::filesystem::path> directories;
            for (const auto* name : {"Shapes", "ShapeGeometries"})
            {
                const auto path = resourceRoot / name;
                if (std::filesystem::is_directory(path)) directories.push_back(path);
            }
            if (!directories.empty())
            {
                db1::detail::ShapeDirectoryScan scan; std::string diagnostic;
                if (!db1::detail::readShapeDirectories(directories, scan, diagnostic))
                {
                    for (const auto& path : directories) failure(path, diagnostic);
                    continue;
                }
                auto& catalog = scan.catalog;
                if (options.strictCompanions && !catalog.diagnostics.empty())
                    throw std::runtime_error(catalog.diagnostics.front());
                // Ambiguity at an earlier priority is not an absent resource.
                claimedShapeDefinitions.insert(scan.ambiguousDefinitions.begin(), scan.ambiguousDefinitions.end());
                claimedShapeGeometries.insert(scan.ambiguousGeometries.begin(), scan.ambiguousGeometries.end());
                for (auto& entry : catalog.definitionsByGuid)
                    if (claimedShapeDefinitions.insert(entry.first).second)
                        result.shapes.definitionsByGuid.emplace(entry.first, std::move(entry.second));
                for (auto& entry : catalog.geometriesByStorageId)
                    if (claimedShapeGeometries.insert(entry.first).second)
                        result.shapes.geometriesByStorageId.emplace(entry.first, std::move(entry.second));
                for (const auto& item : catalog.diagnostics)
                {
                    result.shapes.diagnostics.push_back(item);
                    result.diagnostics.push_back(item);
                }
                for (const auto& path : directories)
                {
                    shapeDirectories.push_back(path);
                    mark(path, catalog.diagnostics.empty() ? ReadLevel::Semantic : ReadLevel::PartialSemantic,
                         catalog.diagnostics.empty() ? "" : "shape resource contains invalid or ambiguous entries");
                }
            }
        }
        if (result.bolts && result.boltAssemblies && !boltCatalogPath.empty() &&
            !boltAssemblyCatalogPath.empty() &&
            std::filesystem::absolute(boltCatalogPath).lexically_normal().parent_path() ==
                std::filesystem::absolute(boltAssemblyCatalogPath).lexically_normal().parent_path())
        {
            std::map<std::string, std::vector<std::size_t>> entriesByStandard;
            for (std::size_t index = 0; index < result.bolts->bolts.size(); ++index)
                if (!result.bolts->bolts[index].standard.empty())
                    entriesByStandard[result.bolts->bolts[index].standard].push_back(index);
            for (std::size_t assemblyIndex = 0; assemblyIndex < result.boltAssemblies->assemblies.size(); ++assemblyIndex)
            {
                const auto& assembly = result.boltAssemblies->assemblies[assemblyIndex];
                for (std::size_t componentIndex = 0; componentIndex < assembly.componentNames.size(); ++componentIndex)
                {
                    const auto& standard = assembly.componentNames[componentIndex];
                    const auto found = entriesByStandard.find(standard);
                    if (standard.empty() || found == entriesByStandard.end()) continue;
                    result.boltAssemblyStandardAssociations.push_back({boltAssemblyCatalogPath, assemblyIndex,
                        componentIndex, standard, boltCatalogPath, found->second});
                }
            }
            if (!result.boltAssemblyStandardAssociations.empty())
                result.associations.push_back({boltAssemblyCatalogPath, boltCatalogPath,
                    "assembly component standard exactly matches bolt catalog standard"});
        }
        if (options.readProfileLibraries)
        {
            std::set<std::filesystem::path> seenClbFiles;
            for (const auto& resourceRoot : roots)
            {
                std::error_code iterationError;
                std::filesystem::recursive_directory_iterator iterator(
                    resourceRoot, std::filesystem::directory_options::skip_permission_denied, iterationError), end;
                for (; iterator != end; iterator.increment(iterationError))
                {
                    if (iterationError) { iterationError.clear(); continue; }
                    const auto depth = iterator.depth();
                    if (depth >= 12 && iterator->is_directory(iterationError))
                    {
                        iterator.disable_recursion_pending();
                        continue;
                    }
                    if (iterator->is_symlink(iterationError))
                    {
                        iterator.disable_recursion_pending();
                        continue;
                    }
                    if (!iterator->is_regular_file(iterationError) ||
                        lower(db1::detail::pathUtf8(iterator->path().extension())) != ".clb") continue;
                    const auto path = std::filesystem::absolute(iterator->path()).lexically_normal();
                    if (!seenClbFiles.insert(path).second) continue;
                    db1::ClbDocument document; std::string diagnostic;
                    if (!db1::parseClbDocument(path, document, diagnostic))
                    {
                        failure(path, diagnostic);
                        continue;
                    }
                    const auto level = document.diagnostics.empty() ? ReadLevel::Semantic : ReadLevel::PartialSemantic;
                    const auto detail = document.diagnostics.empty() ? std::string{} : "CLB syntax retained with ambiguous declarations";
                    mark(path, level, detail);
                    for (const auto& item : document.diagnostics)
                        result.diagnostics.push_back(db1::detail::pathUtf8(path) + ": " + item);
                    result.clbDocuments.emplace(path, std::move(document));
                }
            }
        }
        for (const auto& file : result.clbDocuments)
        {
            if (lower(db1::detail::pathUtf8(file.first.filename())) != "components.clb") continue;
            bool partial = false;
            for (std::size_t index = 0; index < file.second.includes.size(); ++index)
            {
                const auto& include = file.second.includes[index];
                const auto separator = include.target.find_first_of("/\\:");
                const auto includeName = std::filesystem::path(include.target);
                if (separator != std::string::npos || includeName.has_parent_path() ||
                    lower(db1::detail::pathUtf8(includeName.extension())) != ".clb")
                {
                    partial = true;
                    result.diagnostics.push_back(db1::detail::pathUtf8(file.first) + ": components.clb Include at line " +
                        std::to_string(include.line) + " is not a sibling .clb filename; association omitted");
                    continue;
                }
                try
                {
                    const auto candidate = findFile(file.first.parent_path(), db1::detail::pathUtf8(includeName.filename()));
                    if (candidate.empty())
                    {
                        partial = true;
                        result.diagnostics.push_back(db1::detail::pathUtf8(file.first) + ": components.clb Include target is missing: " + include.target);
                        continue;
                    }
                    const auto canonical = std::filesystem::absolute(candidate).lexically_normal();
                    if (result.clbDocuments.find(canonical) == result.clbDocuments.end())
                    {
                        partial = true;
                        result.diagnostics.push_back(db1::detail::pathUtf8(file.first) + ": components.clb Include target was not parsed: " + include.target);
                        continue;
                    }
                    result.clbIncludeAssociations.push_back({file.first, index, canonical});
                }
                catch (const std::exception& exception)
                {
                    partial = true;
                    result.diagnostics.push_back(db1::detail::pathUtf8(file.first) + ": " + exception.what());
                }
            }
            if (partial)
                mark(file.first, ReadLevel::PartialSemantic, "one or more components.clb Include targets are invalid, missing, or ambiguous");
        }
        if (result.profileRules && !result.clbDocuments.empty())
        {
            std::filesystem::path profitabPath;
            for (const auto& association : result.associations)
                if (lower(db1::detail::pathUtf8(association.target.filename())) == "profitab.inp" &&
                    association.reason == "resource search precedence")
                { profitabPath = association.target; break; }
            using SectionTarget = std::pair<std::filesystem::path, std::size_t>;
            std::map<std::pair<std::string, std::string>, std::vector<SectionTarget>> sectionTargets;
            for (const auto& file : result.clbDocuments)
            {
                if (file.second.libraryId.empty()) continue;
                for (std::size_t index = 0; index < file.second.sections.size(); ++index)
                {
                    const auto& section = file.second.sections[index];
                    if (!section.name.empty())
                        sectionTargets[{file.second.libraryId, section.name}].push_back({file.first, index});
                }
            }
            for (std::size_t index = 0; index < result.profileRules->rules.size(); ++index)
            {
                const auto& rule = result.profileRules->rules[index];
                if (rule.generatorLibraryId.empty() || rule.generatorSectionName.empty()) continue;
                const auto found = sectionTargets.find({rule.generatorLibraryId, rule.generatorSectionName});
                if (found != sectionTargets.end() && found->second.size() == 1)
                {
                    result.profileRuleSectionAssociations.push_back(
                        {profitabPath, index,
                         found->second.front().first, found->second.front().second});
                }
                else if (found != sectionTargets.end())
                    result.diagnostics.push_back("profitab G3-NAME matches multiple CLB sections; association omitted: " + rule.generator);
                else
                    result.diagnostics.push_back("profitab G3-NAME has no matching CLB section: " + rule.generator);
            }
        }
        std::set<std::string> reportedAmbiguousProfiles;
        const auto linkProfiles = [&](const db1::Model& model) {
            for (const auto& entry : model.parts)
            {
                const auto& part = entry.second;
                if (part.profile.empty()) continue;
                if (model.ambiguousProfileNames.count(part.profile))
                {
                    if (reportedAmbiguousProfiles.insert(part.profile).second)
                        result.diagnostics.push_back("profile name is duplicated within its profdb.bin; association omitted: " + part.profile);
                    continue;
                }
                const auto found = profileSources.find(part.profile);
                if (found == profileSources.end()) continue;
                if (found->second.size() != 1)
                {
                    if (reportedAmbiguousProfiles.insert(part.profile).second)
                        result.diagnostics.push_back("profile name has multiple catalog sources; association omitted: " + part.profile);
                    continue;
                }
                result.profileCatalogAssociations.push_back({model.databasePath, part.id, part.profile,
                    found->second.front().first, found->second.front().second});
            }
        };
        linkProfiles(result.model);
        if (result.componentLibrary) linkProfiles(*result.componentLibrary);
        std::sort(result.profileCatalogAssociations.begin(), result.profileCatalogAssociations.end(), [](const auto& a, const auto& b) {
            return std::tie(a.database, a.objectId) < std::tie(b.database, b.objectId);
        });
        std::sort(result.profileRuleSectionAssociations.begin(), result.profileRuleSectionAssociations.end(), [](const auto& a, const auto& b) {
            return std::tie(a.profitab, a.ruleIndex, a.clb, a.sectionIndex) <
                   std::tie(b.profitab, b.ruleIndex, b.clb, b.sectionIndex);
        });
        std::sort(result.clbIncludeAssociations.begin(), result.clbIncludeAssociations.end(), [](const auto& a, const auto& b) {
            return std::tie(a.components, a.includeIndex, a.clb) < std::tie(b.components, b.includeIndex, b.clb);
        });
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
            const bool legacyPair = options.trustLegacyNumberingBasenames && (model.storageVersion=="7.30" || model.storageVersion=="7.64" || model.storageVersion=="7.82") && model.databaseGuid.empty() && database.raw.databaseGuid.empty();
            if (!legacyPair && (model.databaseGuid.empty() || database.raw.databaseGuid.empty() || lower(model.databaseGuid)!=lower(database.raw.databaseGuid)))
            {
                result.diagnostics.push_back("object numbering DB1/DB2 GUID scope is unverified: " + db1::detail::pathUtf8(model.databasePath));
                return;
            }
            std::map<std::pair<std::string,std::uint32_t>,std::vector<std::size_t>> indices;
            for (std::size_t i=0; i<database.series.size(); ++i)
                indices[{database.series[i].prefix,database.series[i].startNumber}].push_back(i);
            std::size_t unresolved = 0;
            const auto linkRecord = [&](std::uint32_t objectId,const db1::ObjectNumberingRecord& record) {
                if (record.kind!=db1::ObjectNumberingKind::Part && record.kind!=db1::ObjectNumberingKind::Assembly) return;
                const auto series = indices.find({record.prefix,record.startNumber});
                if (series==indices.end() || series->second.size()!=1) { ++unresolved; return; }
                result.objectNumberingSeriesAssociations.push_back({model.databasePath,pair,objectId,record.id,series->second.front(),legacyPair?NumberingPairEvidence::ExplicitLegacyBasename:NumberingPairEvidence::DatabaseGuid});
            };
            for (const auto& entry : model.objectNumberingReferences)
            {
                const auto record = model.objectNumberingRecords.find(entry.second.numberingRecordId);
                if (record!=model.objectNumberingRecords.end()) linkRecord(entry.first,record->second);
            }
            for (const auto& entry : model.objectNumberingRecords)
                if (entry.second.inlineObjectId) linkRecord(*entry.second.inlineObjectId,entry.second);
            if (unresolved) result.diagnostics.push_back(std::to_string(unresolved) + " object numbering references have missing or ambiguous DB2 series: " + db1::detail::pathUtf8(model.databasePath));

            for (const auto& entry : model.objectNumberingRecords)
            {
                const auto& record = entry.second;
                if (record.kind != db1::ObjectNumberingKind::Reinforcement || record.rawPayload.size() < 12)
                    continue;
                const auto series = indices.find({record.prefix, record.startNumber});
                if (series == indices.end() || series->second.size() != 1)
                    continue;
                const auto seriesIndex = series->second.front();
                const auto& numberingSeries = database.series.at(seriesIndex);
                // The observed byte alias is from the modern 80-byte DB2 row
                // (five additional u32 values). Do not extend it to the
                // legacy 76-byte row based only on a coincidental equality.
                if (numberingSeries.additionalFields.size() != 5)
                    continue;
                const auto db1RawField = std::uint32_t(record.rawPayload[8]) |
                    (std::uint32_t(record.rawPayload[9]) << 8) |
                    (std::uint32_t(record.rawPayload[10]) << 16) |
                    (std::uint32_t(record.rawPayload[11]) << 24);
                const auto db2RawField = numberingSeries.additionalFields[3];
                if (db1RawField == db2RawField)
                    result.reinforcementNumberingSeriesAssociations.push_back({model.databasePath, pair,
                        record.id, seriesIndex, db1RawField, db2RawField,
                        legacyPair ? NumberingPairEvidence::ExplicitLegacyBasename : NumberingPairEvidence::DatabaseGuid});
            }
        };
        linkNumberingSeries(result.model);
        if (result.componentLibrary) linkNumberingSeries(*result.componentLibrary);
        std::sort(result.objectNumberingSeriesAssociations.begin(),result.objectNumberingSeriesAssociations.end(),[](const auto& a,const auto& b) {
            return std::tie(a.database,a.objectId) < std::tie(b.database,b.objectId);
        });
        std::sort(result.reinforcementNumberingSeriesAssociations.begin(),result.reinforcementNumberingSeriesAssociations.end(),[](const auto& a,const auto& b) {
            return std::tie(a.database,a.numberingRecordId) < std::tie(b.database,b.numberingRecordId);
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
        if (result.materials)
        {
            std::map<std::string,std::vector<std::size_t>> materialIndices;
            for (std::size_t i=0;i<result.materials->materials.size();++i)
                materialIndices[result.materials->materials[i].name].push_back(i);
            const auto linkPartMaterials = [&](const db1::Model& model) {
                for (const auto& entry:model.parts)
                {
                    const auto& part=entry.second;
                    if (part.material.empty()) continue;
                    const auto found=materialIndices.find(part.material);
                    if (found!=materialIndices.end() && found->second.size()==1)
                        result.partMaterialAssociations.push_back(
                            {model.databasePath,part.id,part.material,found->second.front()});
                }
            };
            linkPartMaterials(result.model);
            if (result.componentLibrary) linkPartMaterials(*result.componentLibrary);
            std::sort(result.partMaterialAssociations.begin(),result.partMaterialAssociations.end(),[](const auto& a,const auto& b) {
                return std::tie(a.database,a.objectId)<std::tie(b.database,b.objectId);
            });
        }
        for (const auto& entry : result.shapes.definitionsByGuid)
        {
            const auto& definition = entry.second;
            const auto geometry = result.shapes.geometriesByStorageId.find(definition.brepStorageId);
            if (geometry != result.shapes.geometriesByStorageId.end())
                result.associations.push_back({definition.sourcePath, geometry->second.sourcePath,
                                               "shape BrepStorageId matches geometry storage ID"});
            else
            {
                const auto diagnostic = "shape " + definition.name + " references missing or ambiguous geometry " + definition.brepStorageId;
                result.shapes.diagnostics.push_back(diagnostic);
                result.diagnostics.push_back(diagnostic);
                for (const auto& path : shapeDirectories)
                {
                    const auto relative = definition.sourcePath.lexically_relative(path);
                    if (!relative.empty() && *relative.begin() != "..")
                        mark(path, ReadLevel::PartialSemantic, "shape resource has unresolved geometry references");
                }
            }
        }
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
