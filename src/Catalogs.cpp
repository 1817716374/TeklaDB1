#include <tekla/db1/Catalogs.hpp>

#include <zlib.h>
#include "Path.hpp"
#include "BinaryIO.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace tekla::db1
{
namespace
{
template <typename T>
T readNumber(const std::uint8_t* data, std::size_t offset)
{
    T result{};
    std::memcpy(&result, data + offset, sizeof(T));
    return result;
}

std::string fixedString(const std::uint8_t* data, std::size_t offset, std::size_t size)
{
    const auto* begin = reinterpret_cast<const char*>(data + offset);
    const auto* end = std::find(begin, begin + size, '\0');
    return std::string(begin, end);
}

std::string trim(std::string value)
{
    const auto nonspace = [](unsigned char character) { return !std::isspace(character); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), nonspace));
    value.erase(std::find_if(value.rbegin(), value.rend(), nonspace).base(), value.end());
    return value;
}

using detail::readFile;
using detail::readPayload;

constexpr std::size_t maxTextCatalogBytes = 64ULL * 1024 * 1024;

std::string readText(const std::filesystem::path& path)
{
    const auto bytes = readPayload(path, maxTextCatalogBytes);
    std::string result(bytes.begin(), bytes.end());
    if (result.size() >= 3 && static_cast<unsigned char>(result[0]) == 0xef &&
        static_cast<unsigned char>(result[1]) == 0xbb && static_cast<unsigned char>(result[2]) == 0xbf)
        result.erase(0, 3);
    return result;
}

std::vector<std::string> split(const std::string& value, char delimiter)
{
    std::vector<std::string> result;
    std::size_t begin = 0;
    while (begin <= value.size())
    {
        const auto end = value.find(delimiter, begin);
        result.push_back(trim(value.substr(begin, end == std::string::npos ? end : end - begin)));
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    return result;
}

bool optionalInteger(const std::string& value, int& result)
{
    if (value.empty())
    {
        result = 0;
        return true;
    }
    auto begin = value.data();
    const auto end = begin + value.size();
    if (*begin == '+')
        ++begin;
    if (begin == end)
        return false;
    const auto parsed = std::from_chars(begin, end, result, 10);
    return parsed.ec == std::errc{} && parsed.ptr == end;
}

std::string stripLineComment(const std::string& line)
{
    bool quoted = false;
    for (std::size_t index = 0; index + 1 < line.size(); ++index)
    {
        if (line[index] == '"' && (index == 0 || line[index - 1] != '\\'))
            quoted = !quoted;
        if (!quoted && line[index] == '/' && line[index + 1] == '/')
            return line.substr(0, index);
    }
    return line;
}

std::vector<std::string> clbTokens(const std::string& line)
{
    std::vector<std::string> result;
    std::string token;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index)
    {
        const auto character = line[index];
        if (quoted)
        {
            if (character == '"' && (index == 0 || line[index - 1] != '\\'))
            {
                result.push_back(token);
                token.clear();
                quoted = false;
            }
            else
                token.push_back(character);
        }
        else if (character == '"')
        {
            if (!token.empty())
            {
                result.push_back(token);
                token.clear();
            }
            quoted = true;
        }
        else if (std::isspace(static_cast<unsigned char>(character)))
        {
            if (!token.empty())
            {
                result.push_back(token);
                token.clear();
            }
        }
        else
            token.push_back(character);
    }
    if (quoted)
        throw std::runtime_error("unterminated quoted CLB string");
    if (!token.empty())
        result.push_back(token);
    return result;
}


}

bool parseProfileGeometryCatalog(const std::filesystem::path& path,
                                 ProfileGeometryCatalog& result, std::string& error)
{
    try
    {
        error.clear();
        result = {};
        const auto data = readPayload(path);
        constexpr std::array<std::uint32_t, 5> payloads{100, 36, 52, 24, 44};
        struct TableView
        {
            std::uint32_t count = 0;
            std::uint32_t payload = 0;
            std::size_t begin = 0;
        };
        std::array<TableView, 5> tables{};
        std::size_t position = 0;
        for (std::size_t tableIndex = 0; tableIndex < tables.size(); ++tableIndex)
        {
            if (position + 8 > data.size())
                throw std::runtime_error("pgdb.bin table header is truncated");
            auto& table = tables[tableIndex];
            table.count = readNumber<std::uint32_t>(data.data(), position);
            table.payload = readNumber<std::uint32_t>(data.data(), position + 4);
            table.begin = position + 8;
            if (table.payload != payloads[tableIndex])
                throw std::runtime_error("unsupported pgdb.bin payload at table " +
                                         std::to_string(tableIndex));
            const auto bytes = static_cast<std::uint64_t>(table.count) *
                               (static_cast<std::uint64_t>(table.payload) + 1U);
            if (bytes > data.size() - table.begin)
                throw std::runtime_error("pgdb.bin table exceeds the file");
            result.tables[tableIndex] = {table.count, table.payload};
            result.rawTables[tableIndex].reserve(table.count);
            for (std::uint32_t rowIndex = 0; rowIndex < table.count; ++rowIndex)
            {
                const auto* row = data.data() + table.begin +
                                  static_cast<std::size_t>(rowIndex) * (table.payload + 1U);
                result.rawTables[tableIndex].push_back(
                    {row[0], std::vector<std::uint8_t>(row + 1, row + table.payload + 1)});
            }
            position = table.begin + static_cast<std::size_t>(bytes);
        }
        if (position != data.size())
            throw std::runtime_error("pgdb.bin has bytes outside its five tables");

        const auto row = [&](std::size_t tableIndex, std::size_t rowIndex) {
            const auto& table = tables.at(tableIndex);
            return data.data() + table.begin + rowIndex * (table.payload + 1U);
        };

        struct StringChunk { std::string text; std::uint32_t next = 0; };
        std::unordered_map<std::uint32_t, StringChunk> stringChunks;
        for (std::size_t index = 0; index < tables[1].count; ++index)
        {
            const auto* value = row(1, index);
            const auto id = readNumber<std::uint32_t>(value, 1);
            if (!id || !stringChunks.emplace(id,
                    StringChunk{fixedString(value, 5, 28), readNumber<std::uint32_t>(value, 33)}).second)
                throw std::runtime_error("duplicate or zero pgdb.bin string chunk ID " + std::to_string(id));
        }
        const auto resolveString = [&](std::uint32_t first) {
            std::string value;
            std::unordered_set<std::uint32_t> visited;
            auto current = first;
            while (current)
            {
                if (!visited.insert(current).second)
                {
                    result.diagnostics.push_back("cycle in pgdb.bin string chain " +
                                                 std::to_string(current));
                    break;
                }
                const auto found = stringChunks.find(current);
                if (found == stringChunks.end())
                {
                    result.diagnostics.push_back("missing pgdb.bin string chunk " +
                                                 std::to_string(current));
                    break;
                }
                value += found->second.text;
                current = found->second.next;
            }
            return value;
        };

        for (std::size_t index = 0; index < tables[4].count; ++index)
        {
            const auto* value = row(4, index);
            SketchProfileChamfer chamfer;
            chamfer.id = readNumber<std::uint32_t>(value, 1);
            chamfer.type = readNumber<std::uint32_t>(value, 5);
            chamfer.x = readNumber<double>(value, 9);
            chamfer.y = readNumber<double>(value, 17);
            chamfer.dz1 = readNumber<double>(value, 25);
            chamfer.dz2 = readNumber<double>(value, 33);
            chamfer.flags = readNumber<std::uint32_t>(value, 41);
            if (!chamfer.id || !std::isfinite(chamfer.x) || !std::isfinite(chamfer.y) ||
                !std::isfinite(chamfer.dz1) || !std::isfinite(chamfer.dz2))
                throw std::runtime_error("invalid pgdb.bin chamfer ID or non-finite geometry");
            const auto floatLimit = static_cast<double>((std::numeric_limits<float>::max)());
            if (std::abs(chamfer.x) > floatLimit || std::abs(chamfer.y) > floatLimit)
                throw std::runtime_error("out-of-range pgdb.bin chamfer coordinate");
            if (!result.chamfers.emplace(chamfer.id, chamfer).second)
                throw std::runtime_error("duplicate pgdb.bin chamfer ID " + std::to_string(chamfer.id));
        }
        for (std::size_t index = 0; index < tables[3].count; ++index)
        {
            const auto* value = row(3, index);
            SketchProfilePoint point;
            point.id = readNumber<std::uint32_t>(value, 1);
            point.chamferId = readNumber<std::uint32_t>(value, 5);
            point.value = {readNumber<double>(value, 9), readNumber<double>(value, 17)};
            if (!point.id || !std::isfinite(point.value[0]) || !std::isfinite(point.value[1]))
                throw std::runtime_error("invalid pgdb.bin point ID or non-finite coordinates");
            if (!result.points.emplace(point.id, point).second)
                throw std::runtime_error("duplicate pgdb.bin point ID " + std::to_string(point.id));
        }

        struct PointBlock
        {
            std::uint32_t flags = 0;
            std::array<std::uint32_t, 10> pointIds{};
            std::uint32_t next = 0;
        };
        std::unordered_map<std::uint32_t, PointBlock> blocks;
        for (std::size_t index = 0; index < tables[2].count; ++index)
        {
            const auto* value = row(2, index);
            PointBlock block;
            const auto id = readNumber<std::uint32_t>(value, 1);
            if (!id)
                throw std::runtime_error("zero pgdb.bin point block ID");
            block.flags = readNumber<std::uint32_t>(value, 5);
            for (std::size_t pointIndex = 0; pointIndex < block.pointIds.size(); ++pointIndex)
                block.pointIds[pointIndex] = readNumber<std::uint32_t>(value, 9 + pointIndex * 4);
            block.next = readNumber<std::uint32_t>(value, 49);
            if (!blocks.emplace(id, block).second)
                throw std::runtime_error("duplicate pgdb.bin point block ID " + std::to_string(id));
        }

        for (std::size_t index = 0; index < tables[0].count; ++index)
        {
            const auto* value = row(0, index);
            SketchProfile profile;
            profile.prefix = fixedString(value, 1, 12);
            profile.suffix = resolveString(readNumber<std::uint32_t>(value, 13));
            profile.name = profile.prefix + profile.suffix;
            profile.firstPointBlockId = readNumber<std::uint32_t>(value, 17);
            profile.flags = readNumber<std::uint32_t>(value, 21);
            for (std::size_t solved = 0; solved < profile.solvedValues.size(); ++solved)
                profile.solvedValues[solved] = readNumber<double>(value, 25 + solved * 8);
            if (!std::all_of(profile.solvedValues.begin(), profile.solvedValues.end(),
                             [](double number) { return std::isfinite(number); }))
                throw std::runtime_error("non-finite pgdb.bin solved profile value");
            profile.rawPayload.assign(value + 1, value + 101);

            std::unordered_set<std::uint32_t> visited;
            auto current = profile.firstPointBlockId;
            std::uint32_t pointIndex = 0;
            while (current)
            {
                if (!visited.insert(current).second)
                {
                    result.diagnostics.push_back("cycle in pgdb.bin point block " +
                                                 std::to_string(current));
                    break;
                }
                const auto block = blocks.find(current);
                if (block == blocks.end())
                {
                    result.diagnostics.push_back("profile " + profile.name +
                                                 " references missing point block " +
                                                 std::to_string(current));
                    break;
                }
                for (const auto pointId : block->second.pointIds)
                {
                    if (!pointId)
                        continue;
                    const auto source = result.points.find(pointId);
                    if (source == result.points.end())
                    {
                        result.diagnostics.push_back("profile " + profile.name +
                                                     " references missing point " +
                                                     std::to_string(pointId));
                        continue;
                    }
                    ProfileContourPoint destination;
                    destination.index = pointIndex++;
                    destination.value = source->second.value;
                    const auto chamfer = result.chamfers.find(source->second.chamferId);
                    if (chamfer != result.chamfers.end())
                    {
                        destination.chamferType = chamfer->second.type;
                        destination.chamferX = static_cast<float>(chamfer->second.x);
                        destination.chamferY = static_cast<float>(chamfer->second.y);
                        destination.chamferDz1 = chamfer->second.dz1;
                        destination.chamferDz2 = chamfer->second.dz2;
                        destination.chamferFlags = chamfer->second.flags;
                    }
                    else if (source->second.chamferId)
                        result.diagnostics.push_back("profile " + profile.name +
                                                     " references missing chamfer " +
                                                     std::to_string(source->second.chamferId));
                    profile.contour.push_back(destination);
                }
                current = block->second.next;
            }
            if (profile.name.empty())
                result.diagnostics.push_back("pgdb.bin contains an unnamed solved profile");
            else if (!result.profiles.emplace(profile.name, std::move(profile)).second)
                throw std::runtime_error("duplicate pgdb.bin solved profile name");
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool parseMaterialCatalog(const std::filesystem::path& path, MaterialCatalog& result, std::string& error)
{
    try
    {
        error.clear();
        result = {};
        const auto data = readPayload(path);
        if (data.size() < 4)
            throw std::runtime_error("matdb.bin is truncated");
        result.version = readNumber<std::uint32_t>(data.data(), 0);
        if (result.version != 3)
            throw std::runtime_error("unsupported matdb.bin version " + std::to_string(result.version));
        constexpr std::array<std::uint32_t, 6> payloads{180, 68, 72, 320, 536, 296};
        std::size_t position = 4;
        for (std::size_t table = 0; table < payloads.size(); ++table)
        {
            if (position + 8 > data.size())
                throw std::runtime_error("matdb.bin table header is truncated");
            const auto count = readNumber<std::uint32_t>(data.data(), position);
            const auto payload = readNumber<std::uint32_t>(data.data(), position + 4);
            position += 8;
            if (payload != payloads[table])
                throw std::runtime_error("unsupported matdb.bin table payload at index " + std::to_string(table));
            const auto tableBytes = static_cast<std::uint64_t>(count) * (payload + 1ULL);
            if (tableBytes > data.size() - position)
                throw std::runtime_error("matdb.bin table exceeds the file");
            result.tables[table] = {count, payload};
            for (std::uint32_t index = 0; index < count; ++index)
            {
                const auto* row = data.data() + position + static_cast<std::size_t>(index) * (payload + 1U);
                if (table == 0)
                {
                    Material material;
                    material.rowTag = row[0];
                    material.category = readNumber<std::uint32_t>(row, 1);
                    material.name = fixedString(row, 5, 32);
                    material.alias = fixedString(row, 37, 64);
                    material.grade = fixedString(row, 101, 36);
                    material.density = readNumber<double>(row, 137);
                    material.secondaryDensity = readNumber<double>(row, 145);
                    material.elasticModulus = readNumber<double>(row, 153);
                    material.poissonRatio = readNumber<double>(row, 161);
                    material.thermalExpansion = readNumber<double>(row, 169);
                    material.flags = readNumber<std::uint32_t>(row, 177);
                    material.rawPayload.assign(row + 1, row + payload + 1);
                    result.materials.push_back(std::move(material));
                }
                else if (table == 1)
                    result.integerAttributes.push_back({row[0], fixedString(row, 1, 32), fixedString(row, 33, 32),
                                                        readNumber<std::uint32_t>(row, 65)});
                else if (table == 2)
                    result.attributeReferences.push_back({row[0], fixedString(row, 1, 32), fixedString(row, 33, 40)});
                else if (table == 3)
                    result.stringAttributes.push_back({row[0], fixedString(row, 1, 32), fixedString(row, 33, 32),
                                                       fixedString(row, 65, 256)});
                else if (table == 4)
                {
                    MaterialAttributeDefinition definition;
                    definition.rowTag = row[0];
                    definition.id = readNumber<std::uint32_t>(row, 1);
                    definition.valueKind = readNumber<std::uint32_t>(row, 5);
                    definition.scope = readNumber<std::uint32_t>(row, 9);
                    definition.flags = readNumber<std::uint32_t>(row, 13);
                    definition.name = fixedString(row, 17, 256);
                    definition.label = fixedString(row, 273, 256);
                    definition.rawPayload.assign(row + 1, row + payload + 1);
                    result.attributeDefinitions.push_back(std::move(definition));
                }
                else
                    result.labels.push_back({row[0], readNumber<std::uint32_t>(row, 1), fixedString(row, 5, 32),
                                             fixedString(row, 37, 260)});
            }
            position += static_cast<std::size_t>(tableBytes);
        }
        if (position != data.size())
            throw std::runtime_error("matdb.bin has trailing bytes outside its six tables");
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool parseBoltCatalog(const std::filesystem::path& path, BoltCatalog& result, std::string& error)
{
    try
    {
        error.clear();
        result = {};
        const auto data = readPayload(path);
        if (data.size() < 12)
            throw std::runtime_error("screwdb.db is truncated");
        result.version = readNumber<std::uint32_t>(data.data(), 0);
        result.table = {readNumber<std::uint32_t>(data.data(), 4), readNumber<std::uint32_t>(data.data(), 8)};
        if (result.version != 3 || result.table.payloadSize != 116)
            throw std::runtime_error("unsupported screwdb.db schema");
        const auto expected = 12ULL + static_cast<std::uint64_t>(result.table.recordCount) * 117ULL;
        if (expected != data.size())
            throw std::runtime_error("screwdb.db record count does not match the file size");
        for (std::uint32_t index = 0; index < result.table.recordCount; ++index)
        {
            const auto* row = data.data() + 12 + static_cast<std::size_t>(index) * 117;
            BoltCatalogEntry bolt;
            bolt.rowTag = row[0];
            bolt.name = fixedString(row, 1, 32);
            bolt.diameter = readNumber<float>(row, 45);
            bolt.length = readNumber<float>(row, 49);
            for (std::size_t field = 0; field < bolt.trailingDimensions.size(); ++field)
                bolt.trailingDimensions[field] = readNumber<float>(row, 89 + field * 4);
            bolt.rawPayload.assign(row + 1, row + 117);
            bolt.standard = fixedString(row, 53, 28);
            result.bolts.push_back(std::move(bolt));
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool parseBoltAssemblyCatalog(const std::filesystem::path& path, BoltAssemblyCatalog& result, std::string& error)
{
    try
    {
        error.clear();
        result = {};
        const auto data = readPayload(path);
        if (data.size() < 12)
            throw std::runtime_error("assdb.db is truncated");
        result.version = readNumber<std::uint32_t>(data.data(), 0);
        result.table = {readNumber<std::uint32_t>(data.data(), 4), readNumber<std::uint32_t>(data.data(), 8)};
        if (result.version != 3 || result.table.payloadSize != 355)
            throw std::runtime_error("unsupported assdb.db schema");
        const auto expected = 12ULL + static_cast<std::uint64_t>(result.table.recordCount) * 356ULL;
        if (expected != data.size())
            throw std::runtime_error("assdb.db record count does not match the file size");
        for (std::uint32_t index = 0; index < result.table.recordCount; ++index)
        {
            const auto* row = data.data() + 12 + static_cast<std::size_t>(index) * 356;
            BoltAssemblyCatalogEntry assembly;
            assembly.rowTag = row[0];
            assembly.name = fixedString(row, 1, 31);
            for (std::size_t component = 0; component < assembly.componentNames.size(); ++component)
                assembly.componentNames[component] = fixedString(row, 32 + component * 26, 26);
            assembly.rawPayload.assign(row + 1, row + 356);
            result.assemblies.push_back(std::move(assembly));
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool parseProfitabCatalog(const std::filesystem::path& path, ProfitabCatalog& result, std::string& error)
{
    try
    {
        error.clear();
        result = {};
        std::istringstream stream(readText(path));
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const auto cleaned = trim(line);
            if (cleaned.empty() || cleaned.rfind("/*", 0) == 0)
                continue;
            const auto fields = split(cleaned, '!');
            if (fields.size() < 6)
            {
                result.diagnostics.push_back("line " + std::to_string(lineNumber) + " has fewer than six fields");
                continue;
            }
            ParametricProfileRule rule;
            rule.prefix = fields[0];
            rule.type = fields[1];
            std::array<int*, 4> numericFields{&rule.sortOrder, &rule.unit,
                                               &rule.minimumNumbers, &rule.maximumNumbers};
            bool validNumericFields = true;
            for (std::size_t field = 0; field < numericFields.size(); ++field)
                if (!optionalInteger(fields[field + 2], *numericFields[field]))
                {
                    result.diagnostics.push_back("line " + std::to_string(lineNumber) +
                                                 " has an invalid integer in field " +
                                                 std::to_string(field + 3));
                    validNumericFields = false;
                }
            if (!validNumericFields)
                continue;
            rule.generator = fields.size() > 6 ? fields[6] : std::string{};
            const auto separator = rule.generator.find('.');
            if (separator != std::string::npos && separator != 0 && separator + 1 < rule.generator.size() &&
                rule.generator.find('.', separator + 1) == std::string::npos)
            {
                rule.generatorLibraryId = rule.generator.substr(0, separator);
                rule.generatorSectionName = rule.generator.substr(separator + 1);
            }
            rule.namePattern = fields.size() > 7 ? fields[7] : std::string{};
            rule.sourceLine = line;
            result.rules.push_back(std::move(rule));
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool parseClbDocument(const std::filesystem::path& path, ClbDocument& result, std::string& error)
{
    try
    {
        error.clear();
        result = {};
        std::istringstream stream(readText(path));
        struct SourceLine { std::size_t number = 0; std::string value; };
        std::vector<SourceLine> lines;
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(stream, line))
        {
            ++lineNumber;
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            line = trim(stripLineComment(line));
            if (!line.empty())
                lines.push_back({lineNumber, line});
        }

        std::vector<std::vector<ClbStatement>*> stack{&result.statements};
        for (std::size_t index = 0; index < lines.size(); ++index)
        {
            auto current = lines[index].value;
            const auto statementLine = lines[index].number;
            if (current == "}")
            {
                if (stack.size() == 1)
                    throw std::runtime_error("unmatched CLB closing brace at line " + std::to_string(lines[index].number));
                stack.pop_back();
                continue;
            }

            bool opens = !current.empty() && current.back() == '{';
            if (opens)
                current = trim(current.substr(0, current.size() - 1));
            else if (index + 1 < lines.size() && lines[index + 1].value == "{")
            {
                opens = true;
                ++index;
            }
            auto tokens = clbTokens(current);
            if (tokens.empty())
            {
                if (opens)
                {
                    // Some official environment catalogs contain a duplicated bare
                    // opening brace.  It does not introduce a named node and the
                    // matching close still belongs to the surrounding statement.
                    result.diagnostics.push_back("ignored anonymous opening brace at line " +
                                                 std::to_string(statementLine));
                }
                continue;
            }
            ClbStatement statement;
            statement.keyword = tokens.front();
            statement.arguments.assign(tokens.begin() + 1, tokens.end());
            statement.line = statementLine;
            stack.back()->push_back(std::move(statement));
            if (opens)
                stack.push_back(&stack.back()->back().children);
        }
        if (stack.size() != 1)
            throw std::runtime_error("CLB document ends inside an open block");

        const auto lower = [](std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        };
        const auto namedValue = [&](const std::vector<ClbStatement>& statements, const std::string& keyword) {
            std::vector<std::string> values;
            for (const auto& statement : statements)
                if (lower(statement.keyword) == keyword && !statement.arguments.empty())
                    values.push_back(statement.arguments.front());
            return values;
        };
        const auto argumentText = [](const ClbStatement& statement) {
            std::string value;
            for (const auto& argument : statement.arguments)
            {
                if (!value.empty()) value.push_back(' ');
                value += argument;
            }
            return value;
        };
        const auto fieldValue = [&](const std::vector<ClbStatement>& statements, const std::string& keyword,
                                    std::size_t ownerLine, bool required = false) {
            std::vector<const ClbStatement*> matches;
            for (const auto& candidate : statements)
                if (lower(candidate.keyword) == keyword) matches.push_back(&candidate);
            if (matches.size() != 1 || matches.front()->arguments.empty())
            {
                if (required || !matches.empty())
                    result.diagnostics.push_back("CLB field '" + keyword + "' at line " + std::to_string(ownerLine) +
                                                 " is missing or ambiguous");
                return std::string{};
            }
            return argumentText(*matches.front());
        };
        const auto parseIndex = [](const std::string& text) -> std::optional<std::uint32_t> {
            if (text.empty() || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c); }))
                return std::nullopt;
            char* end = nullptr;
            errno = 0;
            const auto value = std::strtoul(text.c_str(), &end, 10);
            if (errno || end == text.c_str() || *end != '\0' || value > std::numeric_limits<std::uint32_t>::max())
                return std::nullopt;
            return static_cast<std::uint32_t>(value);
        };
        const auto libraries = namedValue(result.statements, "library_id");
        if (libraries.size() == 1)
            result.libraryId = libraries.front();
        else if (libraries.size() > 1)
            result.diagnostics.push_back("multiple top-level library_id declarations; library identity is ambiguous");

        for (const auto& statement : result.statements)
            if (lower(statement.keyword) == "include")
            {
                if (statement.arguments.size() == 1)
                    result.includes.push_back({statement.arguments.front(), statement.line});
                else
                    result.diagnostics.push_back("Include at line " + std::to_string(statement.line) +
                                                 " has missing or ambiguous target");
            }

        for (const auto& statement : result.statements)
        {
            if (lower(statement.keyword) != "section_type") continue;
            ClbSection section;
            section.line = statement.line;
            const auto names = namedValue(statement.children, "name");
            if (names.size() == 1) section.name = names.front();
            else result.diagnostics.push_back("Section_type at line " + std::to_string(statement.line) +
                                               " has missing or ambiguous name");
            for (const auto& child : statement.children)
            {
                const auto keyword = lower(child.keyword);
                if (keyword == "base_attribute" || keyword == "expression")
                {
                    ClbNamedValue value;
                    value.name = fieldValue(child.children, "name", child.line, true);
                    value.type = fieldValue(child.children, "type", child.line, true);
                    value.description = fieldValue(child.children, "description", child.line);
                    value.defaultValue = fieldValue(child.children, "default", child.line, true);
                    if (keyword == "expression") value.formula = fieldValue(child.children, "formula", child.line);
                    value.line = child.line;
                    if (value.name.empty()) continue;
                    if (keyword == "base_attribute") section.baseAttributes.push_back(std::move(value));
                    else section.expressions.push_back(std::move(value));
                    continue;
                }
                if (keyword != "geometry") continue;
                ClbGeometry geometry;
                geometry.line = child.line;
                geometry.name = fieldValue(child.children, "name", child.line, true);
                for (const auto& faceStatement : child.children)
                {
                    if (lower(faceStatement.keyword) != "face") continue;
                    ClbFace face;
                    face.line = faceStatement.line;
                    const auto faceIndex = fieldValue(faceStatement.children, "index", faceStatement.line, true);
                    face.index = parseIndex(faceIndex);
                    if (!faceIndex.empty() && !face.index)
                        result.diagnostics.push_back("CLB face index at line " + std::to_string(faceStatement.line) +
                                                     " is not an unsigned 32-bit integer");
                    for (const auto& pointStatement : faceStatement.children)
                    {
                        if (lower(pointStatement.keyword) != "point") continue;
                        ClbPointReference point;
                        point.line = pointStatement.line;
                        if (pointStatement.arguments.size() == 3)
                        {
                            point.stationIndex = parseIndex(pointStatement.arguments[0]);
                            point.yReference = pointStatement.arguments[1];
                            point.zReference = pointStatement.arguments[2];
                        }
                        if (!point.stationIndex || point.yReference.empty() || point.zReference.empty())
                            result.diagnostics.push_back("CLB point at line " + std::to_string(pointStatement.line) +
                                                         " must contain station, y-reference, and z-reference");
                        else if (*point.stationIndex > 1)
                            result.diagnostics.push_back("CLB point station at line " + std::to_string(pointStatement.line) +
                                                         " is outside the documented start/end indices 0 and 1");
                        face.points.push_back(std::move(point));
                    }
                    geometry.faces.push_back(std::move(face));
                }
                section.geometries.push_back(std::move(geometry));
            }
            result.sections.push_back(std::move(section));
        }
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool parseProfileCatalogLis(const std::filesystem::path& path, ProfileCatalogLis& result, std::string& error)
{
    result = {};
    error.clear();
    try
    {
        constexpr std::uintmax_t maxBytes = 64u * 1024u * 1024u;
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(path, sizeError);
        if (sizeError || size > maxBytes)
            throw std::runtime_error("profile .lis file is missing or exceeds 64 MiB");
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("could not open profile .lis file");
        std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (!input.eof() && input.fail()) throw std::runtime_error("could not read profile .lis file");
        if (text.size() != size) throw std::runtime_error("profile .lis file changed while being read");
        if (text.find('\0') != std::string::npos) throw std::runtime_error("profile .lis contains a NUL byte");
        result.rawText = text;
        result.sourcePath = path;

        struct Line { std::string text; std::size_t number; };
        std::vector<Line> lines;
        std::istringstream stream(text);
        std::string line;
        std::size_t lineNumber = 0;
        while (std::getline(stream, line)) lines.push_back({line, ++lineNumber});
        if (lines.empty()) throw std::runtime_error("profile .lis is empty");

        const auto trim = [](std::string value)
        {
            const auto first = value.find_first_not_of(" \t\r");
            if (first == std::string::npos) return std::string{};
            const auto last = value.find_last_not_of(" \t\r");
            return value.substr(first, last - first + 1);
        };
        std::size_t cursor = 0;
        while (cursor < lines.size() && trim(lines[cursor].text).empty()) ++cursor;
        if (cursor == lines.size()) throw std::runtime_error("profile .lis has no version header");
        auto header = trim(lines[cursor].text);
        constexpr char bom[] = "\xEF\xBB\xBF";
        if (header.compare(0, 3, bom) == 0) header.erase(0, 3);
        constexpr std::string_view databasePrefix = "PROFILE DATABASE EXPORT VERSION";
        constexpr std::string_view catalogPrefix = "PROFILE CATALOG EXPORT VERSION";
        const bool teklaCatalogExport = header.compare(0, catalogPrefix.size(), catalogPrefix) == 0;
        const auto prefix = teklaCatalogExport ? catalogPrefix : databasePrefix;
        if (header.compare(0, prefix.size(), prefix) != 0)
            throw std::runtime_error("unsupported profile .lis header at line " + std::to_string(lines[cursor].number));
        const auto equal = header.find('=', prefix.size());
        if (equal == std::string::npos || trim(header.substr(prefix.size(), equal - prefix.size())) != "")
            throw std::runtime_error("malformed profile .lis version header at line " + std::to_string(lines[cursor].number));
        const auto versionText = trim(header.substr(equal + 1));
        std::uint32_t version = 0;
        const auto versionResult = std::from_chars(versionText.data(), versionText.data() + versionText.size(), version);
        if (versionResult.ec != std::errc{} || versionResult.ptr != versionText.data() + versionText.size() ||
            (!teklaCatalogExport && version != 2) || (teklaCatalogExport && version == 0))
            throw std::runtime_error("unsupported profile .lis export version at line " + std::to_string(lines[cursor].number));
        result.version = version;
        result.format = teklaCatalogExport ? ProfileCatalogLisFormat::TeklaProfileCatalogExport
                                           : ProfileCatalogLisFormat::ThirdPartyProfileDatabaseExportV2;
        ++cursor;

        const auto nextNonempty = [&]() -> const Line&
        {
            while (cursor < lines.size() && trim(lines[cursor].text).empty()) ++cursor;
            if (cursor == lines.size()) throw std::runtime_error("unexpected end of profile .lis file");
            return lines[cursor];
        };
        const auto parseUnsigned = [&](const std::string& value, std::size_t at) -> std::uint32_t
        {
            const auto token = trim(value);
            std::uint32_t parsed = 0;
            const auto converted = std::from_chars(token.data(), token.data() + token.size(), parsed);
            if (token.empty() || converted.ec != std::errc{} || converted.ptr != token.data() + token.size())
                throw std::runtime_error("invalid unsigned integer at line " + std::to_string(at));
            return parsed;
        };
        const auto parseNumber = [&](const std::string& value, std::size_t at) -> double
        {
            const auto token = trim(value);
            double parsed = 0.0;
            const auto converted = std::from_chars(token.data(), token.data() + token.size(), parsed,
                                                   std::chars_format::general);
            if (token.empty() || converted.ec != std::errc{} || converted.ptr != token.data() + token.size() ||
                !std::isfinite(parsed))
                throw std::runtime_error("invalid finite number at line " + std::to_string(at));
            return parsed;
        };
        std::unordered_set<std::string> duplicateNames;
        std::unordered_set<std::string> seenNames;
        while (cursor < lines.size())
        {
            while (cursor < lines.size() && trim(lines[cursor].text).empty()) ++cursor;
            if (cursor == lines.size()) break;
            const auto start = cursor;
            const auto declaration = trim(lines[cursor].text);
            constexpr std::string_view namePrefix = "PROFILE_NAME";
            constexpr std::string_view crossSectionPrefix = "CROSS_SECTION_NAME";
            if (teklaCatalogExport && declaration.compare(0, crossSectionPrefix.size(), crossSectionPrefix) == 0)
            {
                const auto nameEqual = declaration.find('=', crossSectionPrefix.size());
                if (nameEqual == std::string::npos ||
                    !trim(declaration.substr(crossSectionPrefix.size(), nameEqual - crossSectionPrefix.size())).empty())
                    throw std::runtime_error("malformed CROSS_SECTION_NAME at line " + std::to_string(lines[cursor].number));
                auto quoted = trim(declaration.substr(nameEqual + 1));
                if (!quoted.empty() && quoted.back() == ';') quoted = trim(quoted.substr(0, quoted.size() - 1));
                if (quoted.size() < 2 || quoted.front() != '"' || quoted.back() != '"')
                    throw std::runtime_error("cross-section name must be quoted at line " + std::to_string(lines[cursor].number));
                ProfileCatalogLisCrossSection crossSection;
                crossSection.name = quoted.substr(1, quoted.size() - 2);
                if (crossSection.name.empty())
                    throw std::runtime_error("empty cross-section name at line " + std::to_string(lines[cursor].number));
                if (std::any_of(result.crossSections.begin(), result.crossSections.end(), [&](const auto& existing) {
                        return existing.name == crossSection.name;
                    }))
                    throw std::runtime_error("duplicate cross-section name at line " + std::to_string(lines[cursor].number));
                ++cursor;

                struct PendingPoint
                {
                    ProfileCatalogLisCrossSectionPoint point;
                    bool hasNumber = false;
                    bool hasX = false;
                    bool hasY = false;
                    bool hasChamferType = false;
                    bool hasChamferX = false;
                    bool hasChamferY = false;
                    bool empty() const { return !hasNumber; }
                } pending;
                std::unordered_set<std::uint32_t> pointNumbers;
                const auto finishPoint = [&]() {
                    if (pending.empty()) return;
                    if (!pending.hasX || !pending.hasY)
                        throw std::runtime_error("cross-section point is missing POINT_X or POINT_Y");
                    pending.point.hasChamferType = pending.hasChamferType;
                    pending.point.hasChamferX = pending.hasChamferX;
                    pending.point.hasChamferY = pending.hasChamferY;
                    crossSection.points.push_back(pending.point);
                    pending = {};
                };
                while (cursor < lines.size())
                {
                    const auto item = trim(lines[cursor].text);
                    if (item.compare(0, namePrefix.size(), namePrefix) == 0 ||
                        item.compare(0, crossSectionPrefix.size(), crossSectionPrefix) == 0)
                        break;
                    const auto fieldEqual = item.find('=');
                    if (fieldEqual != std::string::npos)
                    {
                        const auto key = trim(item.substr(0, fieldEqual));
                        auto value = trim(item.substr(fieldEqual + 1));
                        if (!value.empty() && value.back() == ';') value = trim(value.substr(0, value.size() - 1));
                        if (key == "POINT_NUMBER")
                        {
                            finishPoint();
                            pending.point.pointNumber = parseUnsigned(value, lines[cursor].number);
                            pending.hasNumber = true;
                            if (!pointNumbers.emplace(pending.point.pointNumber).second)
                                throw std::runtime_error("duplicate cross-section point number at line " + std::to_string(lines[cursor].number));
                        }
                        else if (key == "POINT_X" || key == "POINT_Y" || key == "CHAMFER_TYPE" ||
                                 key == "CHAMFER_X" || key == "CHAMFER_Y")
                        {
                            if (pending.empty())
                                throw std::runtime_error("cross-section coordinate or chamfer precedes POINT_NUMBER at line " +
                                                         std::to_string(lines[cursor].number));
                            bool* seen = nullptr;
                            if (key == "POINT_X") seen = &pending.hasX;
                            else if (key == "POINT_Y") seen = &pending.hasY;
                            else if (key == "CHAMFER_TYPE") seen = &pending.hasChamferType;
                            else if (key == "CHAMFER_X") seen = &pending.hasChamferX;
                            else seen = &pending.hasChamferY;
                            if (*seen)
                                throw std::runtime_error("duplicate cross-section field " + key + " at line " +
                                                         std::to_string(lines[cursor].number));
                            *seen = true;
                            if (key == "CHAMFER_TYPE") pending.point.chamferType = parseUnsigned(value, lines[cursor].number);
                            else
                            {
                                const auto number = parseNumber(value, lines[cursor].number);
                                if (key == "POINT_X") pending.point.x = number;
                                else if (key == "POINT_Y") pending.point.y = number;
                                else if (key == "CHAMFER_X") pending.point.chamferX = number;
                                else pending.point.chamferY = number;
                            }
                        }
                    }
                    ++cursor;
                }
                finishPoint();
                if (crossSection.points.empty())
                    throw std::runtime_error("cross-section has no POINT_NUMBER records");
                for (std::size_t index = start; index < cursor; ++index)
                {
                    crossSection.rawRecord += lines[index].text;
                    if (index + 1 < cursor) crossSection.rawRecord.push_back('\n');
                }
                result.crossSections.push_back(std::move(crossSection));
                continue;
            }
            if (declaration.compare(0, namePrefix.size(), namePrefix) != 0)
            {
                if (!teklaCatalogExport)
                    throw std::runtime_error("expected PROFILE_NAME at line " + std::to_string(lines[cursor].number));
                // Native catalog exports include a tree section before the fixed
                // profile records. Preserve it in rawText while interpreting only
                // documented PROFILE_NAME records.
                ++cursor;
                continue;
            }
            const auto nameEqual = declaration.find('=', namePrefix.size());
            if (nameEqual == std::string::npos || !trim(declaration.substr(namePrefix.size(), nameEqual - namePrefix.size())).empty())
                throw std::runtime_error("malformed PROFILE_NAME at line " + std::to_string(lines[cursor].number));
            auto quoted = trim(declaration.substr(nameEqual + 1));
            if (quoted.size() < 2 || quoted.front() != '"')
                throw std::runtime_error("profile name must be quoted at line " + std::to_string(lines[cursor].number));
            const auto closingQuote = quoted.find('"', 1);
            const auto nameTail = closingQuote == std::string::npos ? std::string{} : trim(quoted.substr(closingQuote + 1));
            if (closingQuote == std::string::npos || (!nameTail.empty() && nameTail != ";"))
                throw std::runtime_error("malformed quoted profile name at line " + std::to_string(lines[cursor].number));
            ProfileCatalogLisEntry entry;
            entry.name = quoted.substr(1, closingQuote - 1);
            if (entry.name.empty()) throw std::runtime_error("empty profile name at line " + std::to_string(lines[cursor].number));
            if (!seenNames.emplace(entry.name).second) duplicateNames.emplace(entry.name);
            ++cursor;

            const auto& outerOpen = nextNonempty();
            if (trim(outerOpen.text) != "{") throw std::runtime_error("expected profile block at line " + std::to_string(outerOpen.number));
            ++cursor;
            const auto& typeLine = nextNonempty();
            std::unordered_map<std::string, std::string> headerFields;
            std::istringstream typeStream(typeLine.text);
            std::string field;
            while (std::getline(typeStream, field, ';'))
            {
                field = trim(field);
                if (field.empty()) continue;
                const auto fieldEqual = field.find('=');
                if (fieldEqual == std::string::npos)
                    throw std::runtime_error("malformed profile type field at line " + std::to_string(typeLine.number));
                const auto key = trim(field.substr(0, fieldEqual));
                const auto value = trim(field.substr(fieldEqual + 1));
                if ((key != "TYPE" && key != "SUB_TYPE" && key != "COORDINATE") ||
                    !headerFields.emplace(key, value).second)
                    throw std::runtime_error("unsupported or duplicate profile type field at line " + std::to_string(typeLine.number));
            }
            if (headerFields.size() != 3)
                throw std::runtime_error("profile type line must contain TYPE, SUB_TYPE, and COORDINATE at line " + std::to_string(typeLine.number));
            entry.type = parseUnsigned(headerFields.at("TYPE"), typeLine.number);
            entry.subType = parseUnsigned(headerFields.at("SUB_TYPE"), typeLine.number);
            entry.storedCoordinate = parseNumber(headerFields.at("COORDINATE"), typeLine.number);
            ++cursor;

            const auto& propertiesOpen = nextNonempty();
            if (trim(propertiesOpen.text) != "{")
                throw std::runtime_error("expected profile properties block at line " + std::to_string(propertiesOpen.number));
            ++cursor;
            for (;;)
            {
                const auto& propertyLine = nextNonempty();
                const auto property = trim(propertyLine.text);
                if (property == "}") { ++cursor; break; }
                if (property.empty() || property.front() != '"')
                    throw std::runtime_error("expected quoted numeric property at line " + std::to_string(propertyLine.number));
                const auto propertyEnd = property.find('"', 1);
                if (propertyEnd == std::string::npos)
                    throw std::runtime_error("unterminated property name at line " + std::to_string(propertyLine.number));
                const auto key = property.substr(1, propertyEnd - 1);
                if (key.empty() || entry.properties.find(key) != entry.properties.end())
                    throw std::runtime_error("empty or duplicate property name at line " + std::to_string(propertyLine.number));
                const auto rawValue = trim(property.substr(propertyEnd + 1));
                entry.properties.emplace(key, parseNumber(rawValue, propertyLine.number));
                ++cursor;
            }
            const auto& outerClose = nextNonempty();
            if (trim(outerClose.text) != "}")
                throw std::runtime_error("expected end of profile block at line " + std::to_string(outerClose.number));
            ++cursor;
            for (std::size_t index = start; index < cursor; ++index)
            {
                entry.rawRecord += lines[index].text;
                if (index + 1 < cursor) entry.rawRecord.push_back('\n');
            }
            result.entries.push_back(std::move(entry));
        }
        if (result.entries.empty())
            throw std::runtime_error("profile .lis contains no supported PROFILE_NAME records");
        result.ambiguousProfileNames.assign(duplicateNames.begin(), duplicateNames.end());
        std::sort(result.ambiguousProfileNames.begin(), result.ambiguousProfileNames.end());
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

}
