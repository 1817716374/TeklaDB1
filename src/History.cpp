#include <tekla/History.hpp>

#include <sqlite3.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace tekla
{
namespace
{
struct DbCloser { void operator()(sqlite3* db) const { if (db) sqlite3_close(db); } };
struct StatementCloser { void operator()(sqlite3_stmt* statement) const { if (statement) sqlite3_finalize(statement); } };
using Db = std::unique_ptr<sqlite3, DbCloser>;
using Statement = std::unique_ptr<sqlite3_stmt, StatementCloser>;

[[noreturn]] void fail(sqlite3* db, const std::string& what)
{
    throw std::runtime_error(what + ": " + (db ? sqlite3_errmsg(db) : "SQLite error"));
}

Statement query(sqlite3* db, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) fail(db, "prepare history query");
    return Statement(statement);
}

std::string stringAt(sqlite3_stmt* statement, int column, std::size_t& used, std::size_t limit)
{
    const auto* text = sqlite3_column_text(statement, column);
    const auto size = sqlite3_column_bytes(statement, column);
    if (size < 0 || static_cast<std::size_t>(size) > limit - (std::min)(used, limit))
        throw std::runtime_error("history database exceeds decoded byte limit");
    used += static_cast<std::size_t>(size);
    return text && size > 0 ? std::string(reinterpret_cast<const char*>(text), static_cast<std::size_t>(size)) : std::string{};
}

std::vector<std::uint8_t> blobAt(sqlite3_stmt* statement, int column, std::size_t expected,
                                 std::size_t& used, std::size_t limit, const char* field)
{
    const int size = sqlite3_column_bytes(statement, column);
    const auto* data = static_cast<const std::uint8_t*>(sqlite3_column_blob(statement, column));
    if (size < 0 || (expected && static_cast<std::size_t>(size) != expected) || (size && !data))
        throw std::runtime_error(std::string("invalid history blob: ") + field);
    const auto count = static_cast<std::size_t>(size);
    if (count > limit - (std::min)(used, limit)) throw std::runtime_error("history database exceeds decoded byte limit");
    used += count;
    return count ? std::vector<std::uint8_t>(data, data + count) : std::vector<std::uint8_t>{};
}

std::array<std::uint8_t, 16> guidAt(sqlite3_stmt* statement, int column, std::size_t& used,
                                   std::size_t limit, const char* field)
{
    const auto blob = blobAt(statement, column, 16, used, limit, field);
    if (blob.size() != 16) throw std::runtime_error(std::string("missing history GUID: ") + field);
    std::array<std::uint8_t, 16> value{};
    std::copy(blob.begin(), blob.end(), value.begin());
    return value;
}

template <class Consumer>
void stepRows(sqlite3* db, sqlite3_stmt* statement, std::size_t& rows, std::size_t maxRows,
              const char* table, Consumer&& consume)
{
    int status;
    while ((status = sqlite3_step(statement)) == SQLITE_ROW)
    {
        if (rows >= maxRows) throw std::runtime_error("history database exceeds row limit");
        ++rows;
        consume();
    }
    if (status != SQLITE_DONE) fail(db, std::string("read history table ") + table);
}
}

std::string historyGuidString(const std::array<std::uint8_t, 16>& bytes)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(36);
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10) result.push_back('-');
        result.push_back(digits[bytes[i] >> 4]);
        result.push_back(digits[bytes[i] & 15]);
    }
    return result;
}

bool parseHistoryDatabase(const std::filesystem::path& path, HistoryDatabase& result,
                          std::string& error, const HistoryReadOptions& options)
{
    try
    {
        error.clear();
        result = {};
        if (options.maxDecodedBytes == 0 || options.maxRows == 0)
            throw std::runtime_error("history read limits must be nonzero");
        sqlite3* rawDb = nullptr;
        const auto filename = path.u8string();
        const auto* filenameUtf8 = reinterpret_cast<const char*>(filename.data());
        if (sqlite3_open_v2(filenameUtf8, &rawDb, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK)
        {
            Db failed(rawDb);
            fail(rawDb, "open history database read-only");
        }
        Db db(rawDb);
        sqlite3_db_config(db.get(), SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
        sqlite3_limit(db.get(), SQLITE_LIMIT_LENGTH,
                      static_cast<int>((std::min)(options.maxDecodedBytes,
                          static_cast<std::size_t>((std::numeric_limits<int>::max)()))));
        result.path = std::filesystem::absolute(path).lexically_normal();
        std::size_t decoded = 0, rows = 0;

        {
            auto statement = query(db.get(), "SELECT id,name,email FROM user ORDER BY id");
            stepRows(db.get(), statement.get(), rows, options.maxRows, "user", [&] {
                result.users.push_back({sqlite3_column_int64(statement.get(), 0),
                                        stringAt(statement.get(), 1, decoded, options.maxDecodedBytes),
                                        stringAt(statement.get(), 2, decoded, options.maxDecodedBytes)});
            });
        }
        {
            auto statement = query(db.get(), "SELECT id,code,comment,user_id,time,committed,revision_hash,packet_number FROM revision ORDER BY id");
            stepRows(db.get(), statement.get(), rows, options.maxRows, "revision", [&] {
                HistoryRevision value;
                value.id = sqlite3_column_int64(statement.get(), 0);
                value.code = stringAt(statement.get(), 1, decoded, options.maxDecodedBytes);
                value.comment = stringAt(statement.get(), 2, decoded, options.maxDecodedBytes);
                value.userId = sqlite3_column_int64(statement.get(), 3);
                value.time = sqlite3_column_int64(statement.get(), 4);
                value.committed = sqlite3_column_int(statement.get(), 5) != 0;
                value.revisionHash = blobAt(statement.get(), 6, 0, decoded, options.maxDecodedBytes, "revision_hash");
                value.packetNumber = sqlite3_column_int64(statement.get(), 7);
                result.revisions.push_back(std::move(value));
            });
        }
        {
            auto statement = query(db.get(), "SELECT id,category,identifier,type,owner_id,creation_time_guid FROM model_item ORDER BY id");
            stepRows(db.get(), statement.get(), rows, options.maxRows, "model_item", [&] {
                HistoryItem value;
                value.id = sqlite3_column_int64(statement.get(), 0);
                value.category = sqlite3_column_int64(statement.get(), 1);
                value.identifier = guidAt(statement.get(), 2, decoded, options.maxDecodedBytes, "model_item.identifier");
                value.type = sqlite3_column_int64(statement.get(), 3);
                value.ownerId = sqlite3_column_int64(statement.get(), 4);
                value.creationTimeGuid = guidAt(statement.get(), 5, decoded, options.maxDecodedBytes, "model_item.creation_time_guid");
                result.items.push_back(value);
            });
        }
        {
            auto statement = query(db.get(), "SELECT revision_id,item_id,action,action_time_guid,version_data FROM model_item_revision ORDER BY revision_id,item_id");
            stepRows(db.get(), statement.get(), rows, options.maxRows, "model_item_revision", [&] {
                HistoryItemRevision value;
                value.revisionId = sqlite3_column_int64(statement.get(), 0);
                value.itemId = sqlite3_column_int64(statement.get(), 1);
                value.action = sqlite3_column_int64(statement.get(), 2);
                if (sqlite3_column_type(statement.get(), 3) != SQLITE_NULL)
                    value.actionTimeGuid = guidAt(statement.get(), 3, decoded, options.maxDecodedBytes, "action_time_guid");
                value.versionData = blobAt(statement.get(), 4, 0, decoded, options.maxDecodedBytes, "version_data");
                result.itemRevisions.push_back(std::move(value));
            });
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        result = {};
        error = exception.what();
        return false;
    }
}
}
