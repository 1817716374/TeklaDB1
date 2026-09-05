#pragma once

#include <tekla/db1/Database.hpp>
#include <tekla/db1/Model.hpp>

#include <filesystem>
#include <string>

namespace tekla::db1
{
bool parseModelDirectory(const std::filesystem::path& directory, Model& model, std::string& error);
// Parses the object graph stored in xslib.db1.  The neutral Model type is
// reused because component libraries contain the same points, parts,
// relationships, parameters and component records as a model database.
bool parseComponentLibrary(const std::filesystem::path& path, Model& model, std::string& error);
}
