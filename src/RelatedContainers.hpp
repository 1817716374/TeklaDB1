#pragma once
#include <tekla/db1/Database.hpp>
#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace tekla::db1::detail
{
using Bytes = std::vector<std::uint8_t>;
inline std::uint32_t u32(const Bytes& data, std::size_t offset)
{
    if (offset > data.size() || data.size() - offset < 4) throw std::runtime_error("truncated integer");
    return std::uint32_t(data[offset]) | (std::uint32_t(data[offset+1]) << 8) |
        (std::uint32_t(data[offset+2]) << 16) | (std::uint32_t(data[offset+3]) << 24);
}
inline bool guidText(const std::string& text)
{
    if (text.size() != 36) return false;
    for (std::size_t i=0; i<text.size(); ++i)
    {
        if (i==8 || i==13 || i==18 || i==23) { if (text[i]!='-') return false; }
        else if (!((text[i]>='0' && text[i]<='9') || (text[i]>='a' && text[i]<='f') || (text[i]>='A' && text[i]<='F'))) return false;
    }
    return true;
}
inline std::size_t relatedHeader(const Bytes& data, RawDatabase& raw, bool drawing)
{
    const char* prefix = drawing ? "Xsteel  " : "Xsteel\x80 ";
    if (data.size()<12 || std::memcmp(data.data(),prefix,8)!=0) throw std::runtime_error("unsupported related database header");
    std::size_t end=8;
    while (end<data.size() && end<24 && ((data[end]>='0' && data[end]<='9') || data[end]=='.')) ++end;
    raw.storageVersion.assign(data.begin()+8,data.begin()+static_cast<std::ptrdiff_t>(end));
    const auto dot=raw.storageVersion.find('.');
    if (dot==std::string::npos || dot==0 || dot+1==raw.storageVersion.size() || raw.storageVersion.find('.',dot+1)!=std::string::npos)
        throw std::runtime_error("invalid related database storage version");
    if (!drawing && end<data.size() && data[end]==' ')
    {
        ++end;
        if (data.size()-end<36) throw std::runtime_error("truncated numbering database GUID");
        raw.databaseGuid.assign(data.begin()+static_cast<std::ptrdiff_t>(end),data.begin()+static_cast<std::ptrdiff_t>(end+36));
        if (!guidText(raw.databaseGuid)) throw std::runtime_error("invalid numbering database GUID");
        end+=36;
    }
    raw.preamble.assign(data.begin(),data.begin()+static_cast<std::ptrdiff_t>(end));
    raw.kind=drawing ? DatabaseKind::Drawing : DatabaseKind::Numbering;
    raw.layout=DatabaseLayout::SequentialTables;
    return end;
}
inline RawTable relatedTable(const Bytes& data, std::size_t& pos, bool numbering, std::uint32_t ordinal)
{
    const auto headerSize=numbering ? 12U : 8U;
    if (pos>data.size() || data.size()-pos<headerSize) throw std::runtime_error("truncated related database table header");
    RawTable table; table.fileOffset=pos; table.schemaValid=true;
    table.ordinal=numbering ? u32(data,pos) : ordinal;
    const auto count=u32(data,pos+(numbering ? 4 : 0));
    table.payloadSize=u32(data,pos+(numbering ? 8 : 4));
    pos+=headerSize;
    const auto stride=std::uint64_t(table.payloadSize)+1;
    const auto rowBytes=std::uint64_t(count)*stride;
    if (rowBytes>data.size()-pos) throw std::runtime_error("related database rows exceed input");
    // Empty tables need not allocate their declared payload size.
    table.records.reserve(count);
    for (std::uint32_t i=0; i<count; ++i)
    {
        // DB2 7.82 public numbering rows also carry tag 5. Preserve the tag;
        // do not interpret its low flag bit as an object/deletion status.
        if (data[pos]!=4 && data[pos]!=12 && !(numbering && data[pos]==5)) throw std::runtime_error("invalid related database allocation tag");
        RawRecord record; record.fileOffset=pos; record.allocationTag=data[pos];
        record.payload.assign(data.begin()+static_cast<std::ptrdiff_t>(pos+1),data.begin()+static_cast<std::ptrdiff_t>(pos+stride));
        table.records.push_back(std::move(record)); pos+=static_cast<std::size_t>(stride);
    }
    if (numbering)
    {
        if (data.size()-pos<4 || u32(data,pos)!=0x00bc614f) throw std::runtime_error("missing numbering table terminator");
        table.trailer.assign(data.begin()+static_cast<std::ptrdiff_t>(pos),data.begin()+static_cast<std::ptrdiff_t>(pos+4)); pos+=4;
    }
    return table;
}
inline void numberingContainer(const Bytes& data, RawDatabase& raw)
{
    auto pos=relatedHeader(data,raw,false);
    std::set<std::uint32_t> ids;
    while (pos<data.size())
    {
        auto table=relatedTable(data,pos,true,0);
        if (!ids.insert(table.ordinal).second) throw std::runtime_error("duplicate numbering table ID");
        raw.tables.push_back(std::move(table));
    }
}
inline void drawing782Container(const Bytes& data, RawDatabase& raw)
{
    // 293 pinned files: fixed preamble, modern sections, two 20-byte metadata
    // slots per record and a one-byte table terminator. Walk rows, never scan
    // for magic inside text/geometry/metadata.
    if (data.size()<76 || std::memcmp(data.data(),"Xsteel! 7.82",12)!=0 ||
        u32(data,12)!=1 || u32(data,16)!=0xdbcec0bc)
        throw std::runtime_error("unsupported legacy drawing preamble");
    raw.kind=DatabaseKind::Drawing; raw.layout=DatabaseLayout::ModernSections; raw.storageVersion="7.82";
    raw.preamble.assign(data.begin(),data.begin()+76);
    std::size_t pos=76;
    while (pos<data.size())
    {
        if (data.size()-pos<12 || u32(data,pos)!=0xdbcec066) throw std::runtime_error("invalid legacy drawing section header");
        RawTable t; t.fileOffset=pos; t.schemaValid=true; t.payloadSize=u32(data,pos+4);
        const auto fields=u32(data,pos+8); pos+=12;
        if (fields>(data.size()-pos)/4) throw std::runtime_error("truncated legacy drawing field descriptors");
        for (std::uint32_t i=0;i<fields;++i) { t.fieldDescriptors.push_back(u32(data,pos)); pos+=4; }
        const auto stride=std::uint64_t(t.payloadSize)+41;
        while (pos<data.size() && (data[pos]==4 || data[pos]==12))
        {
            if (stride>data.size()-pos) throw std::runtime_error("truncated legacy drawing record");
            RawRecord r; r.fileOffset=pos; r.allocationTag=data[pos];
            const auto begin=data.begin()+static_cast<std::ptrdiff_t>(pos+1);
            r.payload.assign(begin,begin+t.payloadSize);
            r.allocatorMetadata.assign(begin+t.payloadSize,begin+t.payloadSize+40);
            t.records.push_back(std::move(r)); pos+=static_cast<std::size_t>(stride);
        }
        if (pos==data.size() || data[pos]!=0) throw std::runtime_error("missing legacy drawing table terminator");
        t.trailer={data[pos++]}; raw.tables.push_back(std::move(t));
    }
    if (raw.tables.empty() || raw.tables.front().payloadSize!=4 || raw.tables.front().records.empty())
        throw std::runtime_error("invalid legacy drawing directory");
    std::set<std::uint32_t> seen; std::vector<std::uint32_t> types;
    for (const auto& r:raw.tables.front().records)
    {
        const auto type=u32(r.payload,0);
        if (!type) throw std::runtime_error("zero legacy drawing table type");
        if (seen.insert(type).second) types.push_back(type);
    }
    // Repeated directory entries are retained, not interpreted as duplicate
    // tables. First occurrences list the physical table roles in reverse order.
    if (types.size()+1!=raw.tables.size()) throw std::runtime_error("legacy drawing directory/table count mismatch");
    for (std::size_t i=0;i<types.size();++i) raw.tables[i+1].ordinal=types[types.size()-1-i];
}
inline void drawingContainer(const Bytes& data, RawDatabase& raw)
{
    if (data.size()>=8 && std::memcmp(data.data(),"Xsteel! ",8)==0)
    {
        drawing782Container(data,raw); return;
    }
    auto pos=relatedHeader(data,raw,true);
    auto types=relatedTable(data,pos,false,0);
    if (types.payloadSize!=4 || types.records.empty()) throw std::runtime_error("invalid drawing table directory");
    std::set<std::uint32_t> ids;
    for (const auto& record:types.records)
        if (!ids.insert(u32(record.payload,0)).second) throw std::runtime_error("duplicate drawing table type");
    const auto count=types.records.size();
    raw.tables.push_back(std::move(types));
    // The type directory is serialized in reverse order of the following tables.
    for (std::size_t i=0;i<count;++i)
    {
        const auto type=u32(raw.tables.front().records[count-1-i].payload,0);
        auto table=relatedTable(data,pos,false,type);
        raw.tables.push_back(std::move(table));
    }
    if (pos!=data.size()) throw std::runtime_error("unexpected trailing drawing data");
}
}
