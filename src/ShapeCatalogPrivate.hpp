#pragma once
#include <tekla/db1/Catalogs.hpp>
#include <set>

namespace tekla::db1::detail
{
// One resource root can store definitions and geometry in sibling directories.
// Keep ambiguous keys even after removing their values, so a caller cannot
// accidentally turn ambiguity into absence and fall back to another resource.
struct ShapeDirectoryScan
{
    ShapeCatalog catalog;
    std::set<std::string> ambiguousDefinitions;
    std::set<std::string> ambiguousGeometries;
};
bool readShapeDirectories(const std::vector<std::filesystem::path>& directories,
                          ShapeDirectoryScan& result, std::string& error);
}
