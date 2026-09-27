#pragma once
#include "RelatedContainers.hpp"

namespace tekla::db1::detail
{
// Observed AnalysisPartDefaults.db6 framing. All prefix data stays opaque:
// neither allocation contexts nor numeric IDs imply a model-object mapping.
inline void analysisContainer(const Bytes& data, RawDatabase& raw)
{
    if (data.size()<32 || u32(data,0)!=10014 || u32(data,4)!=1 ||
        u32(data,8)!=0xdbcec0bc || u32(data,12)!=0 || u32(data,16)!=1 || u32(data,20)!=13)
        throw std::runtime_error("unsupported DB6 analysis header");
    const auto contexts=u32(data,24);
    if (!contexts) throw std::runtime_error("invalid DB6 context count");
    const auto registryBytes=std::uint64_t(contexts-1)*24;
    if (registryBytes>data.size()-32) throw std::runtime_error("truncated DB6 context registry");
    std::size_t pos=32;
    for (std::uint32_t i=1;i<contexts;++i,pos+=24)
        if (u32(data,pos)!=i) throw std::runtime_error("invalid DB6 context ordinal");
    if (data.size()-pos<8 || u32(data,pos)!=5)
        throw std::runtime_error("unsupported DB6 allocation prefix");
    const auto entries=u32(data,pos+4);
    pos+=8;
    const auto arrayBytes=std::uint64_t(entries)*8;
    if (data.size()-pos<20 || arrayBytes>data.size()-pos-20)
        throw std::runtime_error("truncated DB6 allocation arrays");
    pos+=static_cast<std::size_t>(arrayBytes);
    if (u32(data,pos)!=contexts) throw std::runtime_error("inconsistent DB6 context count");
    pos+=20;
    raw.preamble.assign(data.begin(),data.begin()+static_cast<std::ptrdiff_t>(pos));
    raw.kind=DatabaseKind::Analysis;
    raw.layout=DatabaseLayout::ModernSections;
    raw.storageVersion="DB6-10014";
    while (pos<data.size())
    {
        if (data.size()-pos<12 || u32(data,pos)!=0xdbcec066)
            throw std::runtime_error("invalid DB6 table header");
        RawTable table; table.fileOffset=pos;
        table.ordinal=static_cast<std::uint32_t>(raw.tables.size());
        table.payloadSize=u32(data,pos+4);
        const auto fields=u32(data,pos+8);
        pos+=12;
        if (!table.payloadSize || !fields || fields>(data.size()-pos)/4)
            throw std::runtime_error("invalid DB6 table dimensions");
        for (std::uint32_t i=0;i<fields;++i,pos+=4)
        {
            const auto descriptor=u32(data,pos);
            if (descriptor>1) throw std::runtime_error("unsupported DB6 field descriptor");
            table.fieldDescriptors.push_back(descriptor);
        }
        const auto stride=std::uint64_t(table.payloadSize)+9;
        while (pos<data.size() && data[pos]!=0)
        {
            // Only tag 4 has native evidence in this layout. Do not silently
            // borrow other allocation tags from DB1 or drawing databases.
            if (data[pos]!=4) throw std::runtime_error("unsupported DB6 allocation tag");
            if (stride>data.size()-pos) throw std::runtime_error("truncated DB6 record");
            RawRecord row; row.fileOffset=pos; row.allocationTag=data[pos];
            const auto begin=data.begin()+static_cast<std::ptrdiff_t>(pos+1);
            row.payload.assign(begin,begin+table.payloadSize);
            row.allocatorMetadata.assign(begin+table.payloadSize,begin+table.payloadSize+8);
            table.records.push_back(std::move(row));
            pos+=static_cast<std::size_t>(stride);
        }
        if (pos==data.size()) throw std::runtime_error("missing DB6 table terminator");
        table.trailer.push_back(data[pos++]); table.schemaValid=true;
        raw.tables.push_back(std::move(table));
    }
    if (raw.tables.empty()) throw std::runtime_error("DB6 contains no tables");
    raw.diagnostics.emplace_back("DB6 raw container only; analysis field semantics and DB1 object associations are unverified");
}
}
