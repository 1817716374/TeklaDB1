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
                    mark(entry.path(), ReadLevel::Discovered, "DG semantic decoding is not implemented");
        mark(result.model.databasePath, ReadLevel::Semantic);

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
                mark(library, ReadLevel::Semantic);
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
                        if (stem == lower(db1::detail::pathUtf8(result.model.databasePath.stem())))
                            result.associations.push_back({result.model.databasePath, file.path, "matching DB1/DB2 basename; numbering fields remain raw"});
                        else if (stem == "xslib" && !library.empty())
                            result.associations.push_back({library, file.path, "component DB1/DB2 pair; numbering fields remain raw"});
                        else result.diagnostics.push_back("unpaired numbering database: " + db1::detail::pathUtf8(file.path));
                    }
                    result.rawCompanions.emplace(file.path, std::move(raw));
                }
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
        for (const auto& entry : result.shapes.definitionsByGuid)
            if (!result.shapes.geometriesByStorageId.count(entry.second.brepStorageId))
                result.diagnostics.push_back("shape " + entry.second.name + " references missing geometry " + entry.second.brepStorageId);
        if (result.profileRules)
            for (const auto& item : result.profileRules->diagnostics) result.diagnostics.push_back("profitab: " + item);
        std::sort(result.files.begin(), result.files.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
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
