#include <tekla/db1/Catalogs.hpp>
#include <tekla/db1/Parser.hpp>

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
int inspect(const std::filesystem::path& path, const std::string& kind)
{
    std::string error;
    if (kind == "profile")
    {
        tekla::db1::ProfileCatalog catalog;
        if (!tekla::db1::parseProfileCatalog(path, catalog, error))
            return std::cerr << error << '\n', 1;
        std::cout << "version=" << catalog.version << " profiles=" << catalog.profiles.size() << '\n';
    }
    else if (kind == "profile-geometry")
    {
        tekla::db1::ProfileGeometryCatalog catalog;
        if (!tekla::db1::parseProfileGeometryCatalog(path, catalog, error))
            return std::cerr << error << '\n', 1;
        std::size_t contourPoints = 0;
        for (const auto& entry : catalog.profiles)
            contourPoints += entry.second.contour.size();
        std::cout << "profiles=" << catalog.profiles.size() << " points=" << catalog.points.size()
                  << " chamfers=" << catalog.chamfers.size() << " contourPoints=" << contourPoints
                  << " diagnostics=" << catalog.diagnostics.size() << '\n';
    }
    else if (kind == "metadata")
    {
        tekla::db1::ModelMetadata metadata;
        if (!tekla::db1::parseModelMetadata(path, metadata, error))
            return std::cerr << error << '\n', 1;
        std::cout << "name=" << metadata.name << " version=" << metadata.version
                  << " productVersion=" << metadata.productVersion
                  << " environment=" << metadata.environment
                  << " isTemplate=" << metadata.isTemplate << '\n';
    }
    else if (kind == "raw")
    {
        tekla::db1::RawDatabase database;
        if (!tekla::db1::parseRawDatabase(path, database, error))
            return std::cerr << error << '\n', 1;
        std::size_t records = 0;
        for (const auto& table : database.tables)
            records += table.records.size();
        std::cout << "version=" << database.storageVersion << " tables=" << database.tables.size()
                  << " records=" << records << " kind=" << static_cast<int>(database.kind)
                  << " layout=" << static_cast<int>(database.layout) << '\n';
    }
    else if (kind == "library")
    {
        tekla::db1::Model model;
        if (!tekla::db1::parseComponentLibrary(path, model, error))
            return std::cerr << error << '\n', 1;
        std::cout << "version=" << model.storageVersion << " parts=" << model.actualPartIds.size()
                  << " operativeParts=" << model.operativePartIds.size()
                  << " bolts=" << model.boltGroups.size() << " welds=" << model.welds.size()
                  << " assemblies=" << model.assemblies.size() << " components=" << model.components.size()
                  << " properties=" << model.properties.size()
                  << " parameterDefinitions=" << model.parameterDefinitions.size()
                  << " customComponentDefinitions=" << model.customComponentDefinitions.size() << '\n';
    }
    else if (kind == "material")
    {
        tekla::db1::MaterialCatalog catalog;
        if (!tekla::db1::parseMaterialCatalog(path, catalog, error))
            return std::cerr << error << '\n', 1;
        std::cout << "version=" << catalog.version << " materials=" << catalog.materials.size()
                  << " definitions=" << catalog.attributeDefinitions.size() << '\n';
    }
    else if (kind == "bolt")
    {
        tekla::db1::BoltCatalog catalog;
        if (!tekla::db1::parseBoltCatalog(path, catalog, error))
            return std::cerr << error << '\n', 1;
        std::cout << "version=" << catalog.version << " bolts=" << catalog.bolts.size() << '\n';
    }
    else if (kind == "assembly")
    {
        tekla::db1::BoltAssemblyCatalog catalog;
        if (!tekla::db1::parseBoltAssemblyCatalog(path, catalog, error))
            return std::cerr << error << '\n', 1;
        std::cout << "version=" << catalog.version << " assemblies=" << catalog.assemblies.size() << '\n';
    }
    else if (kind == "profitab")
    {
        tekla::db1::ProfitabCatalog catalog;
        if (!tekla::db1::parseProfitabCatalog(path, catalog, error))
            return std::cerr << error << '\n', 1;
        std::cout << "rules=" << catalog.rules.size() << " diagnostics=" << catalog.diagnostics.size() << '\n';
    }
    else if (kind == "clb")
    {
        tekla::db1::ClbDocument document;
        if (!tekla::db1::parseClbDocument(path, document, error))
            return std::cerr << error << '\n', 1;
        std::cout << "statements=" << document.statements.size() << " diagnostics=" << document.diagnostics.size() << '\n';
    }
    else if (kind == "shape")
    {
        tekla::db1::ShapeCatalog catalog;
        if (!tekla::db1::parseShapeCatalog(path, catalog, error))
            return std::cerr << error << '\n', 1;
        std::cout << "definitions=" << catalog.definitionsByGuid.size()
                  << " geometries=" << catalog.geometriesByStorageId.size()
                  << " diagnostics=" << catalog.diagnostics.size() << '\n';
    }
    else if (kind == "shape-geometry")
    {
        tekla::db1::ShapeGeometry geometry;
        if (!tekla::db1::parseShapeGeometry(path, geometry, error))
            return std::cerr << error << '\n', 1;
        std::cout << "points=" << geometry.points.size() << " faces=" << geometry.faces.size()
                  << " edges=" << geometry.edges.size() << '\n';
    }
    else
        return 2;
    return 0;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    if (argc != 3)
        return 2;
    const std::wstring kindWide(argv[1]);
    std::string kind;
    kind.reserve(kindWide.size());
    for (const auto character : kindWide)
    {
        if (character > 0x7f)
            return 2;
        kind.push_back(static_cast<char>(character));
    }
    return inspect(std::filesystem::path(argv[2]), kind);
}
#else
int main(int argc, char** argv)
{
    return argc == 3 ? inspect(std::filesystem::u8path(argv[2]), argv[1]) : 2;
}
#endif
