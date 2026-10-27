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
// Partial semantics: series keys/counters. Object assignments are recovered
// separately from DB1; comparison snapshots and other DB2 tables remain raw.
bool parseNumberingDatabase(const std::filesystem::path& path, NumberingDatabase& result,
                            std::string& error, const db1::RawDatabaseOptions& options = {});
}
