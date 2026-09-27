#include <tekla/db1/Catalogs.hpp>

#include <zlib.h>
#include "Path.hpp"
#include "BinaryIO.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
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

std::string readText(const std::filesystem::path& path)
{
    const auto bytes = readPayload(path);
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

int optionalInteger(const std::string& value)
{
    if (value.empty())
        return 0;
    char* end = nullptr;
    const auto number = std::strtol(value.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int>(number) : 0;
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
            stringChunks[readNumber<std::uint32_t>(value, 1)] =
                {fixedString(value, 5, 28), readNumber<std::uint32_t>(value, 33)};
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
            result.chamfers[chamfer.id] = chamfer;
        }
        for (std::size_t index = 0; index < tables[3].count; ++index)
        {
            const auto* value = row(3, index);
            SketchProfilePoint point;
            point.id = readNumber<std::uint32_t>(value, 1);
            point.chamferId = readNumber<std::uint32_t>(value, 5);
            point.value = {readNumber<double>(value, 9), readNumber<double>(value, 17)};
            result.points[point.id] = point;
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
            block.flags = readNumber<std::uint32_t>(value, 5);
            for (std::size_t pointIndex = 0; pointIndex < block.pointIds.size(); ++pointIndex)
                block.pointIds[pointIndex] = readNumber<std::uint32_t>(value, 9 + pointIndex * 4);
            block.next = readNumber<std::uint32_t>(value, 49);
            blocks[id] = block;
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
                    }
                    profile.contour.push_back(destination);
                }
                current = block->second.next;
            }
            if (profile.name.empty())
                result.diagnostics.push_back("pgdb.bin contains an unnamed solved profile");
            else
                result.profiles[profile.name] = std::move(profile);
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
            rule.sortOrder = fields.size() > 2 ? optionalInteger(fields[2]) : 0;
            rule.unit = fields.size() > 3 ? optionalInteger(fields[3]) : 0;
            rule.minimumNumbers = fields.size() > 4 ? optionalInteger(fields[4]) : 0;
            rule.maximumNumbers = fields.size() > 5 ? optionalInteger(fields[5]) : 0;
            rule.generator = fields.size() > 6 ? fields[6] : std::string{};
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
