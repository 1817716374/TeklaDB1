#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace tekla
{
// Literal types in the saved preset, not inferred boolean/enum semantics.
// Decoded strings are UTF-8. rawText and rawValue retain original file bytes.
using AnalysisSettingValue=std::variant<std::int64_t,double,std::string>;
struct AnalysisSetting
{
    std::string name;
    AnalysisSettingValue value;
    std::string rawValue;
    std::size_t lineNumber=0;
};
struct AnalysisDesignSetting
{
    std::string tableName;
    std::string analysisEngine;
    std::string field;
    AnalysisSettingValue value;
    std::string rawValue;
    std::size_t lineNumber=0;
};
struct AnalysisModelSettings
{
    std::filesystem::path sourcePath;
    std::uint32_t codePage=0;
    std::vector<AnalysisSetting> settings;
    std::vector<AnalysisDesignSetting> designSettings;
    std::string rawText;
    std::vector<std::string> diagnostics;
};
// Verified plain .admodel grammar with encoding 1252. Other encodings and
// unsupported syntax fail explicitly. Duplicates stay ordered; no precedence
// or connection to a DB6/model is inferred. Failure clears result.
bool parseAnalysisModelSettings(const std::filesystem::path& path,
    AnalysisModelSettings& result,std::string& error,std::size_t maxBytes=4*1024*1024);
}
