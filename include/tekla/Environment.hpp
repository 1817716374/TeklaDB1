#pragma once
#include <tekla/db1/Database.hpp>
#include <array>
#include <map>
#include <optional>
#include <variant>

namespace tekla
{
enum class StoredValueKind { Boolean, Integer, Real, String };
using StoredValue = std::variant<bool,std::int32_t,double,std::string>;
struct AttributeChoice
{
    std::uint32_t id = 0;
    std::uint32_t index = 0;
    std::int32_t integerValue = 0;
    std::string label;
};
struct AttributeMetadata
{
    std::uint32_t id = 0;
    // Keep the seven fields until each flag/type-code has independent evidence.
    std::array<std::uint32_t,7> fields{};
};
struct AttributeDefinition
{
    std::uint32_t id = 0;
    std::string name;
    std::string label;
    StoredValueKind storageKind = StoredValueKind::String;
    // A stored definition value, not the value on any individual model object.
    // String definition payloads have no nonempty public evidence yet.
    std::optional<StoredValue> storedValue;
    std::vector<StoredValue> additionalNumericFields;
    std::uint32_t metadataId = 0;
    std::vector<AttributeChoice> choices;
    std::vector<std::uint32_t> objectClassIds;
};
struct ObjectClassDefinition
{
    std::uint32_t id = 0;
    std::string name;
    std::vector<std::uint32_t> attributeIds;
};
struct EnvironmentDatabase
{
    db1::RawDatabase raw;
    std::map<std::uint32_t,ObjectClassDefinition> objectClasses;
    std::map<std::uint32_t,AttributeDefinition> attributes;
    std::map<std::uint32_t,AttributeMetadata> metadata;
    std::vector<std::string> diagnostics;
};
struct StoredOption
{
    std::uint32_t id = 0;
    std::string name;
    StoredValueKind kind = StoredValueKind::String;
    // Do not interpret either slot as the effective/default value without
    // independent evidence about the option database state and precedence.
    std::array<StoredValue,2> valueSlots;
    std::uint32_t flags = 0;
};
struct OptionsDatabase
{
    db1::RawDatabase raw;
    std::map<std::uint32_t,StoredOption> options;
    std::vector<std::string> diagnostics;
};
struct OptionSetting
{
    std::size_t lineNumber = 0; // one-based source line
    std::string name;
    std::string value; // exact bytes after '=', including spaces and empty values
    bool setPrefix = false;
};
struct OptionSettingsFile
{
    std::filesystem::path sourcePath;
    std::string rawText;
    std::vector<OptionSetting> settings; // ordered; duplicate keys are retained
    std::vector<std::size_t> uninterpretedLines;
    std::vector<std::string> diagnostics;
};
struct OptionValueMatch
{
    std::size_t settingIndex = 0;
    std::uint32_t optionId = 0;
    bool valueParsed = false; // text is comparable to this DBV storage type
    std::array<bool,2> matchingSlots{};
};
// Model-local text assignments only. No environment expansion or precedence.
// UTF-8 BOM is accepted; other bytes are preserved without code-page guessing.
bool parseOptionSettingsFile(const std::filesystem::path& path,OptionSettingsFile& result,
                             std::string& error,std::size_t maxBytes = 4*1024*1024);
// Exact case-sensitive names; every duplicate/cross-type record is retained.
// Matching a stored slot does not establish that it is the effective value.
std::vector<OptionValueMatch> matchOptionSettings(const OptionSettingsFile& settings,
                                                 const OptionsDatabase& database);
// Text bytes are preserved; no locale/code-page guessing is performed.
// Both APIs expose partial semantics and retain all unnamed fields in raw.
bool parseEnvironmentDatabase(const std::filesystem::path& path,EnvironmentDatabase& result,
                               std::string& error,const db1::RawDatabaseOptions& options = {});
bool parseOptionsDatabase(const std::filesystem::path& path,OptionsDatabase& result,
                          std::string& error,const db1::RawDatabaseOptions& options = {});
}
