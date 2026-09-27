#include <tekla/GuidMappings.hpp>
#include "BinaryIO.hpp"
#include "RelatedContainers.hpp"
#include <map>
#include <set>
#include <sstream>

namespace tekla
{
namespace
{
std::string canonical(std::string text)
{
    if (text.size() == 38 && (text[0] == 'I' || text[0] == 'i') && (text[1] == 'D' || text[1] == 'd'))
        text.erase(0, 2);
    if (!db1::detail::guidText(text)) return {};
    for (auto& c : text) if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
    return text;
}
std::map<std::string, std::vector<std::uint32_t>> modelGuids(const db1::Model& model)
{
    std::map<std::string, std::vector<std::uint32_t>> ids;
    for (const auto& entry : model.identities)
    {
        const auto guid = canonical(entry.second.guid);
        if (!guid.empty() && guid != "00000000-0000-0000-0000-000000000000") ids[guid].push_back(entry.first);
    }
    return ids;
}
}
bool parseGuidMappingFile(const std::filesystem::path& path, GuidMappingFile& result,
                          std::string& error, std::size_t maxDecodedBytes)
{
    result = {}; error.clear();
    try
    {
        const auto bytes = db1::detail::readPayload(path, maxDecodedBytes);
        GuidMappingFile parsed;
        parsed.sourcePath = path; parsed.rawText.assign(bytes.begin(), bytes.end());
        if (parsed.rawText.find('\0') != std::string::npos || parsed.rawText.compare(0, 2, "\xff\xfe") == 0 ||
            parsed.rawText.compare(0, 2, "\xfe\xff") == 0)
            throw std::runtime_error("unsupported binary or UTF-16 GUID mapper");
        std::size_t start = parsed.rawText.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0, lineNumber = 0;
        std::set<std::string> sources, targets;
        while (start < parsed.rawText.size())
        {
            ++lineNumber;
            const auto end = parsed.rawText.find_first_of("\r\n", start);
            const auto line = parsed.rawText.substr(start, end == std::string::npos ? end : end - start);
            if (end == std::string::npos) start = parsed.rawText.size();
            else
            {
                start = end + 1;
                if (parsed.rawText[end] == '\r' && start < parsed.rawText.size() && parsed.rawText[start] == '\n') ++start;
            }
            if (line.find_first_not_of(" \t") == std::string::npos) continue;
            constexpr const char* prefix = "!Guid mapping for ";
            if (line.compare(0, 18, prefix) == 0)
            {
                const auto name = line.substr(18);
                if (name.find_first_not_of(" \t") == std::string::npos)
                    throw std::runtime_error("empty GUID mapping model name at line " + std::to_string(lineNumber));
                parsed.batches.push_back({lineNumber, name, {}});
                sources.clear(); targets.clear();
                continue;
            }
            std::istringstream input(line);
            std::string from, to, extra;
            input >> from >> to;
            from = canonical(from); to = canonical(to);
            if (parsed.batches.empty() || from.empty() || to.empty() || (input >> extra))
                throw std::runtime_error("invalid GUID mapping at line " + std::to_string(lineNumber));
            const bool newSource = sources.insert(from).second;
            const bool newTarget = targets.insert(to).second;
            if (!newSource || !newTarget)
                parsed.diagnostics.push_back("duplicate GUID mapping candidate retained at line " + std::to_string(lineNumber));
            parsed.batches.back().mappings.push_back({lineNumber, std::move(from), std::move(to)});
        }
        if (parsed.batches.empty()) throw std::runtime_error("missing GUID mapping batch header");
        result = std::move(parsed);
        return true;
    }
    catch (const std::exception& e) { error = e.what(); return false; }
}
std::vector<GuidMappingTarget> matchGuidMappingTargets(const GuidMappingFile& mappings,
                                                      const db1::Model& model)
{
    const auto ids = modelGuids(model);
    std::vector<GuidMappingTarget> result;
    for (std::size_t batch = 0; batch < mappings.batches.size(); ++batch)
        for (std::size_t index = 0; index < mappings.batches[batch].mappings.size(); ++index)
        {
            const auto found = ids.find(canonical(mappings.batches[batch].mappings[index].targetGuid));
            if (found != ids.end() && found->second.size() == 1)
                result.push_back({batch, index, found->second.front()});
        }
    return result;
}
std::vector<GuidMappedObjectPair> matchGuidMappingObjects(const GuidMappingFile& mappings,
    const db1::Model& sourceModel, const db1::Model& targetModel)
{
    const auto sourceIds = modelGuids(sourceModel);
    std::vector<GuidMappedObjectPair> result;
    for (const auto& target : matchGuidMappingTargets(mappings, targetModel))
    {
        const auto& mapping = mappings.batches[target.batchIndex].mappings[target.mappingIndex];
        const auto source = sourceIds.find(canonical(mapping.sourceGuid));
        if (source != sourceIds.end() && source->second.size() == 1)
            result.push_back({target.batchIndex, target.mappingIndex, source->second.front(), target.modelObjectId});
    }
    return result;
}
}
