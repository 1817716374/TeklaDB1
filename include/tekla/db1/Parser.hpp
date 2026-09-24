#pragma once

#include <tekla/db1/Database.hpp>
#include <tekla/db1/Model.hpp>

#include <filesystem>
#include <string>

namespace tekla::db1
{
struct ModelReadOptions
{
    std::size_t maxDecodedBytes = 1024ULL * 1024 * 1024;
};
bool parseModelDirectory(const std::filesystem::path& directory, Model& model, std::string& error);
bool parseModelDirectory(const std::filesystem::path& directory, Model& model, std::string& error,
                         const ModelReadOptions& options);
// Selects exactly this DB1; optional local catalogs are read from its parent.
// Directory input rejects ambiguity rather than picking the largest database.
bool parseModelFile(const std::filesystem::path& path, Model& model, std::string& error);
bool parseModelFile(const std::filesystem::path& path, Model& model, std::string& error,
                    const ModelReadOptions& options);
// Parses the object graph stored in xslib.db1.  The neutral Model type is
// reused because component libraries contain the same points, parts,
// relationships, parameters and component records as a model database.
bool parseComponentLibrary(const std::filesystem::path& path, Model& model, std::string& error);
bool parseComponentLibrary(const std::filesystem::path& path, Model& model, std::string& error,
                           const ModelReadOptions& options);
}
