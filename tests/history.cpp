#include <tekla/History.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
bool exec(sqlite3* db, const char* sql)
{
    char* error = nullptr;
    const int status = sqlite3_exec(db, sql, nullptr, nullptr, &error);
    if (status != SQLITE_OK) std::cerr << (error ? error : sqlite3_errmsg(db)) << '\n';
    sqlite3_free(error);
    return status == SQLITE_OK;
}

int run(const std::filesystem::path& root)
{
    std::filesystem::create_directories(root);
    const auto path = root / "history.db";
    std::filesystem::remove(path);
    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) return 1;
    const bool built = exec(db,
        "CREATE TABLE user(id INTEGER PRIMARY KEY,name TEXT,email TEXT NOT NULL UNIQUE);"
        "CREATE TABLE revision(id INTEGER PRIMARY KEY,code TEXT,comment TEXT,user_id INTEGER,time INTEGER,committed INTEGER,revision_hash BLOB,packet_number INTEGER);"
        "CREATE TABLE model_item(id INTEGER PRIMARY KEY,category INTEGER,identifier BLOB NOT NULL,type INTEGER,owner_id INTEGER,creation_time_guid BLOB);"
        "CREATE TABLE model_item_revision(revision_id INTEGER,item_id INTEGER,action INTEGER,action_time_guid BLOB,version_data BLOB,PRIMARY KEY(revision_id,item_id));"
        "INSERT INTO user VALUES(1,'Test User','test@example.invalid');"
        "INSERT INTO revision VALUES(7,'R1','initial',1,1234,1,X'ABCD',9);"
        "INSERT INTO model_item VALUES(12,0,X'00112233445566778899AABBCCDDEEFF',34,1,X'FFEEDDCCBBAA99887766554433221100');"
        "INSERT INTO model_item_revision VALUES(7,12,2,NULL,X'010203');");
    sqlite3_close(db);
    if (!built) return 1;

    tekla::HistoryDatabase result;
    std::string error;
    if (!tekla::parseHistoryDatabase(path, result, error)) { std::cerr << error << '\n'; return 1; }
    if (result.users.size() != 1 || result.revisions.size() != 1 || result.items.size() != 1 ||
        result.itemRevisions.size() != 1 || result.revisions[0].revisionHash.size() != 2 ||
        result.items[0].type != 34 || result.itemRevisions[0].versionData.size() != 3 ||
        tekla::historyGuidString(result.items[0].identifier) != "00112233-4455-6677-8899-AABBCCDDEEFF")
        return 1;

    tekla::HistoryReadOptions limited;
    limited.maxRows = 1;
    if (tekla::parseHistoryDatabase(path, result, error, limited) || error.empty()) return 1;
    limited.maxRows = 100;
    limited.maxDecodedBytes = 1;
    if (tekla::parseHistoryDatabase(path, result, error, limited) || error.empty()) return 1;
    if (!result.items.empty() || !result.users.empty()) return 1;
    return 0;
}
}

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    return run(std::filesystem::u8path(argv[1]));
}
