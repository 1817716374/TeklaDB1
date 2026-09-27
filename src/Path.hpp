#pragma once
#include <filesystem>
#include <string>

namespace tekla::db1::detail
{
inline std::string pathUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return std::string(value.begin(), value.end());
}
}
