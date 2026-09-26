#pragma once
#include <tekla/db1/Database.hpp>

namespace tekla
{
struct NumberingSeries
{
    std::string key;
    std::string prefix;
    std::uint32_t startNumber = 0;
    // Stored sequence counters, not the number of currently existing objects.
    // These do not identify which model object has a particular position.
    std::uint32_t partCounter = 0;
    std::uint32_t assemblyCounter = 0;
    std::vector<std::uint32_t> additionalFields;
};
struct NumberingDatabase
{
    db1::RawDatabase raw;
    std::vector<NumberingSeries> series;
    std::vector<std::string> diagnostics;
};
// Partial semantics: series keys/counters. Object assignments and comparison
// snapshots remain raw, including all tables with large numeric table IDs.
bool parseNumberingDatabase(const std::filesystem::path& path, NumberingDatabase& result,
                            std::string& error, const db1::RawDatabaseOptions& options = {});
}
