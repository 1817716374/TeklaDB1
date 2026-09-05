#include <tekla/db1/Catalogs.hpp>

#include <zlib.h>

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

std::vector<std::uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("cannot open " + path.u8string());
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 0)
        throw std::runtime_error("cannot determine file size: " + path.u8string());
    stream.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    if (!result.empty() && !stream.read(reinterpret_cast<char*>(result.data()), size))
        throw std::runtime_error("cannot read " + path.u8string());
    return result;
}

std::vector<std::uint8_t> readPayload(const std::filesystem::path& path)
{
    std::ifstream inputStream(path, std::ios::binary);
    if (!inputStream)
        throw std::runtime_error("cannot open " + path.u8string());
    std::array<std::uint8_t, 2> signature{};
    inputStream.read(reinterpret_cast<char*>(signature.data()), signature.size());
    if (inputStream.gcount() != static_cast<std::streamsize>(signature.size()) ||
        signature[0] != 0x1f || signature[1] != 0x8b)
        return readFile(path);
    inputStream.clear();
    inputStream.seekg(0, std::ios::beg);
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        throw std::runtime_error("zlib failed to initialize for " + path.u8string());
    std::vector<std::uint8_t> output;
    std::array<std::uint8_t, 256 * 1024> input{};
    std::array<std::uint8_t, 256 * 1024> chunk{};
    int status = Z_OK;
    while (status == Z_OK && inputStream)
    {
        inputStream.read(reinterpret_cast<char*>(input.data()), input.size());
        stream.next_in = input.data();
        stream.avail_in = static_cast<uInt>(inputStream.gcount());
        do
        {
            stream.next_out = chunk.data();
            stream.avail_out = static_cast<uInt>(chunk.size());
            status = inflate(&stream, Z_NO_FLUSH);
            output.insert(output.end(), chunk.data(), chunk.data() + chunk.size() - stream.avail_out);
        }
        while (status == Z_OK && (stream.avail_in || stream.avail_out == 0));
    }
    inflateEnd(&stream);
    if (status != Z_STREAM_END)
        throw std::runtime_error("invalid gzip stream: " + path.u8string());
    return output;
}

std::string readText(const std::filesystem::path& path)
{
    const auto bytes = readPayload(path);
    std::string result(bytes.begin(), bytes.end());
    if (result.size() >= 3 && static_cast<unsigned char>(result[0]) == 0xef &&
        static_cast<unsigned char>(result[1]) == 0xbb && static_cast<unsigned char>(result[2]) == 0xbf)
        result.erase(0, 3);
    return result;
}

std::string xmlDecode(std::string value)
{
    const std::pair<const char*, const char*> replacements[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}};
    for (const auto& replacement : replacements)
    {
        std::size_t position = 0;
        while ((position = value.find(replacement.first, position)) != std::string::npos)
        {
            value.replace(position, std::strlen(replacement.first), replacement.second);
            position += std::strlen(replacement.second);
        }
    }
    return value;
}

std::string elementText(const std::string& xml, const std::string& tag, std::size_t begin = 0)
{
    const auto open = xml.find("<" + tag + ">", begin);
    if (open == std::string::npos)
        return {};
    const auto content = open + tag.size() + 2;
    const auto close = xml.find("</" + tag + ">", content);
    if (close == std::string::npos)
        throw std::runtime_error("unterminated XML element " + tag);
    return xmlDecode(xml.substr(content, close - content));
}

std::string elementOpeningTag(const std::string& xml, const std::string& tag, std::size_t begin = 0)
{
    const auto open = xml.find("<" + tag, begin);
    if (open == std::string::npos)
        return {};
    const auto close = xml.find('>', open);
    if (close == std::string::npos)
        throw std::runtime_error("unterminated XML opening tag " + tag);
    return xml.substr(open, close - open + 1);
}

std::string xmlAttribute(const std::string& openingTag, const std::string& name)
{
    const auto marker = name + "=\"";
    const auto begin = openingTag.find(marker);
    if (begin == std::string::npos)
        return {};
    const auto valueBegin = begin + marker.size();
    const auto end = openingTag.find('"', valueBegin);
    if (end == std::string::npos)
        throw std::runtime_error("unterminated XML attribute " + name);
    return xmlDecode(openingTag.substr(valueBegin, end - valueBegin));
}

double parseDouble(const std::string& value, const char* field)
{
    if (value.empty())
        throw std::runtime_error(std::string("missing numeric value for ") + field);
    char* end = nullptr;
    errno = 0;
    const auto result = std::strtod(value.c_str(), &end);
    if (errno == ERANGE || !end || *end != '\0')
        throw std::runtime_error(std::string("invalid numeric value for ") + field + ": " + value);
    return result;
}

std::uint32_t parseUnsigned(const std::string& value, const char* field)
{
    const auto number = parseDouble(value, field);
    if (number < 0.0 || number > static_cast<double>((std::numeric_limits<std::uint32_t>::max)()) ||
        number != static_cast<double>(static_cast<std::uint32_t>(number)))
        throw std::runtime_error(std::string("invalid unsigned integer for ") + field + ": " + value);
    return static_cast<std::uint32_t>(number);
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

std::vector<std::uint32_t> parseIndices(const std::string& xml)
{
    std::vector<std::uint32_t> result;
    std::size_t position = 0;
    while (true)
    {
        const auto open = xml.find("<Index>", position);
        if (open == std::string::npos)
            return result;
        const auto content = open + 7;
        const auto close = xml.find("</Index>", content);
        if (close == std::string::npos)
            throw std::runtime_error("unterminated shape Index element");
        result.push_back(parseUnsigned(xml.substr(content, close - content), "shape index"));
        position = close + 8;
    }
}

std::vector<std::string> elementBlocks(const std::string& xml, const std::string& tag)
{
    std::vector<std::string> result;
    const auto openTag = "<" + tag + ">";
    const auto closeTag = "</" + tag + ">";
    std::size_t position = 0;
    while (true)
    {
        const auto open = xml.find(openTag, position);
        if (open == std::string::npos)
            return result;
        const auto content = open + openTag.size();
        const auto close = xml.find(closeTag, content);
        if (close == std::string::npos)
            throw std::runtime_error("unterminated XML block " + tag);
        result.push_back(xml.substr(content, close - content));
        position = close + closeTag.size();
    }
}
}

bool parseProfileGeometryCatalog(const std::filesystem::path& path,
                                 ProfileGeometryCatalog& result, std::string& error)
{
    try
    {
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

bool parseModelMetadata(const std::filesystem::path& path, ModelMetadata& result, std::string& error)
{
    try
    {
        result = {};
        const auto xml = readText(path);
        if (xml.find("<TeklaStructuresModels") == std::string::npos ||
            xml.find("<Model>") == std::string::npos)
            throw std::runtime_error("XML is not TeklaStructuresModel metadata");
        result.name = elementText(xml, "Name");
        result.designer = elementText(xml, "Designer");
        result.description = elementText(xml, "Description");
        result.version = elementText(xml, "Version");
        result.productVersion = elementText(xml, "ProductVersion");
        result.language = elementText(xml, "Language");
        result.templateName = elementText(xml, "Template");
        result.environment = elementText(xml, "Environment");
        auto isTemplate = elementText(xml, "IsTemplate");
        std::transform(isTemplate.begin(), isTemplate.end(), isTemplate.begin(),
                       [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
        result.isTemplate = isTemplate == "TRUE" || isTemplate == "1";
        result.projectSearchPath = elementText(xml, "XS_PROJECT");
        result.firmSearchPath = elementText(xml, "XS_FIRM");
        result.systemSearchPath = elementText(xml, "XS_SYSTEM");
        result.connectedId = elementText(xml, "ConnectedId");
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

bool parseShapeDefinition(const std::filesystem::path& path, ShapeDefinition& result, std::string& error)
{
    try
    {
        result = {};
        const auto xml = readText(path);
        if (xml.find("<ImportPart") == std::string::npos)
            throw std::runtime_error("XML is not a Tekla ImportPart shape definition");
        result.name = elementText(xml, "Name");
        result.brepStorageId = elementText(xml, "BrepStorageId");
        result.guid = elementText(xml, "Guid");
        result.geometryType = parseUnsigned(elementText(xml, "BrepGeomType"), "BrepGeomType");
        result.fingerprint = elementText(xml, "Fingerprint");
        const auto minimum = elementOpeningTag(xml, "MinPoint");
        const auto maximum = elementOpeningTag(xml, "MaxPoint");
        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const std::string coordinate(1, "XYZ"[axis]);
            result.minimum[axis] = parseDouble(xmlAttribute(minimum, coordinate), "MinPoint");
            result.maximum[axis] = parseDouble(xmlAttribute(maximum, coordinate), "MaxPoint");
        }
        if (result.guid.empty() || result.brepStorageId.empty())
            throw std::runtime_error("shape definition is missing Guid or BrepStorageId");
        result.sourcePath = path;
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool parseShapeGeometry(const std::filesystem::path& path, ShapeGeometry& result, std::string& error)
{
    try
    {
        result = {};
        const auto xml = readText(path);
        if (xml.find("<Polymesh") == std::string::npos)
            throw std::runtime_error("TEZ payload is not a Tekla Polymesh document");
        const auto pointsBlock = elementText(xml, "Points");
        for (const auto& pointXml : elementBlocks(pointsBlock, "Point"))
            result.points.push_back({parseDouble(elementText(pointXml, "X"), "Point.X"),
                                     parseDouble(elementText(pointXml, "Y"), "Point.Y"),
                                     parseDouble(elementText(pointXml, "Z"), "Point.Z")});
        const auto facesBlock = elementText(xml, "Faces");
        for (const auto& faceXml : elementBlocks(facesBlock, "Face"))
        {
            ShapeFace face;
            face.outerLoop = parseIndices(elementText(faceXml, "OuterLoop"));
            const auto innerXml = elementText(faceXml, "InnerLoops");
            for (const auto& loopXml : elementBlocks(innerXml, "Loop"))
                face.innerLoops.push_back(parseIndices(loopXml));
            if (face.outerLoop.size() < 3)
                throw std::runtime_error("shape face has fewer than three outer-loop vertices");
            const auto validate = [&](const std::vector<std::uint32_t>& loop) {
                return std::all_of(loop.begin(), loop.end(), [&](std::uint32_t vertex) { return vertex < result.points.size(); });
            };
            if (!validate(face.outerLoop) ||
                !std::all_of(face.innerLoops.begin(), face.innerLoops.end(), validate))
                throw std::runtime_error("shape face references a vertex outside the point array");
            result.faces.push_back(std::move(face));
        }
        const auto edgesBlock = elementText(xml, "Edges");
        for (const auto& edgeXml : elementBlocks(edgesBlock, "Edge"))
        {
            ShapeEdge edge;
            edge.firstVertex = parseUnsigned(elementText(edgeXml, "FirstVertexIndex"), "Edge.FirstVertexIndex");
            edge.secondVertex = parseUnsigned(elementText(edgeXml, "SecondVertexIndex"), "Edge.SecondVertexIndex");
            edge.rawType = elementText(edgeXml, "EdgeType");
            if (edge.rawType == "VisibleEdge")
                edge.type = ShapeEdgeType::Visible;
            else if (edge.rawType == "InvisibleEdge")
                edge.type = ShapeEdgeType::Invisible;
            if (edge.firstVertex >= result.points.size() || edge.secondVertex >= result.points.size())
                throw std::runtime_error("shape edge references a vertex outside the point array");
            result.edges.push_back(edge);
        }
        result.storageId = path.stem().u8string();
        result.sourcePath = path;
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool parseShapeCatalog(const std::filesystem::path& directory, ShapeCatalog& result, std::string& error)
{
    try
    {
        result = {};
        if (!std::filesystem::is_directory(directory))
            throw std::runtime_error("shape catalog input is not a directory");
        std::error_code filesystemError;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, filesystemError))
        {
            if (filesystemError)
                throw std::runtime_error("cannot enumerate shape catalog: " + filesystemError.message());
            if (!entry.is_regular_file())
                continue;
            auto extension = entry.path().extension().u8string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            std::string itemError;
            if (extension == ".tez")
            {
                ShapeGeometry geometry;
                if (parseShapeGeometry(entry.path(), geometry, itemError))
                    result.geometriesByStorageId[geometry.storageId] = std::move(geometry);
                else
                    result.diagnostics.push_back(entry.path().u8string() + ": " + itemError);
            }
            else if (extension == ".xml")
            {
                const auto text = readText(entry.path());
                if (text.find("<ImportPart") == std::string::npos)
                    continue;
                ShapeDefinition definition;
                if (parseShapeDefinition(entry.path(), definition, itemError))
                    result.definitionsByGuid[definition.guid] = std::move(definition);
                else
                    result.diagnostics.push_back(entry.path().u8string() + ": " + itemError);
            }
        }
        for (const auto& entry : result.definitionsByGuid)
            if (!result.geometriesByStorageId.count(entry.second.brepStorageId))
                result.diagnostics.push_back("shape " + entry.second.name + " references missing geometry " +
                                             entry.second.brepStorageId);
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
