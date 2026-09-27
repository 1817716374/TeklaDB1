#include <tekla/Environment.hpp>
#include "RelatedContainers.hpp"
#include <algorithm>
#include <cmath>
#include <set>

namespace tekla
{
namespace
{
using db1::detail::Bytes;
using db1::detail::u32;
std::string fixed(const Bytes& data,std::size_t offset,std::size_t width)
{
    if (offset>data.size() || width>data.size()-offset) throw std::runtime_error("truncated DBV string");
    const auto begin=data.begin()+static_cast<std::ptrdiff_t>(offset);
    return {begin,std::find(begin,begin+static_cast<std::ptrdiff_t>(width),0)};
}
std::int32_t integer(const Bytes& data,std::size_t offset)
{
    const auto bits=u32(data,offset); std::int32_t value;
    std::memcpy(&value,&bits,sizeof(value)); return value;
}
double real(const Bytes& data,std::size_t offset)
{
    const auto bits=std::uint64_t(u32(data,offset)) | (std::uint64_t(u32(data,offset+4))<<32);
    double value; std::memcpy(&value,&bits,sizeof(value));
    if (!std::isfinite(value)) throw std::runtime_error("non-finite DBV value");
    return value;
}
template<class Map,class T> void unique(Map& items,T value)
{
    const auto id=value.id;
    if (!id || !items.emplace(id,std::move(value)).second) throw std::runtime_error("zero or duplicate DBV ID");
}
void name(std::set<std::string>& names,const std::string& value)
{
    if (value.empty() || !names.insert(value).second) throw std::runtime_error("empty or duplicate DBV name");
}
void schema(const db1::RawDatabase& raw,const std::vector<unsigned>& widths,
            const std::vector<std::vector<std::uint32_t>>& fields)
{
    if (raw.kind!=db1::DatabaseKind::Environment || raw.layout!=db1::DatabaseLayout::ModernSections || raw.tables.size()!=widths.size())
        throw std::runtime_error("unsupported DBV layout");
    for (std::size_t i=0;i<widths.size();++i)
        if (!raw.tables[i].schemaValid || raw.tables[i].payloadSize!=widths[i] || raw.tables[i].fieldDescriptors!=fields[i])
            throw std::runtime_error("unsupported DBV table signature at "+std::to_string(i));
}
void header(const db1::RawDatabase& raw,bool environment)
{
    if (!raw.containerName.empty() && (environment ? raw.containerName!="Environment" :
        raw.containerName!="EnvModelOptions" && raw.containerName!="EnvDrawingOptions"))
        throw std::runtime_error("wrong DBV container name");
    const auto offset=raw.containerName.empty() ? 0 : 8+raw.containerName.size();
    const std::vector<std::uint32_t> expected=raw.containerName.empty()
        ? std::vector<std::uint32_t>{1,0,0,0,1,0xdbcec0bc}
        : std::vector<std::uint32_t>{1,environment ? 3U : 2U,0,0,0,1};
    if (raw.preamble.size()<offset+24) throw std::runtime_error("unsupported DBV preamble length");
    for (std::size_t i=0;i<expected.size();++i)
        if (u32(raw.preamble,offset+i*4)!=expected[i]) throw std::runtime_error("unsupported DBV header");
}
}

bool parseEnvironmentDatabase(const std::filesystem::path& path,EnvironmentDatabase& result,
                               std::string& error,const db1::RawDatabaseOptions& options)
{
    try
    {
        error.clear(); result={};
        if (!db1::parseRawDatabase(path,result.raw,error,options)) throw std::runtime_error(error);
        header(result.raw,true);
        std::vector<unsigned> widths{25,12,72,84,144,32,47,55,124};
        std::vector<std::vector<std::uint32_t>> fields{{1,1,0},{1,1,1,1},{1,1,0,0,0,0,0,1},
            {1,1,0,0,0,0,0,1},{1,1,0,0,0,1},{1,1,0,0,0,0,0,0,0},{1,1,1,0,0,0},{1,1,1,0,0,0},{1,1,1,0,0,0}};
        if (result.raw.tables.size()==15)
        {
            widths.insert(widths.end(),{104,116,172,77,85,154});
            for (auto i:{2,3,4,6,7,8}) fields.push_back(fields[i]);
        }
        schema(result.raw,widths,fields);
        const auto& tables=result.raw.tables;
        std::set<std::string> classNames,attributeNames;
        for (const auto& record:tables[0].records)
        {
            ObjectClassDefinition value; value.id=u32(record.payload,0); value.name=fixed(record.payload,4,21);
            name(classNames,value.name); unique(result.objectClasses,std::move(value));
        }
        for (const auto& record:tables[5].records)
        {
            AttributeMetadata value; value.id=u32(record.payload,0);
            for (std::size_t i=0;i<7;++i) value.fields[i]=u32(record.payload,4+i*4);
            unique(result.metadata,std::move(value));
        }
        for (auto t:{2U,3U,4U,9U,10U,11U})
        {
            if (t>=tables.size()) continue;
            for (const auto& record:tables[t].records)
            {
                const auto& row=record.payload;
                AttributeDefinition value; value.id=u32(row,0); value.name=fixed(row,4,21);
                value.label=fixed(row,25,t<9 ? 31 : 61); value.metadataId=u32(row,row.size()-4);
                if (!result.metadata.count(value.metadataId)) throw std::runtime_error("missing DBV attribute metadata");
                name(attributeNames,value.name);
                if (t==2 || t==9 || t==3 || t==10)
                {
                    const auto isInteger=t==2 || t==9;
                    value.storageKind=isInteger ? StoredValueKind::Integer : StoredValueKind::Real;
                    const std::size_t offset=t<9 ? 56 : 88;
                    for (std::size_t i=0;i<3;++i)
                    {
                        StoredValue field=isInteger ? StoredValue{integer(row,offset+i*4)} : StoredValue{real(row,offset+i*8)};
                        if (!i) value.storedValue=field; else value.additionalNumericFields.push_back(field);
                    }
                }
                unique(result.attributes,std::move(value));
            }
        }
        std::set<std::uint32_t> linkIds,choiceIds;
        std::set<std::pair<std::uint32_t,std::uint32_t>> links,indices;
        for (const auto& record:tables[1].records)
        {
            const auto& row=record.payload; const auto id=u32(row,0),classId=u32(row,4),attributeId=u32(row,8);
            if (!id || !linkIds.insert(id).second || !links.emplace(classId,attributeId).second) throw std::runtime_error("duplicate DBV class link");
            if (!result.objectClasses.count(classId) || !result.attributes.count(attributeId)) throw std::runtime_error("dangling DBV class link");
            result.objectClasses.at(classId).attributeIds.push_back(attributeId);
            result.attributes.at(attributeId).objectClassIds.push_back(classId);
        }
        for (auto t:{6U,7U,8U,12U,13U,14U})
        {
            if (t>=tables.size()) continue;
            if (t!=6 && t!=12)
            {
                if (!tables[t].records.empty()) result.diagnostics.push_back("DBV choice table "+std::to_string(t)+" retains raw records; non-integer choice layout unverified");
                continue;
            }
            for (const auto& record:tables[t].records)
            {
                const auto& row=record.payload; const auto attributeId=u32(row,4);
                AttributeChoice value{u32(row,0),u32(row,8),integer(row,12),fixed(row,16,t==6 ? 31 : 61)};
                if (!value.id || !choiceIds.insert(value.id).second || !indices.emplace(attributeId,value.index).second)
                    throw std::runtime_error("duplicate DBV attribute choice");
                const auto found=result.attributes.find(attributeId);
                if (found==result.attributes.end() || found->second.storageKind!=StoredValueKind::Integer)
                    throw std::runtime_error("missing or incompatible DBV choice attribute");
                found->second.choices.push_back(std::move(value));
            }
        }
        for (auto& entry:result.attributes)
            std::sort(entry.second.choices.begin(),entry.second.choices.end(),[](const auto& a,const auto& b){ return a.index<b.index; });
        result.diagnostics.emplace_back("partial DBV environment semantics: metadata flags, string definition values and additional numeric fields remain uninterpreted");
        return true;
    }
    catch (const std::exception& e) { result={}; error=e.what(); return false; }
}

bool parseOptionsDatabase(const std::filesystem::path& path,OptionsDatabase& result,
                          std::string& error,const db1::RawDatabaseOptions& options)
{
    try
    {
        error.clear(); result={};
        if (!db1::parseRawDatabase(path,result.raw,error,options)) throw std::runtime_error(error);
        header(result.raw,false);
        schema(result.raw,{86,98,114,2130,80,88,332,72,12},{{1,1,0,0,0,0,0,0,0,0},
            {1,1,0,0,0,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0,0,0,0,0},
            {1,1,0,0,0,0,0,0,0,0},{1,1,1,0,0,0},{1,1,1,0,0,0},{1,1,1,0,0,0},{1,1,0,1},{1,1,1,1}});
        std::set<std::string> names;
        for (std::size_t t=0;t<result.raw.tables.size();++t)
        {
            const auto& table=result.raw.tables[t];
            std::set<std::string> typedNames;
            if (t>3)
            {
                if (!table.records.empty()) result.diagnostics.push_back("DBV options table "+std::to_string(t)+" retains raw records; semantic mapping unavailable");
                continue;
            }
            for (const auto& record:table.records)
            {
                const auto& row=record.payload; StoredOption value;
                value.id=u32(row,0); value.name=fixed(row,4,64); value.flags=u32(row,68);
                value.kind=static_cast<StoredValueKind>(t); name(typedNames,value.name);
                if (!names.insert(value.name).second)
                    result.diagnostics.push_back("option name occurs in multiple storage types; preserve each ID without choosing precedence: "+value.name);
                for (std::size_t slot=0;slot<2;++slot)
                {
                    if (t==0)
                    {
                        if (row[72+slot]>1) throw std::runtime_error("invalid DBV boolean value");
                        value.valueSlots[slot]=row[72+slot]!=0;
                    }
                    if (t==1) value.valueSlots[slot]=integer(row,72+slot*4);
                    if (t==2) value.valueSlots[slot]=real(row,72+slot*8);
                    if (t==3) value.valueSlots[slot]=fixed(row,72+slot*1024,1024);
                }
                unique(result.options,std::move(value));
            }
        }
        result.diagnostics.emplace_back("partial DBV options semantics: paired slots retain stored order; effective/default precedence and flags are unverified");
        return true;
    }
    catch (const std::exception& e) { result={}; error=e.what(); return false; }
}
}
