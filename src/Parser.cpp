#include <tekla/db1/Parser.hpp>
#include <tekla/db1/Catalogs.hpp>
#include "Path.hpp"
#include "BinaryIO.hpp"
#include "RelatedContainers.hpp"

#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace tekla::db1
{
namespace
{
constexpr std::array<uint8_t, 4> kSectionMagic{0x66, 0xc0, 0xce, 0xdb};
constexpr std::array<uint8_t, 4> kLegacyTableMagic{0x4f, 0x61, 0xbc, 0x00};

struct Table
{
    std::size_t offset = 0;
    uint32_t payloadSize = 0;
    uint32_t fieldCount = 0;
    std::size_t headerSize = 0;
    std::size_t rowStride = 0;
    std::size_t rowCount = 0;
    std::size_t trailerSize = 0;
    bool valid = false;
};

struct Association
{
    uint32_t id = 0;
    uint32_t type = 0;
    uint32_t source = 0;
    uint32_t target = 0;
    uint32_t table = 0;
    std::string guid;
};

struct LegacyTable
{
    uint32_t ordinal = 0;
    uint32_t payloadSize = 0;
    std::size_t rowsOffset = 0;
    std::size_t rowCount = 0;
};

template<class Map, class Value>
void insertUnique(Map& target, std::uint32_t id, Value&& value, const char* role)
{
    if (!target.emplace(id, std::forward<Value>(value)).second)
        throw std::runtime_error(std::string("duplicate DB1 ") + role + " ID " + std::to_string(id));
}

void validateModel(Model& model)
{
    const auto finite = [](const Vec3& value) {
        return std::all_of(value.begin(), value.end(), [](double x) { return std::isfinite(x); });
    };
    for (const auto& item : model.points)
        if (!finite(item.second.value)) throw std::runtime_error("non-finite DB1 point " + std::to_string(item.first));
    for (const auto& item : model.frames)
        if (!finite(item.second.axis) || !finite(item.second.secondary) || !finite(item.second.normal))
            throw std::runtime_error("non-finite DB1 frame " + std::to_string(item.first));
    for (auto& item : model.parts)
    {
        auto& part = item.second;
        if (!std::isfinite(part.length) || !finite(part.origin))
            throw std::runtime_error("non-finite DB1 placement " + std::to_string(item.first));
        for (const auto& point : part.contour)
            if (!finite(point.value))
                throw std::runtime_error("non-finite DB1 contour " + std::to_string(item.first));
            else if (point.chamferType != 0 && point.chamferType != 40 &&
                     (!std::isfinite(point.chamferX) || !std::isfinite(point.chamferY)))
            {
                part.contourKindUnverified = true;
                model.diagnostics.push_back("non-finite active chamfer retained without geometry for part " + std::to_string(item.first));
            }
    }
    for (const auto& bolt : model.individualBolts)
    {
        if (!model.parts.count(bolt.id) || model.parts.at(bolt.id).internalType != 10)
            throw std::runtime_error("broken DB1 individual bolt reference");
        for (const auto partId : bolt.connectedPartIds)
            if (!model.parts.count(partId) || model.parts.at(partId).internalType != 2)
                throw std::runtime_error("broken DB1 individual bolt connection " + std::to_string(bolt.id));
    }
    for (const auto& entry : model.surfaceTreatments)
    {
        const auto& surface = entry.second;
        if (!finite(surface.origin) || !std::isfinite(surface.storedLength))
            throw std::runtime_error("non-finite surface treatment placement " + std::to_string(surface.id));
        for (const auto& point : surface.contour)
            if (!finite(point.value) || (point.chamferType != 0 && point.chamferType != 40 &&
                (!std::isfinite(point.chamferX) || !std::isfinite(point.chamferY))))
                throw std::runtime_error("non-finite surface treatment contour " + std::to_string(surface.id));
    }
    model.identityTypeCounts.clear();
    for (const auto& identity : model.identities) ++model.identityTypeCounts[identity.second.type];
}

template <typename T>
T read(const uint8_t* data, std::size_t offset)
{
    T result{};
    std::memcpy(&result, data + offset, sizeof(T));
    return result;
}

std::string fixedString(const uint8_t* data, std::size_t offset, std::size_t size)
{
    const auto* begin = reinterpret_cast<const char*>(data + offset);
    const auto* end = std::find(begin, begin + size, '\0');
    return std::string(begin, end);
}

void appendUtf8(std::string& result, uint32_t codepoint)
{
    if (codepoint <= 0x7f)
        result.push_back(static_cast<char>(codepoint));
    else if (codepoint <= 0x7ff)
    {
        result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
    else
    {
        result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    }
}

std::string legacyString(const uint8_t* data, std::size_t offset, std::size_t size)
{
    static constexpr std::array<uint16_t, 32> extension{
        0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
        0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
        0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
        0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};
    std::string result;
    result.reserve(size);
    for (std::size_t index = 0; index < size && data[offset + index]; ++index)
    {
        const auto value = data[offset + index];
        if (value < 0x80)
            result.push_back(static_cast<char>(value));
        else
            appendUtf8(result, value < 0xa0 ? extension[value - 0x80] : value);
    }
    while (!result.empty() && (result.back() == ' ' || result.back() == '\t'))
        result.pop_back();
    return result;
}

using detail::readFile;

std::vector<uint8_t> inflateGzip(const std::filesystem::path& path)
{
    return detail::readPayload(path);
}

std::vector<std::size_t> sectionOffsets(const std::vector<uint8_t>& data, bool variableDatabase = false)
{
    std::vector<std::size_t> result;
    if (data.size() < kSectionMagic.size())
        return result;
    for (std::size_t offset = 0; offset + kSectionMagic.size() <= data.size(); ++offset)
    {
        if (std::equal(kSectionMagic.begin(), kSectionMagic.end(), data.begin() + offset))
        {
            result.push_back(offset);
            // Walk complete allocated records before looking for the next section.
            // A section signature can legally occur in strings, coordinates or GUIDs.
            // Old/unknown sections remain available as opaque bytes via the raw API.
            auto cursor = offset + kSectionMagic.size();
            if (data.size() - offset >= 12)
            {
                const auto payload = read<uint32_t>(data.data(), offset + 4);
                const auto fields = read<uint32_t>(data.data(), offset + 8);
                if (fields <= (data.size() - offset - 12) / 4)
                {
                    cursor = offset + 12 + static_cast<std::size_t>(fields) * 4;
                    const auto stride = static_cast<std::size_t>(payload) + 9;
                    while (cursor < data.size() && (data[cursor] == 4 || data[cursor] == 12 || (variableDatabase && data[cursor] == 1)))
                    {
                        if (stride > data.size() - cursor)
                            break;
                        cursor += stride;
                    }
                }
            }
            offset = cursor - 1;
        }
    }
    return result;
}

std::vector<Table> tables(const std::vector<uint8_t>& data, const std::vector<std::size_t>& offsets, bool variableDatabase = false)
{
    std::vector<Table> result;
    result.reserve(offsets.size());
    for (std::size_t ordinal = 0; ordinal < offsets.size(); ++ordinal)
    {
        Table table;
        table.offset = offsets[ordinal];
        const auto next = ordinal + 1 < offsets.size() ? offsets[ordinal + 1] : data.size();
        if (table.offset + 12 > next)
        {
            result.push_back(table);
            continue;
        }
        table.payloadSize = read<uint32_t>(data.data(), table.offset + 4);
        table.fieldCount = read<uint32_t>(data.data(), table.offset + 8);
        table.headerSize = 12 + static_cast<std::size_t>(table.fieldCount) * 4;
        table.rowStride = static_cast<std::size_t>(table.payloadSize) + 9;
        // Some early tables end with only a zero tag; later ones additionally
        // contain an allocator sentinel. Do not discard those early records.
        for (const auto trailer : {std::size_t(9), std::size_t(5), std::size_t(1)})
        {
            if (variableDatabase && trailer != 1)
                continue;
            if (!variableDatabase && ordinal + 1 == offsets.size() && trailer != 5)
                continue;
            if (ordinal + 1 != offsets.size() && trailer == 5)
                continue;
            if (table.headerSize > next - table.offset || trailer > next - table.offset - table.headerSize)
                continue;
            const auto body = next - table.offset - table.headerSize - trailer;
            if (body % table.rowStride != 0 || data[next - trailer] != 0)
                continue;
            bool tagsValid = true;
            for (std::size_t cursor = table.offset + table.headerSize; cursor < next - trailer; cursor += table.rowStride)
                if (data[cursor] != 4 && data[cursor] != 12 && !(variableDatabase && data[cursor] == 1)) { tagsValid = false; break; }
            if (!tagsValid)
                continue;
            table.trailerSize = trailer;
            table.valid = true;
            table.rowCount = body / table.rowStride;
            break;
        }
        result.push_back(table);
    }
    return result;
}

const uint8_t* row(const std::vector<uint8_t>& data, const std::vector<Table>& all,
                   std::size_t ordinal, std::size_t index)
{
    if (ordinal >= all.size() || !all[ordinal].valid || index >= all[ordinal].rowCount)
        throw std::runtime_error("DB1 table row is outside the validated schema");
    return data.data() + all[ordinal].offset + all[ordinal].headerSize + index * all[ordinal].rowStride;
}

void requireTable(const std::vector<Table>& all, std::size_t ordinal, uint32_t payload)
{
    if (ordinal >= all.size() || !all[ordinal].valid || all[ordinal].payloadSize != payload)
        throw std::runtime_error("unsupported DB1 table schema at ordinal " + std::to_string(ordinal));
}

void requireFields(const std::vector<uint8_t>& data, const std::vector<Table>& all,
                   std::size_t ordinal, std::size_t count, std::initializer_list<std::size_t> references)
{
    const auto& table = all.at(ordinal);
    if (table.fieldCount != count)
        throw std::runtime_error("unsupported DB1 field count at ordinal " + std::to_string(ordinal));
    for (std::size_t field = 0; field < count; ++field)
    {
        const uint32_t expected = std::find(references.begin(), references.end(), field) != references.end() ? 1 : 0;
        if (read<uint32_t>(data.data(), table.offset + 12 + field * 4) != expected)
            throw std::runtime_error("unsupported DB1 field signature at ordinal " + std::to_string(ordinal));
    }
}

std::string guid(const uint8_t* bytes)
{
    std::ostringstream result;
    result << std::hex << std::setfill('0');
    for (int index = 0; index < 16; ++index)
    {
        if (index == 4 || index == 6 || index == 8 || index == 10)
            result << '-';
        result << std::setw(2) << static_cast<unsigned>(bytes[index]);
    }
    return result.str();
}

Vec3 cross(const Vec3& one, const Vec3& two)
{
    return {one[1] * two[2] - one[2] * two[1],
            one[2] * two[0] - one[0] * two[2],
            one[0] * two[1] - one[1] * two[0]};
}

std::filesystem::path findMainDatabase(const std::filesystem::path& directory)
{
    std::vector<std::filesystem::path> candidates;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error))
    {
        if (error || !entry.is_regular_file())
            continue;
        std::string name = detail::pathUtf8(entry.path().filename());
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        auto extension = detail::pathUtf8(entry.path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
        if (extension != ".db1" || name == "xslib.db1")
            continue;
        candidates.push_back(entry.path());
    }
    if (error)
        throw std::runtime_error("cannot enumerate model directory: " + error.message());
    if (candidates.size() > 1)
        throw std::runtime_error("ambiguous model directory: multiple main .db1 files; use parseModelFile with an explicit path");
    return candidates.empty() ? std::filesystem::path{} : candidates.front();
}

void parseProfileDatabase(const std::filesystem::path& path, Model& model)
{
    if (!std::filesystem::exists(path))
    {
        model.diagnostics.emplace_back("profdb.bin is missing; only explicit parametric profiles are available");
        return;
    }
    const auto data = inflateGzip(path);
    if (data.size() < 4)
        throw std::runtime_error("profdb.bin is truncated");
    struct ProfTable { uint32_t count = 0; uint32_t payload = 0; std::size_t begin = 0; };
    std::vector<ProfTable> all;
    std::size_t position = 4;
    while (position < data.size())
    {
        if (position + 8 > data.size())
            throw std::runtime_error("profdb.bin has a truncated table header");
        ProfTable table{read<uint32_t>(data.data(), position), read<uint32_t>(data.data(), position + 4), position + 8};
        position = table.begin + static_cast<std::size_t>(table.count) * (table.payload + 1);
        if (position > data.size())
            throw std::runtime_error("profdb.bin table exceeds the file");
        all.push_back(table);
    }
    if (all.size() != 10 || position != data.size())
        throw std::runtime_error("unsupported profdb.bin schema");
    const auto profRow = [&](std::size_t table, std::size_t index) {
        return data.data() + all.at(table).begin + index * (all.at(table).payload + 1);
    };

    std::unordered_map<std::string, Profile> byName;
    for (std::size_t index = 0; index < all[1].count; ++index)
    {
        const auto* value = profRow(1, index);
        Profile profile;
        profile.name = fixedString(value, 1, 60);
        profile.type = read<uint32_t>(value, 65);
        profile.shapeId = read<uint32_t>(value, 69);
        profile.source = ProfileSource::Catalog;
        byName[profile.name] = std::move(profile);
    }
    std::unordered_map<std::string, uint32_t> profileIds;
    for (std::size_t index = 0; index < all[8].count; ++index)
    {
        const auto* value = profRow(8, index);
        const auto name = fixedString(value, 17, 29);
        const auto id = read<uint32_t>(value, 1);
        profileIds[name] = id;
        const auto found = byName.find(name);
        if (found != byName.end())
            found->second.id = id;
    }
    std::unordered_map<uint32_t, Profile*> byId;
    for (auto& entry : byName)
        if (entry.second.id)
            byId[entry.second.id] = &entry.second;
    for (std::size_t index = 0; index < all[3].count; ++index)
    {
        const auto* value = profRow(3, index);
        const auto profileId = read<uint32_t>(value, 1);
        const auto kind = read<uint32_t>(value, 5);
        const auto parameterId = read<uint32_t>(value, 9);
        const auto reserved = read<uint32_t>(value, 13);
        if (kind != 1 || reserved != 0)
            continue;
        const auto found = byId.find(profileId);
        if (found != byId.end())
            found->second->parameters[parameterId] = read<double>(value, 17);
    }
    std::unordered_map<std::string, std::vector<ProfileContourPoint>> contours;
    for (std::size_t index = 0; index < all[5].count; ++index)
    {
        const auto* value = profRow(5, index);
        const auto name = fixedString(value, 5, 24);
        auto& profile = byName[name];
        if (profile.name.empty())
            profile.name = name;
        const auto profileId = profileIds.find(name);
        if (profileId != profileIds.end())
            profile.id = profileId->second;
        ProfileContourPoint point;
        point.index = read<uint32_t>(value, 29);
        point.value = {static_cast<double>(read<float>(value, 33)),
                       static_cast<double>(read<float>(value, 37))};
        point.chamferType = read<uint32_t>(value, 41);
        point.chamferX = read<float>(value, 45);
        point.chamferY = read<float>(value, 49);
        contours[name].push_back(point);
    }
    for (auto& entry : byName)
    {
        auto found = contours.find(entry.first);
        if (found != contours.end())
        {
            std::sort(found->second.begin(), found->second.end(), [](const auto& one, const auto& two) {
                return one.index < two.index;
            });
            for (const auto& point : found->second)
            {
                const auto contourIndex = point.index == 0 ? 0U : (point.index - 1U) / 1000U;
                if (entry.second.fixedContours.size() <= contourIndex)
                    entry.second.fixedContours.resize(contourIndex + 1U);
                entry.second.fixedContours[contourIndex].push_back(point);
            }
        }
        if (!entry.second.name.empty())
            model.profiles[entry.second.name] = std::move(entry.second);
    }
}

std::unordered_map<uint32_t, LegacyTable> legacyTables(const std::vector<uint8_t>& data)
{
    std::unordered_map<uint32_t, LegacyTable> result;
    for (std::size_t offset = 0; offset + 16 <= data.size(); ++offset)
    {
        if (!std::equal(kLegacyTableMagic.begin(), kLegacyTableMagic.end(), data.begin() + offset))
            continue;
        const auto ordinal = read<uint32_t>(data.data(), offset + 4);
        const auto count = read<uint32_t>(data.data(), offset + 8);
        const auto payload = read<uint32_t>(data.data(), offset + 12);
        if (ordinal > 1000 || payload > 10000)
            continue;
        const auto stride = static_cast<uint64_t>(payload) + 1U;
        const auto end = static_cast<uint64_t>(offset) + 16U + static_cast<uint64_t>(count) * stride;
        if (end > data.size())
            continue;
        if (end != data.size() && end + 4 <= data.size() &&
            !std::equal(kLegacyTableMagic.begin(), kLegacyTableMagic.end(), data.begin() + static_cast<std::size_t>(end)))
        {
            // Some legacy tables are followed by allocator metadata. Their header is
            // still accepted only if every row has a supported allocation tag.
            bool tagsValid = true;
            for (uint32_t index = 0; index < count; ++index)
            {
                const auto tag = data[offset + 16 + static_cast<std::size_t>(index) * stride];
                if (tag != 4 && tag != 12)
                {
                    tagsValid = false;
                    break;
                }
            }
            if (!tagsValid)
                continue;
        }
        result[ordinal] = {ordinal, payload, offset + 16, count};
        offset += 15;
    }
    return result;
}

const LegacyTable& requireLegacyTable(const std::unordered_map<uint32_t, LegacyTable>& all,
                                      uint32_t ordinal, uint32_t payload)
{
    const auto found = all.find(ordinal);
    if (found == all.end() || found->second.payloadSize != payload)
        throw std::runtime_error("unsupported legacy DB1 table schema at ordinal " + std::to_string(ordinal));
    return found->second;
}

const uint8_t* legacyRow(const std::vector<uint8_t>& data, const LegacyTable& table, std::size_t index)
{
    return data.data() + table.rowsOffset + index * (static_cast<std::size_t>(table.payloadSize) + 1U);
}

bool startsWithInsensitive(const std::string& value, const std::string& prefix)
{
    return value.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), value.begin(),
        [](unsigned char one, unsigned char two) { return std::toupper(one) == std::toupper(two); });
}

void parseLegacyDatabase(const std::vector<uint8_t>& data, Model& model)
{
    const auto all = legacyTables(data);
    const auto& identityTable = requireLegacyTable(all, 1, 67);
    const auto& placementTable = requireLegacyTable(all, 3, 40);
    const auto& pointTable = requireLegacyTable(all, 13, 32);
    const auto& associationTableOne = requireLegacyTable(all, 16, 60);
    const auto& weldTable = requireLegacyTable(all, 20, 24);
    const auto& associationTableTwo = requireLegacyTable(all, 32, 60);
    const auto& contourTable = requireLegacyTable(all, 36, 332);
    const auto& propertyLinkTable = requireLegacyTable(all, 38, 24);
    const auto& definitionTable = requireLegacyTable(all, 44, 372);
    const auto& frameTable = requireLegacyTable(all, 47, 52);
    const auto& propertyTable = requireLegacyTable(all, 48, 44);
    const auto& partTable = requireLegacyTable(all, 10, 56);
    const auto& componentTable = requireLegacyTable(all, 6, 104);
    const auto& assemblyTable = requireLegacyTable(all, 2, 88);
    const auto& partGroupTable = requireLegacyTable(all, 94, 60);
    const auto& weldDefinitionTable = requireLegacyTable(all, 98, 108);

    struct Placement { uint32_t frameId = 0; Vec3 origin{}; double length = 0.0; };
    std::unordered_map<uint32_t, Placement> placements;
    for (std::size_t index = 0; index < placementTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, placementTable, index);
        Placement placement;
        placement.frameId = read<uint32_t>(value, 5);
        for (int axis = 0; axis < 3; ++axis)
            placement.origin[axis] = read<double>(value, 9 + axis * 8);
        placement.length = read<double>(value, 33);
        placements[read<uint32_t>(value, 1)] = placement;
    }
    for (std::size_t index = 0; index < pointTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, pointTable, index);
        Point point;
        point.id = read<uint32_t>(value, 1);
        point.type = read<uint32_t>(value, 5);
        for (int axis = 0; axis < 3; ++axis)
            point.value[axis] = read<double>(value, 9 + axis * 8);
        insertUnique(model.points, point.id, point, "point");
    }
    for (std::size_t index = 0; index < frameTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, frameTable, index);
        Frame frame;
        for (int axis = 0; axis < 3; ++axis)
        {
            frame.axis[axis] = read<double>(value, 1 + axis * 8);
            frame.secondary[axis] = read<double>(value, 25 + axis * 8);
        }
        frame.normal = cross(frame.axis, frame.secondary);
        frame.id = read<uint32_t>(value, 49);
        insertUnique(model.frames, frame.id, frame, "frame");
    }
    for (std::size_t index = 0; index < identityTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, identityTable, index);
        Identity identity;
        identity.rowTag = value[0];
        identity.id = read<uint32_t>(value, 1);
        identity.ownerId = read<uint32_t>(value, 21);
        identity.contextId = read<uint32_t>(value, 9);
        identity.flags = read<uint32_t>(value, 25);
        identity.guid = legacyString(value, 29, 39);
        model.identities[identity.id] = std::move(identity);
    }

    std::vector<Association> associations;
    associations.reserve(associationTableOne.rowCount + associationTableTwo.rowCount);
    std::unordered_map<uint32_t, std::vector<std::size_t>> bySource;
    const auto readAssociations = [&](const LegacyTable& table) {
        for (std::size_t index = 0; index < table.rowCount; ++index)
        {
            const auto* value = legacyRow(data, table, index);
            Association association{read<uint32_t>(value, 1), read<uint32_t>(value, 5),
                                    read<uint32_t>(value, 9), read<uint32_t>(value, 13),
                                    table.ordinal, legacyString(value, 17, 37)};
            const auto associationIndex = associations.size();
            associations.push_back(std::move(association));
            bySource[associations.back().source].push_back(associationIndex);
        }
    };
    readAssociations(associationTableOne);
    readAssociations(associationTableTwo);

    std::unordered_map<uint32_t, Property> propertyValues;
    for (std::size_t index = 0; index < propertyTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, propertyTable, index);
        Property property;
        property.name = legacyString(value, 17, 28);
        property.group = "Tekla legacy attribute";
        const auto type = read<uint32_t>(value, 5);
        const auto number = read<double>(value, 9);
        if (type == 0)
        {
            property.kind = Property::Kind::Integer;
            property.integerValue = static_cast<int32_t>(std::llround(number));
        }
        else
        {
            property.kind = Property::Kind::Double;
            property.doubleValue = number;
        }
        propertyValues[read<uint32_t>(value, 1)] = std::move(property);
    }
    for (std::size_t index = 0; index < propertyLinkTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, propertyLinkTable, index);
        const auto property = propertyValues.find(read<uint32_t>(value, 5));
        if (property != propertyValues.end())
            model.properties[read<uint32_t>(value, 9)].push_back(property->second);
    }
    for (std::size_t index = 0; index < partGroupTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, partGroupTable, index);
        const auto group = legacyString(value, 17, 44);
        if (group.empty())
            continue;
        Property property;
        property.name = "ModelGroup";
        property.kind = Property::Kind::String;
        property.stringValue = group;
        property.group = "Tekla legacy attribute";
        model.properties[read<uint32_t>(value, 1)].push_back(std::move(property));
    }

    for (std::size_t index = 0; index < definitionTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, definitionTable, index);
        PartDefinition definition;
        definition.id = read<uint32_t>(value, 1);
        definition.subtype = read<uint32_t>(value, 9);
        definition.classNumber = legacyString(value, 81, 22);
        definition.name = legacyString(value, 103, 22);
        definition.profile = legacyString(value, 125, 84);
        definition.secondaryName = legacyString(value, 209, 62);
        definition.material = legacyString(value, 271, 62);
        model.definitions[definition.id] = std::move(definition);
    }

    struct ContourChunk { uint32_t sequence = 0; std::vector<ContourPoint> points; };
    std::unordered_map<uint32_t, std::vector<ContourChunk>> contourChunks;
    for (std::size_t index = 0; index < contourTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, contourTable, index);
        ContourChunk chunk;
        chunk.sequence = read<uint32_t>(value, 5);
        for (int pointIndex = 0; pointIndex < 10; ++pointIndex)
        {
            const auto type = read<uint32_t>(value, 213 + pointIndex * 4);
            if (type == 0x7fffffff)
                break;
            ContourPoint point;
            point.value = {read<float>(value, 13 + pointIndex * 4),
                           read<float>(value, 53 + pointIndex * 4),
                           read<float>(value, 93 + pointIndex * 4)};
            point.chamferX = read<float>(value, 133 + pointIndex * 4);
            point.chamferY = read<float>(value, 173 + pointIndex * 4);
            point.chamferType = type;
            point.chamferDz1 = read<float>(value, 253 + pointIndex * 4);
            point.chamferDz2 = read<float>(value, 293 + pointIndex * 4);
            chunk.points.push_back(point);
        }
        contourChunks[read<uint32_t>(value, 1)].push_back(std::move(chunk));
    }
    std::unordered_map<uint32_t, std::vector<ContourPoint>> contours;
    for (auto& entry : contourChunks)
    {
        std::sort(entry.second.begin(), entry.second.end(), [](const auto& one, const auto& two) {
            return one.sequence < two.sequence;
        });
        auto& destination = contours[entry.first];
        for (const auto& chunk : entry.second)
            destination.insert(destination.end(), chunk.points.begin(), chunk.points.end());
    }

    std::unordered_set<uint32_t> subtractiveIds;
    for (const auto& association : associations)
        if (association.type == 11)
            subtractiveIds.insert(association.target);

    std::unordered_set<uint32_t> boltIds;
    for (std::size_t index = 0; index < partTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, partTable, index);
        Part part;
        part.id = read<uint32_t>(value, 1);
        part.definitionId = read<uint32_t>(value, 5);
        part.startPointId = read<uint32_t>(value, 9);
        part.endPointId = read<uint32_t>(value, 13);
        part.geometryReferenceId = read<uint32_t>(value, 17);
        part.orientationId = read<uint32_t>(value, 21);
        const auto definition = model.definitions.find(part.definitionId);
        const auto start = model.points.find(part.startPointId);
        const auto end = model.points.find(part.endPointId);
        const auto frame = model.frames.find(part.orientationId);
        if (definition == model.definitions.end() || start == model.points.end() ||
            end == model.points.end() || frame == model.frames.end())
            throw std::runtime_error("broken legacy DB1 part join for object " + std::to_string(part.id));
        const auto identity = model.identities.find(part.id);
        if (identity != model.identities.end())
        {
            part.ownerId = identity->second.ownerId;
            part.contextId = identity->second.contextId;
            part.guid = identity->second.guid;
        }
        part.properties = model.properties[part.id];
        part.name = definition->second.name;
        part.profile = definition->second.profile;
        part.material = definition->second.material;
        part.classNumber = definition->second.classNumber;
        part.start = start->second.value;
        part.end = end->second.value;
        part.axis = frame->second.axis;
        part.secondary = frame->second.secondary;
        part.normal = frame->second.normal;
        for (int axis = 0; axis < 3; ++axis)
            part.origin[axis] = read<double>(value, 25 + axis * 8);
        part.length = read<double>(value, 49);
        const auto contour = contours.find(part.geometryReferenceId);
        if (contour != contours.end())
            part.contour = contour->second;
        part.contourIsPath = definition->second.subtype == 4;
        const bool bolt = startsWithInsensitive(part.name, "SCREW") || startsWithInsensitive(part.profile, "MM");
        if (bolt)
        {
            boltIds.insert(part.id);
            BoltDefinition boltDefinition;
            boltDefinition.id = part.definitionId;
            boltDefinition.count = 1;
            boltDefinition.name = part.name;
            boltDefinition.standard = part.profile;
            boltDefinition.classNumber = part.classNumber;
            boltDefinition.length = static_cast<float>(part.length);
            std::smatch dimensions;
            if (std::regex_search(part.profile, dimensions, std::regex(R"(^MM([0-9.]+)\*([0-9.]+))", std::regex::icase)))
                boltDefinition.diameter = static_cast<float>(std::stod(dimensions[1]));
            if (boltDefinition.diameter <= 0)
                boltDefinition.diameter = 16.0f;
            model.boltDefinitions[boltDefinition.id] = boltDefinition;
            BoltGroup group;
            group.id = part.id;
            group.definitionId = part.definitionId;
            group.guid = part.guid;
            group.first = part.start;
            group.second = part.end;
            group.origin = {part.origin[0] + part.axis[0] * part.length * 0.5,
                            part.origin[1] + part.axis[1] * part.length * 0.5,
                            part.origin[2] + part.axis[2] * part.length * 0.5};
            group.axis = part.secondary;
            group.secondary = part.normal;
            group.normal = part.axis;
            group.placementLength = part.length;
            group.positions.push_back({0.0, 0.0, 0.0});
            group.properties = model.properties[part.id];
            for (const auto associationIndex : bySource[part.id])
            {
                const auto& association = associations[associationIndex];
                if (association.type == 10)
                    group.connectedPartIds.push_back(association.target);
            }
            std::sort(group.connectedPartIds.begin(), group.connectedPartIds.end());
            group.connectedPartIds.erase(std::unique(group.connectedPartIds.begin(), group.connectedPartIds.end()), group.connectedPartIds.end());
            model.boltGroups.push_back(std::move(group));
            continue;
        }
        if (subtractiveIds.count(part.id))
        {
            part.internalType = 11;
            model.operativePartIds.push_back(part.id);
        }
        else
        {
            part.internalType = 2;
            model.actualPartIds.push_back(part.id);
        }
        model.identities[part.id].type = part.internalType;
        insertUnique(model.parts, part.id, std::move(part), "part");
    }

    for (const auto& association : associations)
    {
        if (association.type == 11 && model.parts.count(association.target) && model.parts.count(association.source))
            model.booleans.push_back({association.target, association.source, false});
        if ((association.type == 9 || association.type == 12) && model.parts.count(association.source))
        {
            const auto placement = placements.find(association.target);
            if (placement == placements.end() || !model.frames.count(placement->second.frameId))
                throw std::runtime_error("broken legacy DB1 plane operation join");
            const auto& frame = model.frames.at(placement->second.frameId);
            const auto identity = model.identities.find(association.target);
            PlaneOperation operation{association.target, association.source, association.type,
                                     identity == model.identities.end() ? association.guid : identity->second.guid,
                                     placement->second.origin, frame.axis, frame.secondary, frame.normal,
                                     placement->second.length, {}};
            (association.type == 9 ? model.fittings : model.cutPlanes).push_back(std::move(operation));
            model.identities[association.target].type = association.type;
        }
    }

    std::unordered_map<uint32_t, std::pair<uint32_t, uint32_t>> weldReferences;
    for (std::size_t index = 0; index < weldTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, weldTable, index);
        weldReferences[read<uint32_t>(value, 1)] = {read<uint32_t>(value, 5), read<uint32_t>(value, 9)};
    }
    for (std::size_t index = 0; index < weldDefinitionTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, weldDefinitionTable, index);
        if (read<uint32_t>(value, 5) != 13)
            continue;
        WeldDefinition definition;
        definition.id = read<uint32_t>(value, 1);
        definition.size = 3.0f;
        definition.type = read<uint32_t>(value, 25);
        model.weldDefinitions[definition.id] = definition;
    }
    std::set<uint32_t> weldIds;
    for (const auto& association : associations)
        if (association.type == 13)
            weldIds.insert(association.source);
    for (const auto id : weldIds)
    {
        const auto placement = placements.find(id);
        const auto reference = weldReferences.find(id);
        if (placement == placements.end() || reference == weldReferences.end() ||
            !model.frames.count(placement->second.frameId))
            throw std::runtime_error("broken legacy DB1 weld join for object " + std::to_string(id));
        Weld weld;
        weld.id = id;
        weld.definitionId = reference->second.first;
        const auto identity = model.identities.find(id);
        if (identity != model.identities.end())
            weld.guid = identity->second.guid;
        weld.origin = placement->second.origin;
        weld.length = placement->second.length;
        const auto& frame = model.frames.at(placement->second.frameId);
        weld.axis = frame.axis;
        weld.secondary = frame.secondary;
        weld.normal = frame.normal;
        for (const auto associationIndex : bySource[id])
        {
            const auto& association = associations[associationIndex];
            if (association.type == 13 && model.parts.count(association.target))
                weld.connectedPartIds.push_back(association.target);
        }
        std::sort(weld.connectedPartIds.begin(), weld.connectedPartIds.end());
        weld.connectedPartIds.erase(std::unique(weld.connectedPartIds.begin(), weld.connectedPartIds.end()), weld.connectedPartIds.end());
        if (weld.connectedPartIds.size() != 2)
            throw std::runtime_error("legacy DB1 weld does not have exactly two connected parts");
        if (!model.weldDefinitions.count(weld.definitionId))
            model.weldDefinitions[weld.definitionId] = {weld.definitionId, 3.0f, 0};
        model.identities[id].type = 13;
        model.welds.push_back(std::move(weld));
    }

    std::unordered_map<uint32_t, Assembly> assemblies;
    for (std::size_t index = 0; index < assemblyTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, assemblyTable, index);
        if (read<uint32_t>(value, 5) != 15)
            continue;
        Assembly assembly;
        assembly.id = read<uint32_t>(value, 1);
        const auto identity = model.identities.find(assembly.id);
        if (identity != model.identities.end())
            assembly.guid = identity->second.guid;
        assembly.name = legacyString(value, 21, 68);
        assembly.properties = model.properties[assembly.id];
        const auto primary = read<uint32_t>(value, 13);
        if (model.parts.count(primary) && model.parts.at(primary).internalType == 2)
            assembly.memberIds.push_back(primary);
        model.identities[assembly.id].type = 15;
        assemblies[assembly.id] = std::move(assembly);
    }
    for (const auto partId : model.actualPartIds)
    {
        const auto identity = model.identities.find(partId);
        if (identity == model.identities.end())
            continue;
        const auto assemblyId = identity->second.flags;
        if (assemblies.count(assemblyId))
            assemblies[assemblyId].memberIds.push_back(partId);
    }
    for (auto& entry : assemblies)
    {
        auto& members = entry.second.memberIds;
        std::sort(members.begin(), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        model.assemblies.push_back(std::move(entry.second));
    }
    std::sort(model.assemblies.begin(), model.assemblies.end(), [](const auto& one, const auto& two) {
        return one.id < two.id;
    });

    for (std::size_t index = 0; index < componentTable.rowCount; ++index)
    {
        const auto* value = legacyRow(data, componentTable, index);
        if (read<uint32_t>(value, 5) != 3)
            continue;
        Component component;
        component.id = read<uint32_t>(value, 1);
        component.primaryObjectId = read<uint32_t>(value, 13);
        component.referenceObjectId = read<uint32_t>(value, 17);
        component.number = read<uint32_t>(value, 73);
        component.name = "Connection " + std::to_string(component.number);
        const auto identity = model.identities.find(component.id);
        if (identity != model.identities.end())
        {
            component.guid = identity->second.guid;
            component.ownerId = identity->second.ownerId == component.id ? 0 : identity->second.ownerId;
        }
        component.properties = model.properties[component.id];
        for (const auto& child : model.identities)
            if (child.first != component.id && child.second.ownerId == component.id)
                component.childIds.push_back(child.first);
        std::sort(component.childIds.begin(), component.childIds.end());
        component.childIds.erase(std::unique(component.childIds.begin(), component.childIds.end()), component.childIds.end());
        model.identities[component.id].type = 3;
        model.components.push_back(std::move(component));
    }
    std::sort(model.components.begin(), model.components.end(), [](const auto& one, const auto& two) {
        return one.id < two.id;
    });

    std::sort(model.actualPartIds.begin(), model.actualPartIds.end());
    std::sort(model.operativePartIds.begin(), model.operativePartIds.end());
    if (model.actualPartIds.size() + model.boltGroups.size() != partTable.rowCount - model.operativePartIds.size())
        throw std::runtime_error("legacy DB1 object classification is incomplete");
    model.diagnostics.emplace_back("legacy Xsteel 7.x database decoded with table metadata; external profile catalogs are optional for explicit profiles");
}

void parseDatabase(const std::vector<uint8_t>& data, Model& model, bool componentLibrary)
{
    if (data.size() < 64 || std::memcmp(data.data(), "Xsteel", 6) != 0)
        throw std::runtime_error("the main DB1 is not an Xsteel database");
    const std::string header(reinterpret_cast<const char*>(data.data()), (std::min)(data.size(), std::size_t(160)));
    const auto versionAt = header.find_first_of("0123456789", 6);
    if (versionAt != std::string::npos)
    {
        auto versionEnd = versionAt;
        while (versionEnd < header.size() &&
               ((header[versionEnd] >= '0' && header[versionEnd] <= '9') || header[versionEnd] == '.'))
            ++versionEnd;
        model.storageVersion = header.substr(versionAt, versionEnd - versionAt);
        auto guidAt = versionEnd;
        while (guidAt < header.size() && header[guidAt] == ' ')
            ++guidAt;
        if (guidAt + 36 <= header.size() && std::count(header.begin() + guidAt, header.begin() + guidAt + 36, '-') == 4)
            model.databaseGuid = header.substr(guidAt, 36);
    }
    const auto offsets = sectionOffsets(data);
    if (offsets.empty())
    {
        if (model.storageVersion != "7.30")
            throw std::runtime_error("unsupported legacy DB1 version " + model.storageVersion + "; use parseRawDatabase");
        parseLegacyDatabase(data, model);
        return;
    }
    const auto all = tables(data, offsets);
    const bool schema782 = !componentLibrary && model.storageVersion == "7.82" && offsets.size() == 228;
    const bool library782 = componentLibrary && model.storageVersion == "7.82" && offsets.size() == 198;
    const bool schema844 = !componentLibrary && model.storageVersion == "8.44" && offsets.size() == 286;
    const bool schema895 = !componentLibrary && model.storageVersion == "8.95" && offsets.size() == 326;
    const bool modernVersion = model.storageVersion == "9.52" || model.storageVersion == "9.60" ||
                               model.storageVersion == "9.65" || model.storageVersion == "9.66";
    const bool schema952OrNewer = !componentLibrary && modernVersion && offsets.size() >= 356;
    const bool library844 = componentLibrary && model.storageVersion == "8.44" && offsets.size() == 254;
    const bool library895 = componentLibrary && model.storageVersion == "8.95" && offsets.size() == 290;
    const bool library952OrNewer = componentLibrary && modernVersion && offsets.size() >= 319;
    if (!schema782 && !schema844 && !schema895 && !schema952OrNewer &&
        !library782 && !library844 && !library895 && !library952OrNewer)
        throw std::runtime_error("unsupported DB1 semantic schema " + model.storageVersion + " with " +
                                 std::to_string(offsets.size()) + " sections; use parseRawDatabase for lossless access");

    const std::size_t noTable = (std::numeric_limits<std::size_t>::max)();
    const bool older782 = schema782 || library782;
    const bool older844 = schema844 || library844;
    const bool older895 = schema895 || library895;
    const bool olderSchema = older782 || older844 || older895;
    const bool library = componentLibrary;
    const bool componentVariables = library && !older844;
    const bool verifiedPartOwnership = !older782 && !older844;
    const std::size_t identityClassOrdinal = library ? 279 : 315;
    const std::size_t partAuxiliaryOrdinal = library ? 215 : 245;
    const std::size_t surfaceOrdinal = library ? 117 : 145;
    const std::size_t surfaceDefinitionOrdinal = library ? 118 : 146;
    const std::size_t pointOrdinal = library ? 40 : 61;
    const std::size_t placementOrdinal = library ? 43 : 64;
    const std::size_t frameOrdinal = library ? 44 : 65;
    const std::size_t profileStringOrdinal = library ? 53 : 75;
    const std::size_t numericPropertyOrdinal = library ? 95 : 122;
    const std::size_t componentOrdinal = library ? 126 : 154;
    const std::size_t propertyLinkOneOrdinal = library ? 132 : 160;
    const std::size_t propertyLinkTwoOrdinal = library ? 133 : 161;
    const std::size_t weldOrdinal = library ? 160 : 190;
    const std::size_t associationOneOrdinal = library ? 161 : 191;
    const std::size_t associationTwoOrdinal = library ? 162 : 192;
    const std::size_t identityOrdinal = (older782 || older844) ? (library ? 179 : 209) :
                                                (older895 ? (library ? 260 : 293) : (library ? 318 : 355));
    const std::size_t partDefinitionOrdinal = older782 ? (library ? 177 : 207) : older844 ? (library ? 215 : 245) :
                                                      (older895 ? (library ? 264 : 299) : (library ? 305 : 341));
    const std::size_t contourBlockOrdinal = olderSchema ? (library ? 94 : 121) : (library ? 292 : 328);
    const std::size_t stringPropertyOrdinal = olderSchema ? (library ? 90 : 116) : (library ? 304 : 340);
    const std::size_t partOrdinal = older782 ? (library ? 163 : 193) : older844 ? (library ? 241 : 273) : (library ? 242 : 274);
    const std::size_t contourLinkOrdinal = older782 ? noTable : older844 ? (library ? 237 : 269) : (library ? 238 : 270);
    const std::size_t boltDefinitionOrdinal = (older782 || older844) ? noTable :
                                                       (older895 ? (library ? 223 : 253) : (library ? 314 : 351));
    const std::size_t boltLayerOrdinal = olderSchema ? noTable : (library ? 296 : 332);
    const std::size_t boltGroupOrdinal = (older782 || older844) ? noTable : (library ? 274 : 310);
    const std::size_t weldDefinitionOrdinal = older782 ? (library ? 196 : 226) : older844 ? (library ? 197 : 227) : (library ? 198 : 228);
    const std::size_t relationOrdinal = (older782 || older844) ? noTable : (library ? 261 : 294);
    const std::size_t assemblyOrdinal = (older782 || older844) ? (library ? 151 : 181) : (library ? 265 : 300);

    std::vector<std::pair<std::size_t, uint32_t>> required = {
        {pointOrdinal, 32}, {placementOrdinal, 40}, {frameOrdinal, 52}, {profileStringOrdinal, 45},
        {numericPropertyOrdinal, 44}, {componentOrdinal, 104}, {propertyLinkOneOrdinal, 24},
        {propertyLinkTwoOrdinal, 24}, {weldOrdinal, 24}, {associationOneOrdinal, 60},
        {associationTwoOrdinal, 60}, {identityOrdinal, (older782 || older844) ? 63U : (older895 ? 55U : 72U)},
        {partDefinitionOrdinal, older782 ? 380U : older844 ? 322U : (older895 ? 332U : 372U)},
        {contourBlockOrdinal, olderSchema ? 332U : 456U},
        {stringPropertyOrdinal, olderSchema ? 116U : 120U}, {partOrdinal, older782 ? 56U : 64U},
        {weldDefinitionOrdinal, older782 ? 60U : older844 ? 100U : 104U},
        {assemblyOrdinal, (older782 || older844) ? 88U : 92U}};
    if (contourLinkOrdinal != noTable) required.emplace_back(contourLinkOrdinal, 24U);
    if (boltDefinitionOrdinal != noTable) required.emplace_back(boltDefinitionOrdinal, older895 ? 308U : 316U);
    if (boltLayerOrdinal != noTable) required.emplace_back(boltLayerOrdinal, 48U);
    if (boltGroupOrdinal != noTable) required.emplace_back(boltGroupOrdinal, 24U);
    if (relationOrdinal != noTable) required.emplace_back(relationOrdinal, 20U);
    if (older895) required.emplace_back(identityClassOrdinal, 28U);
    if (older782)
    {
        required.emplace_back(surfaceOrdinal, 78U);
        required.emplace_back(surfaceDefinitionOrdinal, 292U);
    }
    if (verifiedPartOwnership) required.emplace_back(partAuxiliaryOrdinal, 52U);
    if (componentVariables)
    {
        required.emplace_back(68, 76U);
        required.emplace_back(147, 64U); required.emplace_back(156, 97U);
        if (!older782) required.emplace_back(226, 36U);
    }
    if (library782)
    {
        required.emplace_back(125, 32U);
    }
    for (const auto& spec : required)
        requireTable(all, spec.first, spec.second);
    if (older895) requireFields(data, all, identityClassOrdinal, 8, {0,1,6,7});
    if (verifiedPartOwnership) requireFields(data, all, partAuxiliaryOrdinal, 14, {0,1});
    if (componentVariables && !older782)
    {
        requireFields(data, all, 147, 14, {0,1,2,3,12,13});
        requireFields(data, all, 156, 6, {0,1,2,4});
    }
    if (older782)
    {
        requireFields(data, all, surfaceOrdinal, 12, {0});
        requireFields(data, all, surfaceDefinitionOrdinal, 29, {0});
        // Both independent 7.82 models and their libraries share exact signatures.
        const std::pair<std::size_t, std::size_t> fields[] = {
            {pointOrdinal,6}, {placementOrdinal,7}, {frameOrdinal,8}, {profileStringOrdinal,6},
            {numericPropertyOrdinal,6}, {componentOrdinal,22}, {propertyLinkOneOrdinal,7},
            {propertyLinkTwoOrdinal,7}, {weldOrdinal,7}, {associationOneOrdinal,7},
            {associationTwoOrdinal,7}, {identityOrdinal,8}, {partDefinitionOrdinal,31},
            {contourBlockOrdinal,84}, {stringPropertyOrdinal,6}, {partOrdinal,11},
            {weldDefinitionOrdinal,16}, {assemblyOrdinal,8}};
        for (const auto& field : fields) requireFields(data, all, field.first, field.second, {0});
        if (library782)
        {
            requireFields(data, all, 68, 6, {0}); requireFields(data, all, 125, 9, {0});
            requireFields(data, all, 147, 14, {0}); requireFields(data, all, 156, 6, {0});
        }
        model.diagnostics.emplace_back("Xsteel 7.82 partial semantics: individual bolts retain stored placement/profile parameters; non-plate contours and unnamed tables need further validation");
        if (library782) model.diagnostics.emplace_back("Xsteel 7.82 custom definitions preserve anonymous and repeated names; classification, lifecycle and formula evaluation remain unverified");
    }
    else if (!older844)
    {
        // Exact field signatures from the pinned 8.95, 9.52 and 9.60 corpus.
        // 9.65/9.66 retain these roles; appended tables do not change ordinals.
        requireFields(data, all, pointOrdinal, 6, {0,1});
        requireFields(data, all, placementOrdinal, 7, {0,1,2});
        requireFields(data, all, frameOrdinal, 8, {0,7});
        requireFields(data, all, profileStringOrdinal, 6, {0,1,2});
        requireFields(data, all, numericPropertyOrdinal, 6, {0,1,5});
        requireFields(data, all, componentOrdinal, 22, {0,1,4,5,6,21});
        for (auto ordinal : {propertyLinkOneOrdinal, propertyLinkTwoOrdinal})
            requireFields(data, all, ordinal, 7, {0,1,2,3,4});
        for (auto ordinal : {weldOrdinal, boltGroupOrdinal})
            requireFields(data, all, ordinal, 7, {0,1,2,3,4,5,6});
        for (auto ordinal : {associationOneOrdinal, associationTwoOrdinal})
            requireFields(data, all, ordinal, 7, {0,1,3,4});
        if (older895) requireFields(data, all, identityOrdinal, 6, {0,1,2,3,4});
        else requireFields(data, all, identityOrdinal, 13, {0,1,2,3,11,12});
        requireFields(data, all, partDefinitionOrdinal, 19, {0,1,12});
        requireFields(data, all, contourBlockOrdinal, 84, {0,1});
        if (older895) requireFields(data, all, stringPropertyOrdinal, 6, {0,1,5});
        else requireFields(data, all, stringPropertyOrdinal, 7, {0,1,5,6});
        requireFields(data, all, partOrdinal, 12, {0,1,2,3,4,5,6,7});
        requireFields(data, all, contourLinkOrdinal, 7, {0,1,2,5,6});
        requireFields(data, all, weldDefinitionOrdinal, 18, {0,1,2});
        requireFields(data, all, assemblyOrdinal, 9, {0,1,4,5});
        if (older895) requireFields(data, all, boltDefinitionOrdinal, 28, {0,1});
        else requireFields(data, all, boltDefinitionOrdinal, 30, {0,1,25,26,27,28,29});
        if (!olderSchema) requireFields(data, all, boltLayerOrdinal, 9, {0,1,3});
        requireFields(data, all, relationOrdinal, 6, {0,1,2,4,5});
    }
    else
    {
        for (const auto& spec : required)
            for (std::size_t field = 0; field < all[spec.first].fieldCount; ++field)
                if (read<uint32_t>(data.data(), all[spec.first].offset + 12 + field * 4) > 1)
                    throw std::runtime_error("unsupported DB1 field descriptor at ordinal " + std::to_string(spec.first));
        model.diagnostics.emplace_back("8.44 semantic tables have structural checks only; complete field signatures need independent corpus validation");
    }
    if (older844)
        model.diagnostics.emplace_back(
            "Xsteel 8.44 tables without proven semantic roles remain available through parseRawDatabase");
    else if (older895)
        model.diagnostics.emplace_back(
            "Xsteel 8.95 bolt-layer records are preserved by parseRawDatabase but their field semantics remain unnamed");

    struct StringChunk { uint32_t next = 0; std::string text; };
    std::unordered_map<uint32_t, StringChunk> stringChunks;
    for (std::size_t index = 0; index < all[profileStringOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, profileStringOrdinal, index);
        insertUnique(stringChunks, read<uint32_t>(value, 1), StringChunk{read<uint32_t>(value, 5), fixedString(value, 17, 28)}, "string chunk");
    }
    std::unordered_map<uint32_t, std::string> strings;
    const auto resolveString = [&](uint32_t first, bool strict = false) {
        std::string result;
        std::unordered_set<uint32_t> visited;
        auto current = first;
        while (current)
        {
            if (!visited.insert(current).second)
            {
                if (older782 || strict) throw std::runtime_error("cycle in DB1 chained string at " + std::to_string(current));
                model.diagnostics.push_back("cycle in DB1 chained string at " + std::to_string(current));
                break;
            }
            const auto found = stringChunks.find(current);
            if (found == stringChunks.end())
            {
                if (older782 || strict) throw std::runtime_error("missing DB1 chained string chunk " + std::to_string(current));
                model.diagnostics.push_back("missing DB1 chained string chunk " + std::to_string(current));
                break;
            }
            result += found->second.text;
            current = found->second.next;
        }
        return result;
    };
    for (const auto& entry : stringChunks)
        strings[entry.first] = resolveString(entry.first);
    for (std::size_t index = 0; index < all[pointOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, pointOrdinal, index);
        Point point;
        point.id = read<uint32_t>(value, 1);
        point.type = read<uint32_t>(value, 5);
        for (int axis = 0; axis < 3; ++axis)
            point.value[axis] = read<double>(value, 9 + axis * 8);
        insertUnique(model.points, point.id, point, "point");
    }
    for (std::size_t index = 0; index < all[frameOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, frameOrdinal, index);
        Frame frame;
        for (int axis = 0; axis < 3; ++axis)
        {
            frame.axis[axis] = read<double>(value, 1 + axis * 8);
            frame.secondary[axis] = read<double>(value, 25 + axis * 8);
        }
        frame.normal = cross(frame.axis, frame.secondary);
        frame.id = read<uint32_t>(value, 49);
        insertUnique(model.frames, frame.id, frame, "frame");
    }
    if (older895)
        for (std::size_t index = 0; index < all[identityClassOrdinal].rowCount; ++index)
        {
            const auto* value = row(data, all, identityClassOrdinal, index);
            IdentityClass identityClass;
            identityClass.id = read<uint32_t>(value, 1);
            identityClass.recordKind = read<uint32_t>(value, 5);
            for (std::size_t i = 0; i < identityClass.rawFields.size(); ++i)
                identityClass.rawFields[i] = read<uint32_t>(value, 9 + i * 4);
            insertUnique(model.identityClasses, identityClass.id, identityClass, "identity class");
        }
    for (std::size_t index = 0; index < all[identityOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, identityOrdinal, index);
        Identity identity;
        identity.rowTag = value[0];
        identity.id = read<uint32_t>(value, 1);
        identity.ownerId = read<uint32_t>(value, older782 ? 17 : older895 ? 9 : 5);
        identity.contextId = read<uint32_t>(value, 9);
        if (older895)
        {
            identity.classReferenceId = read<uint32_t>(value, 5);
            if (!model.identityClasses.count(identity.classReferenceId))
                throw std::runtime_error("missing identity class " + std::to_string(identity.classReferenceId));
        }
        if (older782) identity.flags = read<uint32_t>(value, 21); // legacy assembly reference
        if (older782 || older844)
            identity.guid = fixedString(value, 25, 39);
        else if (older895)
            identity.guid = fixedString(value, 17, 36);
        else
        {
            identity.guid = guid(value + 13);
            identity.type = read<uint32_t>(value, 29);
            identity.flags = read<uint32_t>(value, 53);
        }
        insertUnique(model.identities, identity.id, identity, "identity");
    }
    if (older895)
        for (const auto& entry : model.identities)
            if (entry.second.ownerId && !model.identities.count(entry.second.ownerId))
                throw std::runtime_error("missing identity owner " + std::to_string(entry.second.ownerId));

    if (library && all.size() > 68 && all[68].valid && all[68].payloadSize == 76)
    {
        if (older782) requireFields(data, all, 68, 6, {0});
        else if (!older844) requireFields(data, all, 68, 6, {0,1,4});
        for (std::size_t index = 0; index < all[68].rowCount; ++index)
        {
            const auto* value = row(data, all, 68, index);
            ParameterDefinition parameter;
            parameter.id = read<uint32_t>(value, 1);
            parameter.name = fixedString(value, 5, 31);
            parameter.label = fixedString(value, 36, 31);
            const auto expressionId = read<uint32_t>(value, 69);
            if (componentVariables && expressionId && !stringChunks.count(expressionId))
                throw std::runtime_error("missing parameter expression string " + std::to_string(expressionId));
            parameter.expression = componentVariables ? resolveString(expressionId, true) : strings[expressionId];
            parameter.valueType = read<uint32_t>(value, 73);
            const auto identity = model.identities.find(parameter.id);
            if (componentVariables && identity == model.identities.end())
                throw std::runtime_error("missing parameter identity " + std::to_string(parameter.id));
            if (identity != model.identities.end())
            {
                parameter.ownerId = identity->second.ownerId;
                parameter.guid = identity->second.guid;
            }
            insertUnique(model.parameterDefinitions, parameter.id, parameter, "parameter definition");
        }
    }

    std::unordered_map<uint32_t, std::vector<uint32_t>> childrenByOwner;
    childrenByOwner.reserve(model.identities.size() / 4 + 1);
    for (const auto& identity : model.identities)
        if (identity.second.ownerId && identity.second.ownerId != identity.first)
            childrenByOwner[identity.second.ownerId].push_back(identity.first);
    for (auto& entry : childrenByOwner)
        std::sort(entry.second.begin(), entry.second.end());

    std::unordered_map<uint32_t, std::vector<uint32_t>> parametersByOwner;
    parametersByOwner.reserve(model.parameterDefinitions.size() / 4 + 1);
    for (const auto& parameter : model.parameterDefinitions)
        parametersByOwner[parameter.second.ownerId].push_back(parameter.first);
    for (auto& entry : parametersByOwner)
        std::sort(entry.second.begin(), entry.second.end());

    std::vector<Association> associations;
    associations.reserve(all[associationOneOrdinal].rowCount + all[associationTwoOrdinal].rowCount);
    std::unordered_map<uint32_t, std::vector<std::size_t>> bySource;
    std::unordered_map<uint32_t, std::vector<std::size_t>> byTarget;
    for (const std::size_t ordinal : {associationOneOrdinal, associationTwoOrdinal})
        for (std::size_t index = 0; index < all[ordinal].rowCount; ++index)
        {
            const auto* value = row(data, all, ordinal, index);
            Association association{read<uint32_t>(value, 1), read<uint32_t>(value, 5),
                                    read<uint32_t>(value, 9), read<uint32_t>(value, 13),
                                    static_cast<uint32_t>(ordinal), {}};
            const auto associationIndex = associations.size();
            associations.push_back(std::move(association));
            bySource[associations.back().source].push_back(associationIndex);
            byTarget[associations.back().target].push_back(associationIndex);
        }

    std::unordered_map<uint32_t, std::vector<uint32_t>> distancesByOwner, formulasByOwner;
    if (componentVariables)
    {
        const auto variableIdentity = [&](uint32_t id) -> const Identity& {
            const auto found = model.identities.find(id);
            if (found == model.identities.end())
                throw std::runtime_error("missing component variable identity " + std::to_string(id));
            if (found->second.ownerId && !model.identities.count(found->second.ownerId))
                throw std::runtime_error("missing component variable owner " + std::to_string(found->second.ownerId));
            return found->second;
        };
        const auto variableString = [&](uint32_t id) {
            if (id && !stringChunks.count(id))
                throw std::runtime_error("missing component variable string " + std::to_string(id));
            return resolveString(id, true);
        };
        for (std::size_t index = 0; index < all[147].rowCount; ++index)
        {
            const auto* value = row(data, all, 147, index);
            DistanceParameter distance;
            distance.id = read<uint32_t>(value, 1);
            const auto& identity = variableIdentity(distance.id);
            distance.ownerId = identity.ownerId; distance.guid = identity.guid;
            distance.name = variableString(read<uint32_t>(value, 5));
            distance.label = variableString(read<uint32_t>(value, 9));
            distance.storedDistance = read<double>(value, 17);
            distance.secondaryStoredValue = read<double>(value, 25);
            if (!std::isfinite(distance.storedDistance) || !std::isfinite(distance.secondaryStoredValue))
                throw std::runtime_error("non-finite component distance " + std::to_string(distance.id));
            distance.propertyToken = variableString(read<uint32_t>(value, 57));
            distance.planeToken = variableString(read<uint32_t>(value, 61));
            const std::size_t offsets[] = {13,33,37,41,45,49,53};
            for (std::size_t i = 0; i < distance.rawFields.size(); ++i)
                distance.rawFields[i] = read<uint32_t>(value, offsets[i]);
            insertUnique(model.distanceParameters, distance.id, distance, "distance parameter");
            distancesByOwner[distance.ownerId].push_back(distance.id);
        }
        for (std::size_t index = 0; index < all[156].rowCount; ++index)
        {
            const auto* value = row(data, all, 156, index);
            FormulaBinding formula;
            formula.id = read<uint32_t>(value, 1);
            const auto& identity = variableIdentity(formula.id);
            formula.ownerId = identity.ownerId; formula.guid = identity.guid;
            formula.targetObjectId = read<uint32_t>(value, 5);
            if (!model.identities.count(formula.targetObjectId))
                throw std::runtime_error("missing formula target identity " + std::to_string(formula.targetObjectId));
            formula.storedIndex = read<uint32_t>(value, 9);
            formula.expression = variableString(read<uint32_t>(value, 13));
            formula.propertyName = fixedString(value, 17, 81);
            insertUnique(model.formulaBindings, formula.id, formula, "formula binding");
            formulasByOwner[formula.ownerId].push_back(formula.id);
            model.formulaBindingIdsByTarget[formula.targetObjectId].push_back(formula.id);
        }
        for (const auto& association : associations)
        {
            if (association.table != associationTwoOrdinal || (association.type != 58 && association.type != 59)) continue;
            if (!model.identities.count(association.target))
                throw std::runtime_error("missing component variable reference " + std::to_string(association.target));
            if (association.type == 58)
            {
                const auto found = model.distanceParameters.find(association.source);
                if (found == model.distanceParameters.end())
                    throw std::runtime_error("missing distance association source " + std::to_string(association.source));
                found->second.boundObjectIds.push_back(association.target);
            }
            else
            {
                const auto found = model.formulaBindings.find(association.source);
                if (found == model.formulaBindings.end())
                    throw std::runtime_error("missing formula association source " + std::to_string(association.source));
                found->second.referencedObjectIds.push_back(association.target);
                if (association.target != found->second.targetObjectId)
                    found->second.inputObjectIds.push_back(association.target);
            }
        }
        for (auto& entry : model.distanceParameters)
        {
            auto& distance = entry.second;
            if (distance.boundObjectIds.size() != 2)
                throw std::runtime_error("component distance requires two object bindings " + std::to_string(distance.id));
            std::sort(distance.boundObjectIds.begin(), distance.boundObjectIds.end());
        }
        for (auto& entry : model.formulaBindings)
        {
            auto& formula = entry.second;
            if (std::count(formula.referencedObjectIds.begin(), formula.referencedObjectIds.end(), formula.targetObjectId) != 1)
                throw std::runtime_error("formula association must contain its target once " + std::to_string(formula.id));
            std::sort(formula.referencedObjectIds.begin(), formula.referencedObjectIds.end());
            std::sort(formula.inputObjectIds.begin(), formula.inputObjectIds.end());
        }
        for (auto* map : {&distancesByOwner, &formulasByOwner, &model.formulaBindingIdsByTarget})
            for (auto& entry : *map) std::sort(entry.second.begin(), entry.second.end());
        for (auto& entry : model.distanceParameters)
        {
            const auto found = model.formulaBindingIdsByTarget.find(entry.first);
            if (found != model.formulaBindingIdsByTarget.end()) entry.second.formulaBindingIds = found->second;
        }
    }

    for (const auto& entry : parametersByOwner) model.variablesByOwner[entry.first].parameterIds = entry.second;
    for (const auto& entry : distancesByOwner) model.variablesByOwner[entry.first].distanceParameterIds = entry.second;
    for (const auto& entry : formulasByOwner) model.variablesByOwner[entry.first].formulaBindingIds = entry.second;

    if (olderSchema)
    {
        const auto assignType = [&](uint32_t id, uint32_t type) {
            if (older782 && !model.identities.count(id))
                throw std::runtime_error("broken DB1 association identity " + std::to_string(id));
            model.identities[id].type = type;
        };
        for (const auto& association : associations)
        {
            if (association.type == 9 || association.type == 11 || association.type == 12 || association.type == 38)
                assignType(association.target, association.type);
            else if (association.type == 13 || association.type == 4)
                assignType(association.source, association.type);
        }
        if (!older782)
        for (std::size_t index = 0; index < all[partOrdinal].rowCount; ++index)
        {
            const auto id = read<uint32_t>(row(data, all, partOrdinal, index), 1);
            if (model.identities[id].type == 0)
                model.identities[id].type = 2;
        }
    }

    if (verifiedPartOwnership || older782)
    {
        const std::size_t first = older782 ? (library ? 185 : 215) : (library ? 286 : 322);
        const std::size_t links = library ? 181 : 211;
        const std::size_t widths[] = {older782?64U:68U,older782?72U:76U,44};
        const std::size_t fields[] = {older782?8U:9U,older782?10U:11U,7};
        std::size_t unverified = 0;
        for (std::size_t kind=0; kind<(older782?2U:3U); ++kind)
        {
            requireTable(all, first+kind, static_cast<uint32_t>(widths[kind]));
            if (older782) requireFields(data, all, first+kind, fields[kind], {0});
            else requireFields(data, all, first+kind, fields[kind], {0,1,2});
            for (std::size_t i=0; i<all[first+kind].rowCount; ++i)
            {
                const auto* value = row(data, all, first+kind, i);
                ObjectNumberingRecord record;
                record.id = read<uint32_t>(value, 1);
                record.rawPayload.assign(value+1, value+1+widths[kind]);
                if (kind<2)
                {
                    record.kind = kind==0 ? ObjectNumberingKind::Part : ObjectNumberingKind::Assembly;
                    record.startNumber = read<uint32_t>(value, older782 ? 5 : 9);
                    record.sequence = read<uint32_t>(value, older782 ? 9 : 13);
                    record.prefix = fixedString(value, (kind==0 ? 21 : 29) - (older782 ? 4 : 0), 48);
                    const auto number = uint64_t(record.startNumber) + record.sequence;
                    if (record.startNumber>0 && record.startNumber<0x80000000U && record.sequence>0 && number<=0x80000000ULL)
                        record.positionNumber = static_cast<uint32_t>(number-1);
                }
                else ++unverified;
                insertUnique(model.objectNumberingRecords, record.id, record, "object numbering record");
            }
        }
        requireTable(all, links, 12);
        if (older782) requireFields(data, all, links, 4, {0});
        else requireFields(data, all, links, 4, {0,1,2,3});
        for (std::size_t i=0; i<all[links].rowCount; ++i)
        {
            const auto* value = row(data, all, links, i);
            ObjectNumberingReference reference{read<uint32_t>(value,1),read<uint32_t>(value,5),read<uint32_t>(value,9)};
            if (!model.identities.count(reference.objectId)) throw std::runtime_error("missing object numbering identity");
            if (reference.numberingRecordId && !model.objectNumberingRecords.count(reference.numberingRecordId))
                throw std::runtime_error("missing object numbering record " + std::to_string(reference.numberingRecordId));
            insertUnique(model.objectNumberingReferences, reference.objectId, reference, "object numbering reference");
        }
        if (unverified) model.diagnostics.push_back(std::to_string(unverified) + " object numbering records retain unverified 44-byte semantics");
    }

    const auto readPosition = [&](const uint8_t* value, uint32_t id, std::size_t start) {
        PartPosition position;
        position.id = id;
        position.startAxialOffset = read<float>(value, start);
        position.endAxialOffset = read<float>(value, start + 12);
        position.depthCode = read<uint32_t>(value, start + 24);
        position.depthOffset = read<float>(value, start + 28);
        position.planeCode = read<uint32_t>(value, start + 40);
        position.planeOffset = read<float>(value, start + 44);
        for (auto number : {position.startAxialOffset, position.endAxialOffset, position.depthOffset, position.planeOffset})
            if (!std::isfinite(number)) throw std::runtime_error("non-finite part position " + std::to_string(id));
        const std::size_t rawOffsets[] = {4,8,16,20,32,36};
        for (std::size_t i=0; i<position.rawFields.size(); ++i) position.rawFields[i] = read<uint32_t>(value, start + rawOffsets[i]);
        if (position.depthCode>2 || position.planeCode>2)
            model.diagnostics.push_back("unverified part position code for record " + std::to_string(id));
        return position;
    };
    std::unordered_map<uint32_t, uint32_t> definitionTypes;
    for (std::size_t index = 0; index < all[partDefinitionOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, partDefinitionOrdinal, index);
        PartDefinition definition;
        definition.id = read<uint32_t>(value, 1);
        definition.classNumber = fixedString(value, older782 ? 81 : 33, 22);
        definition.subtype = read<uint32_t>(value, 9);
        const auto nameLength = (older782 || older895) ? std::size_t(22) : std::size_t(62);
        definition.name = fixedString(value, older782 ? 103 : 55, nameLength);
        const auto family = fixedString(value, older782 ? 125 : olderSchema ? 145 : 117, older782 ? 64 : olderSchema ? 22 : 60);
        const auto dimensionId = read<uint32_t>(value, older782 ? 189 : olderSchema ? 141 : 181);
        if (older782 && dimensionId && !stringChunks.count(dimensionId))
            throw std::runtime_error("broken DB1 profile string reference for definition " + std::to_string(definition.id));
        definition.profile = family + strings[dimensionId];
        definition.secondaryName = fixedString(value, older782 ? 215 : olderSchema ? 167 : 207, 62);
        definition.material = fixedString(value, older782 ? 277 : olderSchema ? 229 : 269, older782 ? 32 : older844 ? 85 : 95);
        if (older782) definitionTypes[definition.id] = read<uint32_t>(value, 5);
        insertUnique(model.definitions, definition.id, definition, "definition");
        if (older782)
            insertUnique(model.partDefinitionPositions, definition.id, readPosition(value, definition.id, 21), "definition position");
    }

    struct ContourBlock { uint32_t sequence = 0; std::vector<ContourPoint> points; };
    std::unordered_map<uint32_t, std::vector<ContourBlock>> contourChunks;
    for (std::size_t index = 0; index < all[contourBlockOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, contourBlockOrdinal, index);
        if (!(value[0] & 0x04))
            continue;
        ContourBlock contour;
        contour.sequence = read<uint32_t>(value, 5);
        // The 332-byte Xsteel 8.x block omits the first local origin from its
        // coordinate arrays.  It is implicit only in sequence zero; following
        // chunks contain ten ordinary continuation points.
        if (olderSchema && !older782 && contour.sequence == 0)
            contour.points.push_back(ContourPoint{});
        for (int pointIndex = 0; pointIndex < 10; ++pointIndex)
        {
            const auto typeOffset = older782 ? 213 : olderSchema ? 217 : 337;
            const auto type = read<uint32_t>(value, typeOffset + pointIndex * 4);
            if (type == 0x7fffffff)
                break;
            ContourPoint point;
            if (olderSchema)
                point.value = {read<float>(value, (older782 ? 13 : 17) + pointIndex * 4),
                               read<float>(value, (older782 ? 53 : 57) + pointIndex * 4),
                               read<float>(value, (older782 ? 93 : 97) + pointIndex * 4)};
            else
                point.value = {read<double>(value, 17 + pointIndex * 8),
                               read<double>(value, 97 + pointIndex * 8),
                               read<double>(value, 177 + pointIndex * 8)};
            point.chamferX = read<float>(value, (older782 ? 133 : olderSchema ? 137 : 257) + pointIndex * 4);
            point.chamferY = read<float>(value, (older782 ? 173 : olderSchema ? 177 : 297) + pointIndex * 4);
            point.chamferType = type;
            point.chamferDz1 = read<float>(value, (older782 ? 253 : olderSchema ? 257 : 377) + pointIndex * 4);
            point.chamferDz2 = read<float>(value, (older782 ? 293 : olderSchema ? 297 : 417) + pointIndex * 4);
            contour.points.push_back(point);
        }
        contourChunks[read<uint32_t>(value, 1)].push_back(std::move(contour));
    }
    std::unordered_map<uint32_t, ContourBlock> contours;
    for (auto& entry : contourChunks)
    {
        std::sort(entry.second.begin(), entry.second.end(), [](const auto& one, const auto& two) {
            return one.sequence < two.sequence;
        });
        auto& target = contours[entry.first].points;
        for (auto& chunk : entry.second)
            target.insert(target.end(), chunk.points.begin(), chunk.points.end());
    }
    std::unordered_map<uint32_t, uint32_t> contourLinks;
    if (contourLinkOrdinal != noTable)
    for (std::size_t index = 0; index < all[contourLinkOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, contourLinkOrdinal, index);
        if (value[0] & 0x04)
            contourLinks[read<uint32_t>(value, 1)] = read<uint32_t>(value, 17);
    }

    struct Placement { uint32_t frameId = 0; Vec3 origin{}; double length = 0.0; };
    std::unordered_map<uint32_t, Placement> placements;
    for (std::size_t index = 0; index < all[placementOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, placementOrdinal, index);
        Placement placement;
        const auto id = read<uint32_t>(value, 1);
        placement.frameId = read<uint32_t>(value, 5);
        for (int axis = 0; axis < 3; ++axis)
            placement.origin[axis] = read<double>(value, 9 + axis * 8);
        placement.length = read<double>(value, 33);
        placements[id] = placement;
    }

    if (verifiedPartOwnership)
        for (std::size_t index = 0; index < all[partAuxiliaryOrdinal].rowCount; ++index)
        {
            const auto* value = row(data, all, partAuxiliaryOrdinal, index);
            const auto position = readPosition(value, read<uint32_t>(value, 1), 5);
            insertUnique(model.partPositions, position.id, position, "part position");
        }
    for (std::size_t index = 0; index < all[partOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, partOrdinal, index);
        Part part;
        part.id = read<uint32_t>(value, 1);
        part.definitionId = read<uint32_t>(value, 5);
        part.ownerId = older782 ? 0 : read<uint32_t>(value, 9);
        part.auxiliaryReferenceId = older782 ? 0 : read<uint32_t>(value, 9);
        if (verifiedPartOwnership && part.auxiliaryReferenceId && !model.partPositions.count(part.auxiliaryReferenceId))
            throw std::runtime_error("missing part auxiliary reference " + std::to_string(part.auxiliaryReferenceId));
        part.startPointId = read<uint32_t>(value, older782 ? 9 : 13);
        part.endPointId = read<uint32_t>(value, older782 ? 13 : 17);
        part.geometryReferenceId = read<uint32_t>(value, older782 ? 17 : 21);
        part.orientationId = read<uint32_t>(value, older782 ? 21 : 25);
        auto identity = model.identities.find(part.id);
        auto definition = model.definitions.find(part.definitionId);
        auto start = model.points.find(part.startPointId);
        auto end = model.points.find(part.endPointId);
        auto frame = model.frames.find(part.orientationId);
        if (identity == model.identities.end() || definition == model.definitions.end() ||
            start == model.points.end() || end == model.points.end() || frame == model.frames.end())
            throw std::runtime_error("broken DB1 part join for object " + std::to_string(part.id));
        if (older782)
        {
            identity->second.type = definitionTypes.at(part.definitionId);
            part.ownerId = identity->second.ownerId;
        }
        if (verifiedPartOwnership) part.ownerId = identity->second.ownerId;
        part.internalType = identity->second.type;
        part.contextId = identity->second.contextId;
        part.guid = identity->second.guid;
        part.name = definition->second.name;
        part.profile = definition->second.profile;
        part.material = definition->second.material;
        part.classNumber = definition->second.classNumber;
        part.start = start->second.value;
        part.end = end->second.value;
        part.axis = frame->second.axis;
        part.secondary = frame->second.secondary;
        part.normal = frame->second.normal;
        for (int axis = 0; axis < 3; ++axis)
            part.origin[axis] = read<double>(value, (older782 ? 25 : 33) + axis * 8);
        part.length = read<double>(value, older782 ? 49 : 57);
        if (older782 && part.geometryReferenceId)
        {
            const auto contour = contours.find(part.geometryReferenceId);
            if (contour == contours.end())
                throw std::runtime_error("broken DB1 contour reference for object " + std::to_string(part.id));
            part.contour = contour->second.points;
        }
        auto contourLink = contourLinks.find(part.geometryReferenceId);
        if (contourLink != contourLinks.end())
        {
            const auto contour = contours.find(contourLink->second);
            if (contour != contours.end())
                part.contour = contour->second.points;
        }
        // Corpus-verified subtype 2 contours are plates. Other modern subtype
        // meanings require evidence; the legacy subtype-4 mapping is not assumed.
        part.contourKindUnverified = !part.contour.empty() && definition->second.subtype != 2;
        if (part.contourKindUnverified)
            model.diagnostics.push_back("unverified modern contour kind for part " + std::to_string(part.id) +
                                        " (definition subtype " + std::to_string(definition->second.subtype) + ")");
        if (part.internalType == 2)
            model.actualPartIds.push_back(part.id);
        else if (part.internalType == 11 || part.internalType == 38)
            model.operativePartIds.push_back(part.id);
        else if (older782 && part.internalType == 10)
        {
            IndividualBolt bolt;
            bolt.id = part.id;
            for (const auto associationIndex : bySource[part.id])
            {
                const auto& association = associations[associationIndex];
                if (association.table == associationTwoOrdinal && association.type == 10)
                    bolt.connectedPartIds.push_back(association.target);
            }
            std::sort(bolt.connectedPartIds.begin(), bolt.connectedPartIds.end());
            bolt.connectedPartIds.erase(std::unique(bolt.connectedPartIds.begin(), bolt.connectedPartIds.end()), bolt.connectedPartIds.end());
            model.individualBolts.push_back(std::move(bolt));
        }
        else
        {
            model.unhandledPartIds.push_back(part.id);
            model.diagnostics.push_back("unhandled part type " + std::to_string(part.internalType) +
                                        " for object " + std::to_string(part.id));
        }
        insertUnique(model.parts, part.id, std::move(part), "part");
    }

    std::unordered_map<uint32_t, Property> propertyValues;
    for (std::size_t index = 0; index < all[numericPropertyOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, numericPropertyOrdinal, index);
        const auto id = read<uint32_t>(value, 1);
        const auto type = read<uint32_t>(value, 5);
        Property property;
        property.name = fixedString(value, 17, 28);
        property.group = "Tekla component attribute";
        const auto number = read<double>(value, 9);
        if (!std::isfinite(number))
            throw std::runtime_error("non-finite DB1 numeric property " + std::to_string(id));
        if (type == 0)
        {
            property.kind = Property::Kind::Integer;
            const auto rounded = std::round(number);
            if (rounded < (std::numeric_limits<int32_t>::min)() || rounded > (std::numeric_limits<int32_t>::max)())
                throw std::runtime_error("out-of-range DB1 integer property " + std::to_string(id));
            property.integerValue = static_cast<int32_t>(rounded);
        }
        else
        {
            property.kind = Property::Kind::Double;
            property.doubleValue = number;
        }
        propertyValues[id] = std::move(property);
    }
    for (std::size_t index = 0; index < all[stringPropertyOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, stringPropertyOrdinal, index);
        Property property;
        property.kind = Property::Kind::String;
        property.name = fixedString(value, 9, 20);
        property.stringValue = fixedString(value, 30, olderSchema ? 86 : 91);
        property.group = "Tekla user-defined attribute";
        propertyValues[read<uint32_t>(value, 1)] = std::move(property);
    }
    std::size_t rootOnlyUnresolved = 0;
    for (const auto ordinal : {propertyLinkOneOrdinal, propertyLinkTwoOrdinal})
        for (std::size_t index = 0; index < all[ordinal].rowCount; ++index)
        {
            const auto* value = row(data, all, ordinal, index);
            const auto propertyId = read<uint32_t>(value, 5);
            const auto ownerId = read<uint32_t>(value, 9);
            const auto found = propertyValues.find(propertyId);
            if (found != propertyValues.end())
                model.properties[ownerId].push_back(found->second);
            else if (ownerId == 7)
                ++rootOnlyUnresolved;
            else
                throw std::runtime_error("unresolved DB1 object property " + std::to_string(propertyId));
        }
    if (rootOnlyUnresolved)
        model.diagnostics.emplace_back(std::to_string(rootOnlyUnresolved) + " unresolved database-root bookkeeping links ignored");
    for (auto& entry : model.parts)
        entry.second.properties = model.properties[entry.first];

    if (older782)
    {
        const std::regex thicknessPattern(R"(^PL([+]?[0-9]+(?:\.[0-9]+)?)$)");
        for (std::size_t index = 0; index < all[surfaceDefinitionOrdinal].rowCount; ++index)
        {
            const auto* value = row(data, all, surfaceDefinitionOrdinal, index);
            SurfaceTreatmentDefinition definition;
            definition.id = read<uint32_t>(value, 1);
            definition.classNumber = fixedString(value, 69, 22);
            definition.name = fixedString(value, 91, 22);
            definition.profile = fixedString(value, 113, 62);
            definition.material = fixedString(value, 175, 32);
            definition.typeName = fixedString(value, 207, 62);
            definition.typeCode = read<uint32_t>(value, 269);
            for (std::size_t i=0; i<definition.rawHeader.size(); ++i) definition.rawHeader[i] = read<uint32_t>(value, 5+i*4);
            for (std::size_t i=0; i<definition.rawTail.size(); ++i) definition.rawTail[i] = read<uint32_t>(value, 273+i*4);
            std::smatch match;
            if (std::regex_match(definition.profile, match, thicknessPattern))
            {
                std::istringstream input(match[1].str()); input.imbue(std::locale::classic());
                double thickness = 0.0; input >> thickness;
                if (!input || !std::isfinite(thickness)) throw std::runtime_error("invalid surface profile thickness");
                definition.thicknessFromProfile = thickness;
            }
            insertUnique(model.surfaceTreatmentDefinitions, definition.id, std::move(definition), "surface treatment definition");
        }
        std::unordered_map<uint32_t, uint32_t> fathers;
        for (const auto& association : associations)
            if (association.table == associationOneOrdinal && association.type == 73)
            {
                if (!model.parts.count(association.source)) throw std::runtime_error("missing surface treatment father part");
                if (!fathers.emplace(association.target, association.source).second)
                    throw std::runtime_error("multiple surface treatment father associations");
            }
        for (std::size_t index = 0; index < all[surfaceOrdinal].rowCount; ++index)
        {
            const auto* value = row(data, all, surfaceOrdinal, index);
            SurfaceTreatment surface;
            surface.id = read<uint32_t>(value, 1);
            surface.definitionId = read<uint32_t>(value, 5);
            surface.startPointId = read<uint32_t>(value, 9);
            surface.endPointId = read<uint32_t>(value, 13);
            surface.contourId = read<uint32_t>(value, 17);
            surface.orientationId = read<uint32_t>(value, 21);
            const auto identity = model.identities.find(surface.id);
            const auto father = fathers.find(surface.id);
            const auto start = model.points.find(surface.startPointId), end = model.points.find(surface.endPointId);
            const auto frame = model.frames.find(surface.orientationId);
            const auto contour = contours.find(surface.contourId);
            if (identity == model.identities.end() || father == fathers.end() ||
                !model.surfaceTreatmentDefinitions.count(surface.definitionId) ||
                start == model.points.end() || end == model.points.end() || frame == model.frames.end() || contour == contours.end())
                throw std::runtime_error("broken surface treatment join for object " + std::to_string(surface.id));
            surface.fatherPartId = father->second;
            surface.ownerId = identity->second.ownerId; surface.guid = identity->second.guid;
            surface.start = start->second.value; surface.end = end->second.value;
            surface.axis = frame->second.axis; surface.secondary = frame->second.secondary; surface.normal = frame->second.normal;
            for (std::size_t axis=0; axis<3; ++axis) surface.origin[axis] = read<double>(value, 25+axis*8);
            surface.storedLength = read<double>(value, 49);
            surface.contour = contour->second.points;
            std::copy(value+57, value+79, surface.rawTail.begin());
            const auto properties = model.properties.find(surface.id);
            if (properties != model.properties.end()) surface.properties = properties->second;
            const auto formulas = model.formulaBindingIdsByTarget.find(surface.id);
            if (formulas != model.formulaBindingIdsByTarget.end()) surface.formulaBindingIds = formulas->second;
            model.surfaceTreatmentIdsByFather[surface.fatherPartId].push_back(surface.id);
            insertUnique(model.surfaceTreatments, surface.id, std::move(surface), "surface treatment");
        }
        for (const auto& father : fathers)
            if (!model.surfaceTreatments.count(father.first)) throw std::runtime_error("surface father association has missing target");
        for (const auto& entry : model.distanceParameters)
            for (auto id : entry.second.boundObjectIds)
            {
                const auto surface = model.surfaceTreatments.find(id);
                if (surface != model.surfaceTreatments.end()) surface->second.distanceParameterIds.push_back(entry.first);
            }
        for (auto& entry : model.surfaceTreatments)
        {
            auto& ids = entry.second.distanceParameterIds;
            std::sort(ids.begin(), ids.end()); ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        }
        for (auto& entry : model.surfaceTreatmentIdsByFather) std::sort(entry.second.begin(), entry.second.end());
    }

    for (const auto& id : model.operativePartIds)
    {
        const auto& operative = model.parts.at(id);
        uint32_t father = 0;
        for (const auto associationIndex : byTarget[id])
        {
            const auto& association = associations[associationIndex];
            if (association.table == associationOneOrdinal && association.type == operative.internalType && model.parts.count(association.source))
                father = association.source;
        }
        if (!father)
            throw std::runtime_error("DB1 boolean operation has no father part: " + std::to_string(id));
        model.booleans.push_back({id, father, operative.internalType == 38});
    }
    for (const auto type : {uint32_t(9), uint32_t(12)})
        for (const auto& identityEntry : model.identities)
        {
            if (identityEntry.second.type != type)
                continue;
            const auto placement = placements.find(identityEntry.first);
            if (placement == placements.end() || !model.frames.count(placement->second.frameId))
                throw std::runtime_error("DB1 plane operation has no placement");
            uint32_t father = 0;
            for (const auto associationIndex : byTarget[identityEntry.first])
            {
                const auto& association = associations[associationIndex];
                if (association.table == associationOneOrdinal && association.type == type && model.parts.count(association.source))
                    father = association.source;
            }
            if (!father)
                throw std::runtime_error("DB1 plane operation has no father part");
            const auto& frame = model.frames.at(placement->second.frameId);
            PlaneOperation operation{identityEntry.first, father, type, identityEntry.second.guid,
                                     placement->second.origin, frame.axis, frame.secondary, frame.normal,
                                     placement->second.length, model.properties[identityEntry.first]};
            (type == 9 ? model.fittings : model.cutPlanes).push_back(std::move(operation));
        }

    if (boltDefinitionOrdinal != noTable)
    for (std::size_t index = 0; index < all[boltDefinitionOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, boltDefinitionOrdinal, index);
        BoltDefinition definition;
        definition.id = read<uint32_t>(value, 1);
        definition.count = read<uint32_t>(value, 21);
        definition.classNumber = fixedString(value, 29, 20);
        definition.name = fixedString(value, 51, 42);
        definition.standard = fixedString(value, 157, 94);
        definition.diameter = read<float>(value, 253);
        definition.tolerance = read<float>(value, 257);
        definition.length = read<float>(value, 265);
        definition.extraLength = read<float>(value, 269);
        definition.boltType = read<uint32_t>(value, 273);
        model.boltDefinitions[definition.id] = definition;
    }
    std::unordered_map<uint32_t, std::vector<BoltLayer>> boltLayers;
    if (boltLayerOrdinal != noTable)
    for (std::size_t index = 0; index < all[boltLayerOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, boltLayerOrdinal, index);
        BoltLayer layer;
        const auto groupId = read<uint32_t>(value, 1);
        layer.sequence = read<uint32_t>(value, 5);
        layer.partId = read<uint32_t>(value, 9);
        layer.flags = read<uint32_t>(value, 13);
        for (int item = 0; item < 4; ++item)
            layer.values[item] = read<double>(value, 17 + item * 8);
        boltLayers[groupId].push_back(layer);
    }
    if (boltGroupOrdinal != noTable)
    for (std::size_t index = 0; index < all[boltGroupOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, boltGroupOrdinal, index);
        BoltGroup group;
        group.id = read<uint32_t>(value, 1);
        group.definitionId = read<uint32_t>(value, 5);
        const auto firstId = read<uint32_t>(value, 13);
        const auto secondId = read<uint32_t>(value, 17);
        const auto pointArrayId = read<uint32_t>(value, 21);
        if (!model.points.count(firstId) || !model.points.count(secondId) || !contours.count(pointArrayId) ||
            !model.boltDefinitions.count(group.definitionId))
            throw std::runtime_error("broken DB1 bolt-group join");
        group.first = model.points.at(firstId).value;
        group.second = model.points.at(secondId).value;
        const auto placement = placements.find(group.id);
        if (placement == placements.end() || !model.frames.count(placement->second.frameId))
            throw std::runtime_error("broken DB1 bolt-group placement");
        const auto& frame = model.frames.at(placement->second.frameId);
        group.origin = placement->second.origin;
        group.axis = frame.axis;
        group.secondary = frame.secondary;
        group.normal = frame.normal;
        group.placementLength = placement->second.length;
        for (const auto& point : contours.at(pointArrayId).points)
            group.positions.push_back(point.value);
        group.layers = boltLayers[group.id];
        std::sort(group.layers.begin(), group.layers.end(), [](const auto& one, const auto& two) { return one.sequence < two.sequence; });
        for (const auto associationIndex : bySource[group.id])
        {
            const auto& association = associations[associationIndex];
            if (association.table == associationTwoOrdinal && association.type == 10 && model.parts.count(association.target))
                group.connectedPartIds.push_back(association.target);
        }
        std::sort(group.connectedPartIds.begin(), group.connectedPartIds.end());
        group.connectedPartIds.erase(std::unique(group.connectedPartIds.begin(), group.connectedPartIds.end()), group.connectedPartIds.end());
        if (group.connectedPartIds.empty())
            for (const auto& layer : group.layers)
                if (model.parts.count(layer.partId))
                    group.connectedPartIds.push_back(layer.partId);
        group.guid = model.identities[group.id].guid;
        group.properties = model.properties[group.id];
        model.boltGroups.push_back(std::move(group));
    }

    for (std::size_t index = 0; index < all[weldDefinitionOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, weldDefinitionOrdinal, index);
        WeldDefinition definition{read<uint32_t>(value, 1), read<float>(value, 9), read<uint32_t>(value, 13)};
        model.weldDefinitions[definition.id] = definition;
    }
    for (std::size_t index = 0; index < all[weldOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, weldOrdinal, index);
        const auto id = read<uint32_t>(value, 1);
        if (!model.identities.count(id) || model.identities.at(id).type != 13)
            continue;
        const auto placement = placements.find(id);
        if (placement == placements.end() || !model.frames.count(placement->second.frameId))
            throw std::runtime_error("broken DB1 weld placement");
        Weld weld;
        weld.id = id;
        weld.definitionId = read<uint32_t>(value, 9);
        weld.secondaryDefinitionId = read<uint32_t>(value, 13);
        weld.guid = model.identities.at(id).guid;
        weld.origin = placement->second.origin;
        weld.length = placement->second.length;
        const auto& frame = model.frames.at(placement->second.frameId);
        weld.axis = frame.axis; weld.secondary = frame.secondary; weld.normal = frame.normal;
        for (const auto associationIndex : bySource[id])
        {
            const auto& association = associations[associationIndex];
            if (association.table == associationTwoOrdinal && association.type == 13 && model.parts.count(association.target))
                weld.connectedPartIds.push_back(association.target);
        }
        std::sort(weld.connectedPartIds.begin(), weld.connectedPartIds.end());
        weld.connectedPartIds.erase(std::unique(weld.connectedPartIds.begin(), weld.connectedPartIds.end()), weld.connectedPartIds.end());
        if (weld.connectedPartIds.size() != 2)
            throw std::runtime_error("DB1 weld does not have exactly two connected parts");
        model.welds.push_back(std::move(weld));
    }

    struct Relation { uint32_t owner = 0; uint32_t type = 0; uint32_t first = 0; uint32_t second = 0; };
    std::unordered_map<uint32_t, Relation> relations;
    if (relationOrdinal != noTable)
    for (std::size_t index = 0; index < all[relationOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, relationOrdinal, index);
        relations[read<uint32_t>(value, 1)] = {read<uint32_t>(value, 5), read<uint32_t>(value, 9),
                                               read<uint32_t>(value, 13), read<uint32_t>(value, 17)};
    }
    std::unordered_map<uint32_t, Assembly> assemblies;
    for (std::size_t index = 0; index < all[assemblyOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, assemblyOrdinal, index);
        if ((older782 || older844) && read<uint32_t>(value, 5) != 15)
            continue;
        Assembly assembly;
        assembly.id = read<uint32_t>(value, 1);
        assembly.guid = model.identities[assembly.id].guid;
        assembly.name = (older782 || older844) ? legacyString(value, 21, 68) : fixedString(value, 21, 71);
        assembly.properties = model.properties[assembly.id];
        if (older782 || older844)
        {
            const auto primary = read<uint32_t>(value, 13);
            if (model.parts.count(primary) && model.parts.at(primary).internalType == 2)
                assembly.memberIds.push_back(primary);
            model.identities[assembly.id].type = 15;
        }
        assemblies[assembly.id] = std::move(assembly);
    }
    if (relationOrdinal != noTable)
    for (const auto& entry : relations)
    {
        const auto& relation = entry.second;
        if (relation.type != 52 || !assemblies.count(relation.owner) || !relations.count(relation.first))
            continue;
        const auto partId = relations.at(relation.first).owner;
        if (model.parts.count(partId) && model.parts.at(partId).internalType == 2)
            assemblies[relation.owner].memberIds.push_back(partId);
    }
    if (older782 || older844)
        for (const auto partId : model.actualPartIds)
        {
            const auto identity = model.identities.find(partId);
            if (identity != model.identities.end())
            {
                const auto assemblyId = older782 ? identity->second.flags : identity->second.ownerId;
                if (assemblies.count(assemblyId)) assemblies[assemblyId].memberIds.push_back(partId);
                else if (older782 && assemblyId)
                    model.diagnostics.push_back("unresolved stored assembly reference " + std::to_string(assemblyId) +
                                                " for part " + std::to_string(partId));
            }
        }
    for (auto& entry : assemblies)
    {
        auto& members = entry.second.memberIds;
        std::sort(members.begin(), members.end());
        members.erase(std::unique(members.begin(), members.end()), members.end());
        model.assemblies.push_back(std::move(entry.second));
    }

    for (std::size_t index = 0; index < all[componentOrdinal].rowCount; ++index)
    {
        const auto* value = row(data, all, componentOrdinal, index);
        Component component;
        component.id = read<uint32_t>(value, 1);
        component.name = strings[read<uint32_t>(value, 13)];
        component.primaryObjectId = read<uint32_t>(value, 17);
        component.referenceObjectId = read<uint32_t>(value, 21);
        component.number = read<uint32_t>(value, 73);
        const auto identity = model.identities.find(component.id);
        if (identity != model.identities.end())
        {
            component.guid = identity->second.guid;
            component.ownerId = identity->second.ownerId;
        }
        component.properties = model.properties[component.id];
        const auto children = childrenByOwner.find(component.id);
        if (children != childrenByOwner.end())
            component.childIds = children->second;
        const auto parameters = parametersByOwner.find(component.id);
        if (parameters != parametersByOwner.end()) component.parameterIds = parameters->second;
        const auto distances = distancesByOwner.find(component.id);
        if (distances != distancesByOwner.end()) component.distanceParameterIds = distances->second;
        const auto formulas = formulasByOwner.find(component.id);
        if (formulas != formulasByOwner.end()) component.formulaBindingIds = formulas->second;
        model.components.push_back(std::move(component));
    }

    if (!older844)
    {
        for (const auto& association : associations)
            if (association.table == associationTwoOrdinal && association.type == 4)
            {
                if (!model.identities.count(association.source) || !model.identities.count(association.target))
                    throw std::runtime_error("missing custom component reference identity");
                model.customComponentReferences[association.source].push_back(association.target);
            }
        for (auto& entry : model.customComponentReferences) std::sort(entry.second.begin(), entry.second.end());
        if (!model.customComponentReferences.empty())
            model.diagnostics.emplace_back("type-4 associations are custom component object references, not control lines; control-object decoding remains unverified");
    }
    const std::size_t customDefinitionOrdinal = library782 ? 125 : 226;
    if (library && all.size() > customDefinitionOrdinal && all[customDefinitionOrdinal].valid &&
        all[customDefinitionOrdinal].payloadSize == (library782 ? 32U : 36U))
    {
        if (!older782 && !older844) requireFields(data, all, customDefinitionOrdinal, 10, {0,1,7,8,9});
        std::unordered_set<uint32_t> definitionIds;
        for (std::size_t index = 0; index < all[customDefinitionOrdinal].rowCount; ++index)
        {
            const auto* value = row(data, all, customDefinitionOrdinal, index);
            CustomComponentDefinition definition;
            definition.id = read<uint32_t>(value, 1);
            if (!definitionIds.insert(definition.id).second)
                throw std::runtime_error("duplicate custom component definition " + std::to_string(definition.id));
            definition.kind = read<uint32_t>(value, 5);
            definition.classificationCode = read<uint32_t>(value, 21);
            for (std::size_t item = 0; item < (library782 ? 2U : definition.referenceIds.size()); ++item)
                definition.referenceIds[item] = read<uint32_t>(value, 25 + item * 4);
            if (library782)
            {
                for (auto ref : definition.referenceIds)
                    if (ref && !stringChunks.count(ref))
                        throw std::runtime_error("missing custom definition string " + std::to_string(ref));
                definition.description = strings[definition.referenceIds[0]];
            }
            definition.name = componentVariables ? resolveString(definition.referenceIds[1], true) : strings[definition.referenceIds[1]];
            const auto identity = model.identities.find(definition.id);
            if (componentVariables && identity == model.identities.end())
                throw std::runtime_error("missing custom definition identity " + std::to_string(definition.id));
            if (identity != model.identities.end())
                definition.guid = identity->second.guid;
            const auto parameters = parametersByOwner.find(definition.id);
            if (parameters != parametersByOwner.end())
                definition.parameterIds = parameters->second;
            const auto children = childrenByOwner.find(definition.id);
            if (children != childrenByOwner.end())
                definition.childObjectIds = children->second;
            const auto distances = distancesByOwner.find(definition.id);
            if (distances != distancesByOwner.end()) definition.distanceParameterIds = distances->second;
            const auto formulas = formulasByOwner.find(definition.id);
            if (formulas != formulasByOwner.end()) definition.formulaBindingIds = formulas->second;
            const auto references = model.customComponentReferences.find(definition.id);
            if (references != model.customComponentReferences.end()) definition.referenceObjectIds = references->second;
            model.customComponentDefinitions.push_back(std::move(definition));
        }
    }

    // Preserve the historical 8.44 path pending independent layout evidence.
    if (older844)
    for (const auto& identity : model.identities)
    {
        if (identity.second.type != 4)
            continue;
        ControlLine control{identity.first, identity.second.guid, {}, model.properties[identity.first]};
        for (const auto associationIndex : bySource[identity.first])
        {
            const auto& association = associations[associationIndex];
            if (association.table == associationTwoOrdinal && association.type == 4 && model.points.count(association.target))
                control.pointIds.push_back(association.target);
        }
        std::sort(control.pointIds.begin(), control.pointIds.end());
        control.pointIds.erase(std::unique(control.pointIds.begin(), control.pointIds.end()), control.pointIds.end());
        model.controlLines.push_back(std::move(control));
    }

    std::sort(model.actualPartIds.begin(), model.actualPartIds.end());
    std::sort(model.operativePartIds.begin(), model.operativePartIds.end());
    std::sort(model.unhandledPartIds.begin(), model.unhandledPartIds.end());

}
}

bool parseModelFile(const std::filesystem::path& path, Model& model, std::string& error)
{
    return parseModelFile(path, model, error, ModelReadOptions{});
}

bool parseModelFile(const std::filesystem::path& path, Model& model, std::string& error,
                    const ModelReadOptions& options)
{
    try
    {
        error.clear();
        model = {};
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Tekla DB1 input must be a regular file");
        const auto directory = std::filesystem::absolute(path).parent_path();
        model.directory = directory;
        model.databasePath = std::filesystem::absolute(path);
        const auto metadataPath = directory / "TeklaStructuresModel.xml";
        if (std::filesystem::is_regular_file(metadataPath))
        {
            std::string metadataError;
            if (!parseModelMetadata(metadataPath, model.metadata, metadataError))
                throw std::runtime_error("cannot parse TeklaStructuresModel.xml: " + metadataError);
        }
        parseProfileDatabase(directory / "profdb.bin", model);
        const auto profileGeometryPath = directory / "pgdb.bin";
        if (std::filesystem::is_regular_file(profileGeometryPath))
        {
            ProfileGeometryCatalog catalog;
            std::string catalogError;
            if (!parseProfileGeometryCatalog(profileGeometryPath, catalog, catalogError))
                throw std::runtime_error("cannot parse pgdb.bin: " + catalogError);
            for (const auto& entry : catalog.profiles)
            {
                Profile profile;
                profile.type = 998;
                profile.name = entry.first;
                profile.source = ProfileSource::SketchSolver;
                if (!entry.second.contour.empty())
                    profile.fixedContours.push_back(entry.second.contour);
                model.profiles[profile.name] = std::move(profile);
            }
            for (const auto& diagnostic : catalog.diagnostics)
                model.diagnostics.push_back("pgdb.bin: " + diagnostic);
        }
        parseDatabase(detail::readPayload(model.databasePath, options.maxDecodedBytes), model, false);
        validateModel(model);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        model = {};
        return false;
    }
}

bool parseModelDirectory(const std::filesystem::path& directory, Model& model, std::string& error)
{
    return parseModelDirectory(directory, model, error, ModelReadOptions{});
}

bool parseModelDirectory(const std::filesystem::path& directory, Model& model, std::string& error,
                         const ModelReadOptions& options)
{
    try
    {
        error.clear();
        model = {};
        if (!std::filesystem::is_directory(directory))
            throw std::runtime_error("Tekla DB1 input must be a model directory");
        const auto path = findMainDatabase(directory);
        if (path.empty())
            throw std::runtime_error("the model directory contains no main .db1 file");
        return parseModelFile(path, model, error, options);
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        model = {};
        return false;
    }
}

bool parseComponentLibrary(const std::filesystem::path& path, Model& model, std::string& error)
{
    return parseComponentLibrary(path, model, error, ModelReadOptions{});
}

bool parseComponentLibrary(const std::filesystem::path& path, Model& model, std::string& error,
                           const ModelReadOptions& options)
{
    try
    {
        model = {};
        if (!std::filesystem::is_regular_file(path))
            throw std::runtime_error("Tekla component library path is not a file");
        error.clear();
        model.directory = path.parent_path();
        model.databasePath = path;
        const auto profilePath = model.directory / "profdb.bin";
        if (std::filesystem::is_regular_file(profilePath))
            parseProfileDatabase(profilePath, model);
        parseDatabase(detail::readPayload(path, options.maxDecodedBytes), model, true);
        validateModel(model);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        model = {};
        return false;
    }
}

bool parseRawDatabase(const std::filesystem::path& path, RawDatabase& database,
                      std::string& error, const RawDatabaseOptions& options)
{
    try
    {
        database = {};
        error.clear();
        database.sourcePath = path;
        auto data = detail::readPayload(path, options.maxDecodedBytes);
        if (data.size() < 8)
            throw std::runtime_error("DB1 file is truncated");
        const bool xsteel = data.size() >= 6 && std::memcmp(data.data(), "Xsteel", 6) == 0;
        const bool namedVariableDatabase = data.size() >= 8 && std::memcmp(data.data(), "DBV@", 4) == 0;
        const bool legacyVariableDatabase = data.size() >= 24 && read<uint32_t>(data.data(),0) == 1 &&
            read<uint32_t>(data.data(),4) == 0 && read<uint32_t>(data.data(),8) == 0 &&
            read<uint32_t>(data.data(),12) == 0 && read<uint32_t>(data.data(),16) == 1 &&
            read<uint32_t>(data.data(),20) == 0xdbcec0bc;
        const bool variableDatabase = namedVariableDatabase || legacyVariableDatabase;
        if (!xsteel && !variableDatabase)
            throw std::runtime_error("file is neither an Xsteel nor a DBV database");

        auto filename = detail::pathUtf8(path.filename());
        std::transform(filename.begin(), filename.end(), filename.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool numbering = filename.size() >= 4 && filename.substr(filename.size() - 4) == ".db2";
        const bool drawing = filename.size() >= 3 && filename.substr(filename.size() - 3) == ".dg";
        if (numbering || drawing)
        {
            if (drawing) detail::drawingContainer(data,database);
            else detail::numberingContainer(data,database);
            if (options.retainDecompressedFileImage) database.decompressedFileImage=std::move(data);
            return true;
        }
        database.kind = variableDatabase ? DatabaseKind::Environment :
                            (numbering ? DatabaseKind::Numbering :
                            (startsWithInsensitive(filename, "xslib")
                                 ? DatabaseKind::ComponentLibrary
                                 : DatabaseKind::Model));
        const std::string header(reinterpret_cast<const char*>(data.data()),
                                 (std::min)(data.size(), std::size_t(160)));
        if (namedVariableDatabase)
        {
            const auto length = read<std::uint32_t>(data.data(),4);
            if (length == 0 || length > data.size() - 8)
                throw std::runtime_error("invalid DBV container-name length");
            database.containerName.assign(reinterpret_cast<const char*>(data.data()+8),length);
            database.storageVersion = "DBV";
        }
        else if (legacyVariableDatabase)
            database.storageVersion = "DBV-legacy";
        const auto versionAt = xsteel ? header.find_first_of("0123456789", 6) : std::string::npos;
        if (xsteel && versionAt != std::string::npos)
        {
            auto versionEnd = versionAt;
            while (versionEnd < header.size() &&
                   ((header[versionEnd] >= '0' && header[versionEnd] <= '9') || header[versionEnd] == '.'))
                ++versionEnd;
            database.storageVersion = header.substr(versionAt, versionEnd - versionAt);
            auto guidAt = versionEnd;
            while (guidAt < header.size() && header[guidAt] == ' ')
                ++guidAt;
            if (guidAt + 36 <= header.size() &&
                std::count(header.begin() + guidAt, header.begin() + guidAt + 36, '-') == 4)
                database.databaseGuid = header.substr(guidAt, 36);
        }

        const auto offsets = sectionOffsets(data, variableDatabase);
        if (!offsets.empty())
        {
            database.layout = DatabaseLayout::ModernSections;
            const auto all = tables(data, offsets, variableDatabase);
            if (all.size() != offsets.size())
                throw std::runtime_error("DB1 section enumeration is inconsistent");
            database.preamble.assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(offsets.front()));
            database.tables.reserve(all.size());
            for (std::size_t ordinal = 0; ordinal < all.size(); ++ordinal)
            {
                const auto& source = all[ordinal];
                RawTable target;
                target.ordinal = static_cast<std::uint32_t>(ordinal);
                target.payloadSize = source.payloadSize;
                target.fileOffset = source.offset;
                target.schemaValid = source.valid;
                const auto next = ordinal + 1 < offsets.size() ? offsets[ordinal + 1] : data.size();
                if (!source.valid)
                {
                    target.opaqueSection.assign(data.begin() + static_cast<std::ptrdiff_t>(source.offset),
                                                data.begin() + static_cast<std::ptrdiff_t>(next));
                    database.diagnostics.push_back("preserved non-tabular section " + std::to_string(ordinal) +
                                                   " as opaque bytes");
                    database.tables.push_back(std::move(target));
                    continue;
                }
                target.fieldDescriptors.reserve(source.fieldCount);
                for (std::size_t field = 0; field < source.fieldCount; ++field)
                    target.fieldDescriptors.push_back(read<std::uint32_t>(data.data(), source.offset + 12 + field * 4));
                target.records.reserve(source.rowCount);
                for (std::size_t index = 0; index < source.rowCount; ++index)
                {
                    const auto* value = row(data, all, ordinal, index);
                    RawRecord record;
                    record.allocationTag = value[0];
                    record.fileOffset = static_cast<std::size_t>(value - data.data());
                    record.payload.assign(value + 1, value + 1 + source.payloadSize);
                    record.allocatorMetadata.assign(value + 1 + source.payloadSize,
                                                    value + 1 + source.payloadSize + 8);
                    target.records.push_back(std::move(record));
                }
                target.trailer.assign(data.begin() + static_cast<std::ptrdiff_t>(next - source.trailerSize),
                                      data.begin() + static_cast<std::ptrdiff_t>(next));
                database.tables.push_back(std::move(target));
            }
        }
        else
        {
            database.layout = DatabaseLayout::LegacyTables;
            const auto all = legacyTables(data);
            if (all.empty())
            {
                if (numbering || variableDatabase)
                {
                    database.layout = DatabaseLayout::Opaque;
                    database.preamble = data;
                    if (options.retainDecompressedFileImage) database.decompressedFileImage = std::move(data);
                    database.diagnostics.emplace_back("container preserved as opaque bytes; semantic decoding is not implemented");
                    return true;
                }
                throw std::runtime_error("Xsteel file contains neither modern sections nor legacy tables");
            }
            std::vector<std::uint32_t> ordinals;
            ordinals.reserve(all.size());
            for (const auto& entry : all)
                ordinals.push_back(entry.first);
            std::sort(ordinals.begin(), ordinals.end());
            database.tables.reserve(ordinals.size());
            for (const auto ordinal : ordinals)
            {
                const auto& source = all.at(ordinal);
                RawTable target;
                target.ordinal = ordinal;
                target.payloadSize = source.payloadSize;
                target.fileOffset = source.rowsOffset - 16;
                target.schemaValid = true;
                target.records.reserve(source.rowCount);
                for (std::size_t index = 0; index < source.rowCount; ++index)
                {
                    const auto* value = legacyRow(data, source, index);
                    RawRecord record;
                    record.allocationTag = value[0];
                    record.fileOffset = static_cast<std::size_t>(value - data.data());
                    record.payload.assign(value + 1, value + 1 + source.payloadSize);
                    target.records.push_back(std::move(record));
                }
                database.tables.push_back(std::move(target));
            }
            database.preamble.assign(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(database.tables.front().fileOffset));
            database.diagnostics.push_back(
                "legacy allocator gaps are retained only when retainDecompressedFileImage is enabled");
        }
        if (options.retainDecompressedFileImage)
            database.decompressedFileImage = std::move(data);
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        database = {};
        return false;
    }
}

bool parseProfileCatalog(const std::filesystem::path& path, ProfileCatalog& result, std::string& error)
{
    try
    {
        result = {};
        const auto data = inflateGzip(path);
        if (data.size() < 4)
            throw std::runtime_error("profdb.bin is truncated");
        result.version = read<uint32_t>(data.data(), 0);
        Model model;
        parseProfileDatabase(path, model);
        result.profiles = std::move(model.profiles);
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
