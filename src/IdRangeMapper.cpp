#include <tekla/IdRangeMapper.hpp>
#include "BinaryIO.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace tekla
{
namespace
{
std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}
}

bool parseIdRangeMapperFile(const std::filesystem::path& path, IdRangeMapperFile& result,
                            std::string& error, std::size_t maxDecodedBytes)
{
    result = {};
    error.clear();
    try
    {
        constexpr char signature[] = "IdRangeMapper";
        constexpr std::uint32_t expectedMarker = 0xdbcec0e1U;
        constexpr std::size_t headerSize = 25;
        constexpr std::size_t recordSize = 28;
        const auto bytes = db1::detail::readPayload(path, maxDecodedBytes);
        if (bytes.size() < headerSize)
            throw std::runtime_error("truncated IDRM header");
        if (!std::equal(signature, signature + sizeof(signature) - 1, bytes.begin()))
            throw std::runtime_error("invalid IDRM signature");
        const auto marker = readU32(bytes, 17);
        if (marker != expectedMarker)
            throw std::runtime_error("unsupported IDRM marker");
        const auto count = readU32(bytes, 21);
        const auto available = bytes.size() - headerSize;
        if (count > available / recordSize)
            throw std::runtime_error("truncated IDRM record array");

        IdRangeMapperFile parsed;
        parsed.sourcePath = path;
        parsed.rawBytes = bytes;
        parsed.headerValueAt13 = readU32(bytes, 13);
        parsed.marker = marker;
        parsed.staticIdBoundary = parsed.headerValueAt13;
        parsed.records.reserve(count);
        std::size_t offset = headerSize;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            IdRangeMapperRecord record;
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset), recordSize, record.raw.begin());
            record.staticFirstId = readU32(bytes, offset);
            std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset + 4),
                        record.namespaceGuidBytes.size(), record.namespaceGuidBytes.begin());
            record.namespaceFirstId = readU32(bytes, offset + 20);
            record.idCount = readU32(bytes, offset + 24);
            parsed.records.push_back(record);
            offset += recordSize;
        }
        if (!parsed.records.empty())
        {
            std::uint64_t expectedFirst = parsed.records.front().staticFirstId;
            parsed.staticRangesMatchBoundary = true;
            for (const auto& record : parsed.records)
            {
                if (expectedFirst > std::numeric_limits<std::uint32_t>::max() ||
                    record.staticFirstId != expectedFirst)
                {
                    parsed.staticRangesMatchBoundary = false;
                    break;
                }
                expectedFirst = static_cast<std::uint64_t>(record.staticFirstId) + record.idCount;
            }
            if (expectedFirst != parsed.staticIdBoundary)
                parsed.staticRangesMatchBoundary = false;
        }
        if (!parsed.staticRangesMatchBoundary)
            parsed.diagnostics.push_back("static ID ranges are not a contiguous sequence ending at the header boundary");
        parsed.trailingBytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset), bytes.end());
        if (!parsed.trailingBytes.empty())
            parsed.diagnostics.push_back("unclassified bytes follow the declared IDRM record array");
        result = std::move(parsed);
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}
}
