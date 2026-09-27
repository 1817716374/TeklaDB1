#pragma once
#include "Path.hpp"
#include <zlib.h>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace tekla::db1::detail
{
constexpr std::size_t defaultDecodedLimit = 1024ULL * 1024 * 1024;

inline std::vector<std::uint8_t> readFile(const std::filesystem::path& path,
                                         std::size_t limit = defaultDecodedLimit)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + pathUtf8(path));
    input.seekg(0, std::ios::end);
    const auto size = input.tellg();
    if (size < 0) throw std::runtime_error("cannot determine file size: " + pathUtf8(path));
    if (static_cast<std::uintmax_t>(size) > limit)
        throw std::runtime_error("decoded file exceeds byte limit: " + pathUtf8(path));
    std::vector<std::uint8_t> result(static_cast<std::size_t>(size));
    input.seekg(0);
    if (!result.empty() && !input.read(reinterpret_cast<char*>(result.data()), size))
        throw std::runtime_error("cannot read " + pathUtf8(path));
    return result;
}

inline std::vector<std::uint8_t> readPayload(const std::filesystem::path& path,
                                            std::size_t limit = defaultDecodedLimit)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + pathUtf8(path));
    std::array<unsigned char, 2> magic{};
    input.read(reinterpret_cast<char*>(magic.data()), magic.size());
    if (input.gcount() != 2 || magic[0] != 0x1f || magic[1] != 0x8b)
        return readFile(path, limit);
    input.clear(); input.seekg(0);
    z_stream stream{};
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK)
        throw std::runtime_error("cannot initialize gzip decoder");
    struct Guard { z_stream* stream; ~Guard() { inflateEnd(stream); } } guard{&stream};
    std::array<unsigned char, 256 * 1024> compressed{}, decoded{};
    std::vector<std::uint8_t> result;
    bool memberComplete = false;
    for (;;)
    {
        if (stream.avail_in == 0)
        {
            input.read(reinterpret_cast<char*>(compressed.data()), compressed.size());
            stream.avail_in = static_cast<uInt>(input.gcount());
            stream.next_in = compressed.data();
            if (input.bad()) throw std::runtime_error("cannot read " + pathUtf8(path));
            if (stream.avail_in == 0)
            {
                if (memberComplete) return result;
                // inflate may still have buffered output after consuming the
                // final input byte. Let it drain before declaring truncation.
            }
        }
        if (memberComplete)
        {
            // GZIP permits concatenated members. Validate every member including
            // CRC/trailer instead of silently dropping bytes after the first one.
            if (inflateReset2(&stream, 16 + MAX_WBITS) != Z_OK)
                throw std::runtime_error("cannot reset gzip decoder");
            memberComplete = false;
        }
        stream.next_out = decoded.data(); stream.avail_out = static_cast<uInt>(decoded.size());
        const auto inputBefore = stream.avail_in;
        const auto status = inflate(&stream, Z_NO_FLUSH);
        const auto produced = decoded.size() - stream.avail_out;
        if (produced > limit - result.size())
            throw std::runtime_error("decoded file exceeds byte limit: " + pathUtf8(path));
        result.insert(result.end(), decoded.begin(), decoded.begin() + static_cast<std::ptrdiff_t>(produced));
        if (status == Z_STREAM_END) memberComplete = true;
        else if (status != Z_OK || (inputBefore == stream.avail_in && produced == 0))
            throw std::runtime_error("invalid gzip stream: " + pathUtf8(path));
    }
}
}
