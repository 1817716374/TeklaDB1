#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tekla
{
struct HistoryReadOptions
{
    std::size_t maxDecodedBytes = 256ULL * 1024 * 1024;
    std::size_t maxRows = 5ULL * 1000 * 1000;
};

struct HistoryUser
{
    std::int64_t id = 0;
    std::string name;
    std::string email;
};

struct HistoryRevision
{
    std::int64_t id = 0;
    std::string code;
    std::string comment;
    std::int64_t userId = 0;
    std::int64_t time = 0;
    bool committed = false;
    std::vector<std::uint8_t> revisionHash;
    std::int64_t packetNumber = 0;
};

struct HistoryItem
{
    std::int64_t id = 0;
    std::int64_t category = 0;
    std::array<std::uint8_t, 16> identifier{};
    std::int64_t type = 0; // Raw history enum; not assumed equal to DB1 identity type.
    std::int64_t ownerId = 0;
    std::array<std::uint8_t, 16> creationTimeGuid{};
};

struct HistoryItemRevision
{
    std::int64_t revisionId = 0;
    std::int64_t itemId = 0;
    std::int64_t action = 0;
    std::array<std::uint8_t, 16> actionTimeGuid{};
    std::vector<std::uint8_t> versionData;
};

struct HistoryDatabase
{
    std::filesystem::path path;
    std::vector<HistoryUser> users;
    std::vector<HistoryRevision> revisions;
    std::vector<HistoryItem> items;
    std::vector<HistoryItemRevision> itemRevisions;
    std::vector<std::string> diagnostics;
};

// Reads the observed Tekla history.db SQLite schema in read-only mode. Unknown
// columns remain outside this semantic view; opaque version_data stays raw.
bool parseHistoryDatabase(const std::filesystem::path& path, HistoryDatabase& result,
                          std::string& error, const HistoryReadOptions& options = {});
std::string historyGuidString(const std::array<std::uint8_t, 16>& bytes);
}
