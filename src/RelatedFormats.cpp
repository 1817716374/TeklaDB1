#include <tekla/Drawing.hpp>
#include <tekla/Numbering.hpp>
#include "BinaryIO.hpp"
#include "RelatedContainers.hpp"
#include "XmlPrivate.hpp"
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <iomanip>
#include <sstream>

namespace tekla
{
namespace
{
using db1::detail::u32;
using db1::detail::Bytes;
std::string fixed(const Bytes& data,std::size_t offset,std::size_t width)
{
    if (offset>data.size() || width>data.size()-offset) throw std::runtime_error("truncated fixed string");
    const auto begin=data.begin()+static_cast<std::ptrdiff_t>(offset);
    const auto end=std::find(begin,begin+static_cast<std::ptrdiff_t>(width),0);
    return {begin,end};
}
double real(const Bytes& data,std::size_t offset)
{
    const auto bits=std::uint64_t(u32(data,offset)) | (std::uint64_t(u32(data,offset+4))<<32);
    double value; std::memcpy(&value,&bits,sizeof(value));
    if (!std::isfinite(value)) throw std::runtime_error("non-finite drawing numeric value");
    return value;
}
DrawingMarkNode drawingMarkNode(const db1::detail::xmlsupport::xml::XMLElement& element)
{
    using namespace db1::detail::xmlsupport;
    DrawingMarkNode node;
    node.name=element.Name();
    for (auto* attribute=element.FirstAttribute();attribute;attribute=attribute->Next())
        node.attributes.emplace(attribute->Name(),decode(attribute->Value()));
    for (auto* child=element.FirstChild();child;child=child->NextSibling())
    {
        if (const auto* text=child->ToText())
            node.text+=text->CData()?text->Value():decode(text->Value());
        else if (const auto* nested=child->ToElement())
            node.children.push_back(drawingMarkNode(*nested));
    }
    return node;
}
std::optional<DrawingMarkTemplate> drawingMarkTemplate(const std::string& source)
{
    using namespace db1::detail::xmlsupport;
    validateUtf8(source);
    xml::XMLDocument document(false,xml::PEDANTIC_WHITESPACE);
    if (document.Parse(source.data(),source.size())!=xml::XML_SUCCESS) return std::nullopt;
    const auto* root=document.RootElement();
    if (!root || std::string(root->Name())!="Mark") return std::nullopt;
    validateNodes(*root);
    DrawingMarkTemplate result;
    for (auto* child=root->FirstChildElement();child;child=child->NextSiblingElement())
        result.children.push_back(drawingMarkNode(*child));
    return result;
}
std::string binaryGuid(const Bytes& data,std::size_t offset)
{
    if (offset>data.size() || data.size()-offset<16) throw std::runtime_error("truncated drawing GUID");
    std::ostringstream out; out<<std::hex<<std::setfill('0');
    for (std::size_t i=0;i<16;++i)
    {
        if (i==4 || i==6 || i==8 || i==10) out<<'-';
        out<<std::setw(2)<<static_cast<unsigned>(data[offset+i]);
    }
    return out.str();
}
DrawingCoordinateSystem coordinates(const Bytes& row,std::size_t offset)
{
    DrawingCoordinateSystem value;
    for (std::size_t i=0;i<3;++i)
    {
        value.origin[i]=real(row,offset+i*8);
        value.axisX[i]=real(row,offset+24+i*8)-value.origin[i];
        value.axisY[i]=real(row,offset+48+i*8)-value.origin[i];
    }
    const auto dot=[](const auto& a,const auto& b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; };
    const double xx=dot(value.axisX,value.axisX),yy=dot(value.axisY,value.axisY),xy=dot(value.axisX,value.axisY);
    if (!std::isfinite(xx) || !std::isfinite(yy) || !std::isfinite(xy) ||
        std::abs(xx-1)>1e-6 || std::abs(yy-1)>1e-6 || std::abs(xy)>1e-6)
        throw std::runtime_error("unsupported or degenerate drawing coordinate basis");
    for (std::size_t i=0;i<3;++i)
        value.axisZ[i]=value.axisX[(i+1)%3]*value.axisY[(i+2)%3]-value.axisX[(i+2)%3]*value.axisY[(i+1)%3];
    return value;
}
DrawingViewVolume volume(const Bytes& row,std::size_t offset)
{
    DrawingViewVolume value{real(row,offset),real(row,offset+8),real(row,offset+16),
        real(row,offset+24),real(row,offset+32),real(row,offset+40)};
    if (value.minX>value.maxX || value.minY>value.maxY)
        throw std::runtime_error("invalid drawing view volume");
    return value;
}
bool collapsedCoordinates(const Bytes& row,std::size_t offset)
{
    bool collapsed=true;
    for (std::size_t i=0;i<3;++i)
    {
        const auto origin=real(row,offset+i*8);
        const auto x=real(row,offset+24+i*8), y=real(row,offset+48+i*8);
        collapsed=collapsed && origin==x && origin==y;
    }
    return collapsed;
}
const db1::RawTable& table(const db1::RawDatabase& raw,std::uint32_t type)
{
    for (const auto& item:raw.tables) if (item.ordinal==type) return item;
    throw std::runtime_error("missing drawing table type "+std::to_string(type));
}
template<class Map,class T> void unique(Map& items,std::uint32_t id,T&& value)
{
    if (!id || !items.emplace(id,std::forward<T>(value)).second) throw std::runtime_error("zero or duplicate related object ID");
}
void parse844Dimensions(Drawing& result)
{
    std::map<std::uint32_t,std::size_t> setCounts,dimensionCounts;
    for (const auto& record:table(result.raw,257).records) ++setCounts[u32(record.payload,4)];
    for (const auto& record:table(result.raw,256).records) ++dimensionCounts[u32(record.payload,4)];
    for (const auto& record:table(result.raw,257).records)
    {
        const auto& row=record.payload;
        if (u32(row,0)!=257) throw std::runtime_error("invalid 8.44 straight dimension set type");
        DrawingStraightDimensionSet set; set.recordId=u32(row,4); set.contextId=u32(row,8);
        if (!set.recordId) throw std::runtime_error("zero 8.44 straight dimension set ID");
        if (setCounts[set.recordId]>1)
        {
            result.unhandledDimensionSetRecordIds.push_back(set.recordId);
            result.diagnostics.push_back("duplicate 8.44 straight dimension set ID retained raw: "+std::to_string(set.recordId));
            continue;
        }
        if (!result.viewsByContext.count(set.contextId))
        {
            result.unhandledDimensionSetRecordIds.push_back(set.recordId);
            result.diagnostics.push_back("8.44 straight dimension set has no unique decoded view context; retained raw: "+std::to_string(set.recordId));
            continue;
        }
        unique(result.straightDimensionSets,set.recordId,std::move(set));
    }
    for (const auto& record:table(result.raw,256).records)
    {
        const auto& row=record.payload;
        if (u32(row,0)!=256) throw std::runtime_error("invalid 8.44 straight dimension type");
        DrawingStraightDimension dimension;
        dimension.recordId=u32(row,4); dimension.contextId=u32(row,8);
        dimension.dimensionSetId=u32(row,12); dimension.subtypeCode=u32(row,16);
        if (!dimension.recordId) throw std::runtime_error("zero 8.44 straight dimension ID");
        if (dimensionCounts[dimension.recordId]>1)
        {
            result.unhandledDimensionRecordIds.push_back(dimension.recordId);
            result.diagnostics.push_back("duplicate 8.44 straight dimension ID retained raw: "+std::to_string(dimension.recordId));
            continue;
        }
        const auto set=result.straightDimensionSets.find(dimension.dimensionSetId);
        if (set==result.straightDimensionSets.end())
            result.diagnostics.push_back("8.44 straight dimension "+std::to_string(dimension.recordId)+
                                         " references missing or ambiguous set ID "+std::to_string(dimension.dimensionSetId));
        else if (dimension.contextId!=set->second.contextId)
            result.diagnostics.push_back("8.44 straight dimension "+std::to_string(dimension.recordId)+" context "+
                                         std::to_string(dimension.contextId)+" differs from set "+
                                         std::to_string(dimension.dimensionSetId)+" context "+std::to_string(set->second.contextId));
        else set->second.dimensionIds.push_back(dimension.recordId);
        if (dimension.subtypeCode!=1)
        {
            result.unhandledDimensionRecordIds.push_back(dimension.recordId);
            result.diagnostics.push_back("unknown 8.44 straight dimension subtype retained raw: "+std::to_string(dimension.recordId));
            continue;
        }
        for (std::size_t i=0;i<3;++i)
        {
            dimension.startPoint[i]=real(row,24+i*8);
            dimension.endPoint[i]=real(row,48+i*8);
            dimension.upDirection[i]=real(row,168+i*8);
        }
        dimension.distance=real(row,192);
        const auto& up=dimension.upDirection;
        const double norm=std::hypot(up[0],up[1]);
        if (std::abs(norm-1)<=1e-8 && std::abs(up[2])<=1e-8 &&
            std::abs(dimension.endPoint[2]-dimension.startPoint[2])<=1e-8)
        {
            const double projected=std::abs((dimension.endPoint[0]-dimension.startPoint[0])*up[1]-
                                             (dimension.endPoint[1]-dimension.startPoint[1])*up[0]);
            if (!std::isfinite(projected)) throw std::runtime_error("8.44 straight dimension projection overflow");
            dimension.projectedLength=projected;
        }
        else result.diagnostics.push_back("8.44 straight dimension projection requires a unit XY direction and coplanar endpoints: "+std::to_string(dimension.recordId));
        result.straightDimensions.emplace(dimension.recordId,std::move(dimension));
    }
}
#include "DrawingExamSchema.hpp"
}

bool parseNumberingDatabase(const std::filesystem::path& path,NumberingDatabase& result,
                            std::string& error,const db1::RawDatabaseOptions& options)
{
    try
    {
        error.clear(); result={}; result.raw.sourcePath=path;
        auto data=db1::detail::readPayload(path,options.maxDecodedBytes);
        db1::detail::numberingContainer(data,result.raw);
        const auto& version=result.raw.storageVersion;
        const bool legacy = version=="7.30" || version=="7.64" || version=="7.82";
        const bool emptySnapshot = version=="8.30" || version=="8.37" || version=="8.44" || version=="8.74";
        if (version=="7.30" && (result.raw.preamble.size()!=12 || result.raw.preamble[6]!=' '))
            throw std::runtime_error("unsupported 7.30 numbering preamble");
        if (version=="7.64" && (result.raw.preamble.size()!=12 || result.raw.preamble[6]!=0x80 || !result.raw.databaseGuid.empty()))
            throw std::runtime_error("unsupported 7.64 numbering preamble");
        if (!legacy && !emptySnapshot && version!="8.95" && version!="9.08" && version!="9.50" && version!="9.52" && version!="9.60")
            throw std::runtime_error("unsupported numbering semantic version "+version);
        if (!legacy && version!="8.30" && result.raw.databaseGuid.empty()) throw std::runtime_error("numbering database GUID missing");
        if (version=="8.30" && (result.raw.preamble.size()!=12 || !result.raw.databaseGuid.empty()))
            throw std::runtime_error("unsupported 8.30 numbering preamble");
        if ((version=="8.37" || version=="8.74") &&
            (result.raw.preamble.size()!=49 || result.raw.databaseGuid.empty()))
            throw std::runtime_error("unsupported empty numbering snapshot preamble");
        if (emptySnapshot)
        {
            static constexpr std::array<std::pair<std::uint32_t,std::uint32_t>,10> signature{{
                {10000040,184}, {10000038,36}, {10000043,42}, {10000042,40}, {21,260},
                {10000068,64}, {10000072,68}, {10000077,72}, {10000145,76}, {22,80}}};
            const bool headerOnlyComponentSnapshot=(version=="8.37" || version=="8.74") && result.raw.tables.empty();
            if (!headerOnlyComponentSnapshot)
            {
                if (result.raw.tables.size()!=signature.size())
                    throw std::runtime_error("unsupported empty numbering snapshot table count");
                for (std::size_t i=0;i<signature.size();++i)
                    if (result.raw.tables[i].ordinal!=signature[i].first ||
                        result.raw.tables[i].payloadSize!=signature[i].second ||
                        !result.raw.tables[i].records.empty())
                        throw std::runtime_error("unsupported nonempty or mismatched numbering snapshot table at index "+std::to_string(i));
            }
        }
        std::set<std::string> keys;
        for (const auto& item:result.raw.tables)
        {
            if (emptySnapshot && !item.records.empty())
                throw std::runtime_error("nonempty "+version+" numbering tables remain raw; semantic mapping unavailable");
            if (item.ordinal!=22)
            {
                if (!item.records.empty()) result.diagnostics.push_back("numbering table "+std::to_string(item.ordinal)+" retains raw records; semantic mapping unavailable");
                continue;
            }
            if (item.payloadSize!=(legacy ? 76U : 80U)) throw std::runtime_error("unsupported numbering series record size");
            for (const auto& record:item.records)
            {
                const auto& row=record.payload;
                NumberingSeries series; series.key=fixed(row,0,52);
                const auto slash=series.key.rfind('/');
                if (slash==std::string::npos || slash+1==series.key.size()) throw std::runtime_error("invalid numbering series key");
                series.prefix=series.key.substr(0,slash);
                std::uint64_t start=0;
                for (std::size_t i=slash+1;i<series.key.size();++i)
                {
                    const auto c=series.key[i];
                    if (c<'0' || c>'9') throw std::runtime_error("invalid numbering series start number");
                    start=start*10+static_cast<unsigned>(c-'0');
                    if (start>std::numeric_limits<std::uint32_t>::max()) throw std::runtime_error("numbering series start overflow");
                }
                series.startNumber=static_cast<std::uint32_t>(start);
                series.partCounter=u32(row,52); series.assemblyCounter=u32(row,56);
                for (std::size_t i=60;i<row.size();i+=4) series.additionalFields.push_back(u32(row,i));
                if (!keys.insert(series.key).second) throw std::runtime_error("duplicate numbering series key");
                result.series.push_back(std::move(series));
            }
        }
        if (emptySnapshot)
            result.diagnostics.emplace_back(version+" empty numbering snapshot accepted; nonempty table semantics remain raw and no object assignments are inferred");
        else
            result.diagnostics.emplace_back("partial numbering semantics: counters are not object-position assignments; additional fields and snapshots remain raw");
        if (options.retainDecompressedFileImage) result.raw.decompressedFileImage=std::move(data);
        return true;
    }
    catch (const std::exception& e) { result={}; error=e.what(); return false; }
}

bool parseDrawing(const std::filesystem::path& path,Drawing& result,std::string& error,
                  const db1::RawDatabaseOptions& options)
{
    try
    {
        error.clear(); result={}; result.raw.sourcePath=path;
        auto data=db1::detail::readPayload(path,options.maxDecodedBytes);
        db1::detail::drawingContainer(data,result.raw);
        const bool older713=result.raw.storageVersion=="7.13";
        const bool older764=result.raw.storageVersion=="7.64";
        const bool older782=result.raw.storageVersion=="7.82";
        const bool older730=result.raw.storageVersion=="7.30";
        const bool older844=result.raw.storageVersion=="8.44";
        const bool older950=result.raw.storageVersion=="9.50";
        const bool exam895=result.raw.storageVersion=="8.95",exam908=result.raw.storageVersion=="9.08";
        const bool examDrawing=exam895 || exam908;
        const bool numericIdentity=older730 || older764 || older782 || examDrawing;
        if (!numericIdentity && !older713 && !older844 && !older950 && result.raw.storageVersion!="9.54") throw std::runtime_error("unsupported drawing semantic version "+result.raw.storageVersion);
        // Complete observed directory signature; unknown layouts stay available
        // through parseRawDatabase, never guessed into this semantic mapping.
        constexpr std::array<std::uint32_t,47> types{{253,254,256,257,259,260,263,264,266,268,269,273,275,277,278,279,280,281,293,295,296,297,298,301,302,303,304,305,306,307,308,309,310,311,312,313,314,315,316,317,318,319,320,321,322,323,324}};
        constexpr std::array<std::uint32_t,47> widths{{128,620,1496,144,712,4788,580,584,52,32,6196,964,240,608,296,496,144,32,45,12,37,12,110,3438,296,876,24,64,28,72,48,32,64,120,24,64,40,56,164,24,180,112,20,60,144,152,60}};
        if (older713)
        {
            constexpr std::array<std::array<unsigned,2>,33> signature{{
                {{253,128}},{{254,584}},{{256,1228}},{{257,136}},{{259,524}},{{260,8240}},
                {{264,536}},{{266,52}},{{268,32}},{{269,8640}},{{273,948}},{{275,224}},
                {{277,704}},{{278,296}},{{279,496}},{{280,144}},{{281,16}},{{293,45}},
                {{295,12}},{{296,37}},{{297,12}},{{298,110}},{{301,3438}},{{302,240}},
                {{303,860}},{{304,24}},{{305,64}},{{306,16}},{{307,36}},{{308,40}},
                {{309,20}},{{310,40}},{{311,120}}
            }};
            if(result.raw.tables.size()!=signature.size()+1 || result.raw.tables.front().ordinal!=0)
                throw std::runtime_error("unsupported 7.13 drawing table count");
            for(std::size_t i=0;i<signature.size();++i)
                if(result.raw.tables[i+1].ordinal!=signature[i][0] || result.raw.tables[i+1].payloadSize!=signature[i][1])
                    throw std::runtime_error("unsupported 7.13 drawing table signature");
        }
        else if (examDrawing) requireExamDrawingSchema(result.raw,exam908);
        else if (older764)
        {
            // Exact 7.64 directory signature observed in the RABAS project
            // (46 physical tables plus the directory table). The 254/277
            // widths differ from 7.82, so those layouts are never guessed.
            constexpr std::array<std::array<unsigned,3>,47> signature{{
                {{0,4,2}},{{253,128,20}},{{254,592,62}},{{256,1400,185}},{{257,136,22}},{{259,524,75}},
                {{260,3328,451}},{{264,536,76}},{{266,52,11}},{{268,32,6}},{{269,4888,654}},
                {{273,948,162}},{{275,224,35}},{{277,704,78}},{{278,296,48}},{{279,496,21}},
                {{280,144,21}},{{281,20,6}},{{293,45,6}},{{295,12,4}},{{296,37,5}},{{297,12,4}},
                {{298,110,5}},{{301,3438,50}},{{302,288,47}},{{303,860,143}},{{304,24,7}},
                {{305,64,12}},{{306,16,5}},{{307,36,10}},{{308,40,9}},{{309,20,6}},{{310,40,9}},
                {{311,120,21}},{{312,12,4}},{{313,64,10}},{{314,32,7}},{{315,56,9}},{{316,156,24}},
                {{317,24,7}},{{318,60,16}},{{319,112,17}},{{320,20,6}},{{321,60,16}},
                {{322,112,20}},{{323,144,23}},{{324,60,11}}
            }};
            if (result.raw.tables.size()!=signature.size()) throw std::runtime_error("unsupported 7.64 drawing table count");
            for (std::size_t i=0;i<signature.size();++i)
            {
                const auto& t=result.raw.tables[i]; const auto& s=signature[i];
                if (t.ordinal!=s[0] || t.payloadSize!=s[1] || t.fieldDescriptors.size()!=s[2] ||
                    t.fieldDescriptors.front()!=1 || std::any_of(t.fieldDescriptors.begin()+1,t.fieldDescriptors.end(),[](auto f){return f!=0;}))
                    throw std::runtime_error("unsupported 7.64 drawing table signature");
            }
        }
        else if (older730)
        {
            constexpr std::array<std::array<unsigned,2>,33> signature{{
                {{253,128}},{{254,584}},{{256,1400}},{{257,136}},{{259,524}},{{260,3328}},
                {{264,536}},{{266,52}},{{268,32}},{{269,4888}},{{273,948}},{{275,224}},
                {{277,704}},{{278,296}},{{279,496}},{{280,144}},{{281,16}},{{293,45}},
                {{295,12}},{{296,37}},{{297,12}},{{298,110}},{{301,3438}},{{302,240}},
                {{303,860}},{{304,24}},{{305,64}},{{306,16}},{{307,36}},{{308,40}},
                {{309,20}},{{310,40}},{{311,120}}}};
            if(result.raw.tables.size()!=signature.size()+1)throw std::runtime_error("unsupported 7.30 drawing table count");
            for(std::size_t i=0;i<signature.size();++i)
                if(result.raw.tables[i+1].ordinal!=signature[i][0] || result.raw.tables[i+1].payloadSize!=signature[i][1])
                    throw std::runtime_error("unsupported 7.30 drawing table signature");
        }
        else if (older844)
        {
            struct Signature { unsigned type, width, fields; std::vector<unsigned> references; };
            const std::array<Signature,47> signature{{
                {0,4,2,{0}},
                {253,128,20,{0}},
                {254,596,63,{0,61,62}},
                {256,1400,185,{0,65}},
                {257,136,22,{0,6}},
                {259,624,97,{0,5}},
                {260,4568,632,{0,7,117,326,432}},
                {264,536,76,{0,59}},
                {266,52,11,{0}},
                {268,32,6,{0}},
                {269,5968,793,{0,5}},
                {273,948,162,{0,32}},
                {275,232,37,{0,24}},
                {277,600,92,{0,4}},
                {278,296,48,{0}},
                {279,496,21,{0}},
                {280,144,21,{0}},
                {281,20,6,{0,4}},
                {293,45,6,{0}},
                {295,12,4,{0}},
                {296,37,5,{0}},
                {297,12,4,{0}},
                {298,110,5,{0}},
                {301,3438,50,{0}},
                {302,288,47,{0,31}},
                {303,860,143,{0,20}},
                {304,24,7,{0}},
                {305,64,12,{0}},
                {306,16,5,{0,4}},
                {307,36,10,{0,4,6,8}},
                {308,40,9,{0,4}},
                {309,20,6,{0,4}},
                {310,40,9,{0,4,6}},
                {311,120,21,{0}},
                {312,12,4,{0,2}},
                {313,64,10,{0}},
                {314,32,7,{0,3}},
                {315,56,9,{0}},
                {316,156,24,{0,4}},
                {317,24,7,{0}},
                {318,60,16,{0,6,7,8,9,10,11,12,13,14,15}},
                {319,112,17,{0}},
                {320,20,6,{0}},
                {321,60,16,{0}},
                {322,112,20,{0,3,8}},
                {323,144,23,{0,3}},
                {324,60,11,{0}}
            }};
            if (result.raw.tables.size()!=signature.size()) throw std::runtime_error("unsupported 8.44 drawing table count");
            for (std::size_t i=0;i<signature.size();++i)
            {
                const auto& t=result.raw.tables[i]; const auto& s=signature[i];
                if (t.ordinal!=s.type || t.payloadSize!=s.width || t.fieldDescriptors.size()!=s.fields)
                    throw std::runtime_error("unsupported 8.44 drawing table signature");
                for (std::size_t f=0;f<s.fields;++f)
                    if (t.fieldDescriptors[f]!=(std::find(s.references.begin(),s.references.end(),f)!=s.references.end()?1U:0U))
                        throw std::runtime_error("unsupported 8.44 drawing reference signature");
            }
        }
        else if (older782)
        {
            constexpr std::array<std::array<unsigned,3>,47> signature{{
                {{0,4,2}},{{253,128,20}},{{254,596,63}},{{256,1400,185}},{{257,136,22}},{{259,524,75}},
                {{260,3328,451}},{{264,536,76}},{{266,52,11}},{{268,32,6}},{{269,4888,654}},
                {{273,948,162}},{{275,224,35}},{{277,600,92}},{{278,296,48}},{{279,496,21}},
                {{280,144,21}},{{281,20,6}},{{293,45,6}},{{295,12,4}},{{296,37,5}},{{297,12,4}},
                {{298,110,5}},{{301,3438,50}},{{302,288,47}},{{303,860,143}},{{304,24,7}},
                {{305,64,12}},{{306,16,5}},{{307,36,10}},{{308,40,9}},{{309,20,6}},{{310,40,9}},
                {{311,120,21}},{{312,12,4}},{{313,64,10}},{{314,32,7}},{{315,56,9}},{{316,156,24}},
                {{317,24,7}},{{318,60,16}},{{319,112,17}},{{320,20,6}},{{321,60,16}},
                {{322,112,20}},{{323,144,23}},{{324,60,11}}}};
            if (result.raw.tables.size()!=signature.size()) throw std::runtime_error("unsupported legacy drawing table count");
            for (std::size_t i=0;i<signature.size();++i)
            {
                const auto& t=result.raw.tables[i]; const auto& s=signature[i];
                if (t.ordinal!=s[0] || t.payloadSize!=s[1] || t.fieldDescriptors.size()!=s[2] ||
                    t.fieldDescriptors.front()!=1 || std::any_of(t.fieldDescriptors.begin()+1,t.fieldDescriptors.end(),[](auto f){return f!=0;}))
                    throw std::runtime_error("unsupported legacy drawing table signature");
            }
        }
        else if (older950)
        {
            // Tekla Structures 2023 SP4 Abri-Moto-VCCF25: nine real drawings
            // share this exact 48-section table signature. Three payload
            // widths differ from the 9.54 layout, so retain them raw and only
            // decode the common string/property/sheet tables below.
            constexpr std::array<std::uint32_t,48> signatureWidths{{
                4,128,620,1496,144,664,4740,580,584,52,32,6148,964,240,608,296,496,144,32,45,12,37,12,110,
                3438,296,876,24,64,28,72,48,32,64,120,24,64,40,56,164,24,180,112,20,60,144,152,60}};
            constexpr std::array<std::uint32_t,48> signatureFieldCounts{{
                2,20,63,205,22,101,652,16,84,11,6,804,162,37,92,48,21,21,6,6,4,5,4,5,
                50,47,143,7,12,5,10,9,6,9,21,4,10,7,9,24,7,16,17,6,16,20,23,11}};
            if (result.raw.tables.size()!=signatureWidths.size())
                throw std::runtime_error("unsupported 9.50 drawing table count");
            for (std::size_t i=0;i<signatureWidths.size();++i)
            {
                const auto& t=result.raw.tables[i];
                const auto expectedOrdinal=i==0?0U:types[i-1];
                if (t.ordinal!=expectedOrdinal || t.payloadSize!=signatureWidths[i] ||
                    t.fieldDescriptors.size()!=signatureFieldCounts[i] || t.fieldDescriptors.empty() ||
                    t.fieldDescriptors.front()!=1U ||
                    std::any_of(t.fieldDescriptors.begin()+1,t.fieldDescriptors.end(),[](auto value){return value!=0U;}))
                    throw std::runtime_error("unsupported 9.50 drawing table signature at index "+std::to_string(i)+
                        " (type="+std::to_string(t.ordinal)+", width="+std::to_string(t.payloadSize)+
                        ", fields="+std::to_string(t.fieldDescriptors.size())+")");
            }
        }
        else
        {
            if (result.raw.tables.size()!=48) throw std::runtime_error("unsupported drawing table count");
            for (std::size_t i=0;i<types.size();++i)
                if (result.raw.tables[i+1].ordinal!=types[i] || result.raw.tables[i+1].payloadSize!=widths[i])
                    throw std::runtime_error("unsupported drawing table signature");
        }

        // Type 304 is a fixed 24-byte cross-table index on every pinned DG
        // layout. Its first two words identify the referenced table and row;
        // the third word is useful as a stored relation value, but its meaning
        // is not uniform across all referenced table types.
        std::map<std::pair<std::uint32_t,std::uint32_t>,std::vector<std::uint64_t>> targetRecordOffsets;
        for (const auto& targetTable:result.raw.tables)
        {
            if (targetTable.ordinal==0) continue;
            for (const auto& targetRecord:targetTable.records)
            {
                if (targetRecord.payload.size()<8 || u32(targetRecord.payload,0)!=targetTable.ordinal) continue;
                targetRecordOffsets[{targetTable.ordinal,u32(targetRecord.payload,4)}].push_back(targetRecord.fileOffset);
            }
        }
        for (const auto& record:table(result.raw,304).records)
        {
            const auto& row=record.payload;
            if (row.size()!=24) throw std::runtime_error("invalid drawing record-association index row size");
            DrawingRecordAssociationIndexEntry entry;
            entry.recordTypeCode=u32(row,0);
            entry.recordId=u32(row,4);
            entry.relatedId=u32(row,8);
            std::copy_n(row.begin()+12,entry.unclassifiedBytes.size(),entry.unclassifiedBytes.begin());
            const auto target=targetRecordOffsets.find({entry.recordTypeCode,entry.recordId});
            if (target!=targetRecordOffsets.end()) entry.candidateTargetRecordOffsets=target->second;
            result.recordAssociationIndex.push_back(entry);
        }

        // Type 311 has a stable 120-byte row in every pinned DG schema. The
        // four doubles at payload offsets 24..55 behave as 2D line endpoints
        // across all inspected versions; retain every row and its file offset
        // without assigning style/context semantics to the remaining bytes.
        for (const auto& record:table(result.raw,311).records)
        {
            const auto& row=record.payload;
            if (row.size()!=120) throw std::runtime_error("invalid drawing type-311 row size");
            DrawingLineSegment line;
            line.recordId=u32(row,4);
            line.startPoint={real(row,24),real(row,32)};
            line.endPoint={real(row,40),real(row,48)};
            line.rawRecordOffset=record.fileOffset;
            result.lineSegments.push_back(std::move(line));
        }
        if(result.raw.storageVersion=="9.54")
            for(const auto& record:table(result.raw,281).records)
            {
                const auto& row=record.payload;
                if(row.size()!=32) throw std::runtime_error("invalid 9.54 type-281 row size");
                if(u32(row,4)!=311) continue;
                DrawingType281LineReference reference;
                reference.referenceRecordId=u32(row,0);
                reference.lineRecordId=u32(row,28);
                const auto target=targetRecordOffsets.find({311,reference.lineRecordId});
                if(target!=targetRecordOffsets.end()) reference.candidateLineRecordOffsets=target->second;
                for(std::size_t i=0;i<reference.unclassifiedWords.size();++i)
                    reference.unclassifiedWords[i]=u32(row,8+i*4);
                reference.rawRecordOffset=record.fileOffset;
                result.type281LineReferences.push_back(std::move(reference));
            }

        // Type 309 numeric model references are only verified for the two
        // same-version legacy project corpora. Keep every row, including IDs
        // that do not resolve in a paired DB1 snapshot.
        if (older730 || older782)
            for (const auto& record:table(result.raw,309).records)
            {
                const auto& row=record.payload;
                if (row.size()!=20) throw std::runtime_error("invalid legacy drawing model-reference row size");
                result.legacyModelReferences.push_back({u32(row,0),u32(row,12),record.fileOffset});
            }

        struct Chunk { std::uint32_t next; std::string text; };
        std::map<std::uint32_t,Chunk> chunks;
        std::set<std::uint32_t> continuations;
        for (const auto& row:table(result.raw,293).records)
        {
            const auto id=u32(row.payload,0), next=u32(row.payload,4);
            unique(chunks,id,Chunk{next,fixed(row.payload,16,28)});
            if (next) continuations.insert(next);
        }
        for (auto id:continuations) if (!chunks.count(id))
        {
            if (!older782) throw std::runtime_error("missing drawing string continuation");
            result.diagnostics.push_back("missing legacy drawing string continuation retained as incomplete text: "+std::to_string(id));
        }
        std::map<std::uint32_t,unsigned> state;
        for (const auto& entry:chunks)
        {
            std::vector<std::uint32_t> stack;
            auto id=entry.first;
            while (id && chunks.count(id) && state[id]==0) { state[id]=1; stack.push_back(id); id=chunks.at(id).next; }
            if (id && state[id]==1) throw std::runtime_error("cycle in drawing string chain");
            for (auto visited:stack) state[visited]=2;
        }
        std::size_t total=0;
        for (const auto& entry:chunks)
        {
            if (continuations.count(entry.first)) continue;
            DrawingString string; string.id=entry.first;
            for (auto id=entry.first;id;id=chunks.at(id).next)
            {
                if (!chunks.count(id)) { string.complete=false; string.missingContinuationId=id; break; }
                const auto& text=chunks.at(id).text;
                if (text.size()>options.maxDecodedBytes-total) throw std::runtime_error("drawing strings exceed decode budget");
                total+=text.size(); string.text+=text;
            }
            if (string.complete)
            {
                try { string.markTemplate=drawingMarkTemplate(string.text); }
                catch (const std::exception&)
                {
                }
                if (!string.markTemplate && string.text.find("<Mark")!=std::string::npos)
                    result.diagnostics.push_back("drawing mark XML is malformed or has an unsupported root; original text retained: "+std::to_string(string.id));
            }
            result.strings.emplace(entry.first,std::move(string));
        }
        for (const auto& row:table(result.raw,298).records)
        {
            if (u32(row.payload,4)!=2) throw std::runtime_error("unsupported drawing string property discriminator");
            DrawingProperty p; p.id=u32(row.payload,0); p.name=fixed(row.payload,8,21); p.stringValue=fixed(row.payload,29,81);
            if (p.name=="grProjectGuid")
            {
                auto guid=p.stringValue;
                if (older844 && guid.size()==38 && guid.compare(0,2,"ID")==0) guid.erase(0,2);
                if (!db1::detail::guidText(guid) || (!result.projectGuid.empty() && result.projectGuid!=guid))
                    throw std::runtime_error("invalid or conflicting drawing project GUID");
                result.projectGuid=std::move(guid);
            }
            if (p.name=="grFileName")
            {
                if (!result.storedFileName.empty() && result.storedFileName!=p.stringValue) throw std::runtime_error("conflicting drawing file names");
                result.storedFileName=p.stringValue;
            }
            const auto id=p.id; unique(result.properties,id,std::move(p));
        }
        for (const auto& row:table(result.raw,296).records)
        {
            DrawingProperty p; p.id=u32(row.payload,0); p.isString=false;
            p.numericValue=real(row.payload,8); p.name=fixed(row.payload,16,21);
            const auto id=p.id; unique(result.properties,id,std::move(p));
        }
        std::set<std::uint32_t> linkIds;
        for (auto type:{295U,297U})
            for (const auto& row:table(result.raw,type).records)
            {
                DrawingPropertyLink link{u32(row.payload,0),u32(row.payload,4),u32(row.payload,8)};
                if (!link.id || !linkIds.insert(link.id).second) throw std::runtime_error("invalid drawing property link ID");
                if (!result.properties.count(link.propertyId)) throw std::runtime_error("drawing property link target is missing");
                if (result.properties.at(link.propertyId).isString!=(type==297)) throw std::runtime_error("drawing property link type mismatch");
                result.propertyLinks.push_back(link);
            }
        for (const auto& row:table(result.raw,266).records)
        {
            if (u32(row.payload,0)!=266) throw std::runtime_error("invalid drawing sheet type");
            DrawingSheet sheet{u32(row.payload,4),real(row.payload,16),real(row.payload,24)};
            if (sheet.width<=0 || sheet.height<=0) throw std::runtime_error("invalid drawing sheet dimensions");
            result.sheets.push_back(sheet);
        }
        if (older713)
        {
            result.diagnostics.emplace_back("7.13 drawing strings, stored properties/links and sheet sizes decoded; subjects, views, dimensions, graphics and lifecycle remain raw");
            if (options.retainDecompressedFileImage) result.raw.decompressedFileImage=std::move(data);
            return true;
        }
        if (older844)
        {
            // The 8.44 view record is wider than the 7.30 record, but its two
            // coordinate blocks and two six-value ranges occupy the same
            // validated prefix offsets. Keep all other view fields raw.
            std::set<std::uint32_t> ids, contexts, ambiguousContexts;
            for (const auto& record:table(result.raw,260).records)
            {
                const auto& row=record.payload;
                if (u32(row,0)!=260) throw std::runtime_error("invalid 8.44 drawing view type");
                DrawingView view; view.recordId=u32(row,4); view.contextId=u32(row,8);
                if (!view.recordId || !view.contextId || !ids.insert(view.recordId).second)
                    throw std::runtime_error("zero or duplicate 8.44 drawing view ID");
                if (!contexts.insert(view.contextId).second) ambiguousContexts.insert(view.contextId);
                if (collapsedCoordinates(row,40) && collapsedCoordinates(row,160))
                {
                    result.unhandledViewRecordIds.push_back(view.recordId);
                    result.diagnostics.push_back("8.44 drawing view has collapsed coordinate blocks; retained raw: "+std::to_string(view.recordId));
                    continue;
                }
                view.viewCoordinates=coordinates(row,40); view.displayCoordinates=coordinates(row,160);
                view.restriction=volume(row,112); view.storedAttributeVolume=volume(row,232);
                result.viewsByRecordId.emplace(view.recordId,view);
                if (!result.viewsByContext.emplace(view.contextId,std::move(view)).second)
                    ambiguousContexts.insert(u32(row,8));
            }
            for (const auto context:ambiguousContexts)
            {
                result.viewsByContext.erase(context);
                result.diagnostics.push_back("ambiguous 8.44 drawing view context retained by record ID: "+std::to_string(context));
            }
            parse844Dimensions(result);
            result.diagnostics.emplace_back("8.44 drawing view coordinate bases, stored ranges and subtype-1 straight dimension anchors/sets decoded; subject, graphics, model references and remaining view/dimension fields stay raw");
            if (options.retainDecompressedFileImage) result.raw.decompressedFileImage=std::move(data);
            return true;
        }
        if (older950)
        {
            result.diagnostics.emplace_back("9.50 strings, properties/links and sheet dimensions decoded; subject, views, dimensions, model references and graphics remain raw");
            if (options.retainDecompressedFileImage) result.raw.decompressedFileImage=std::move(data);
            return true;
        }
        for (const auto& row:table(result.raw,269).records)
        {
            if (u32(row.payload,0)==0)
            {
                result.diagnostics.emplace_back("drawing header placeholder retained raw; no active subject decoded");
                continue;
            }
            if (u32(row.payload,0)!=269 || result.subject) throw std::runtime_error("invalid or duplicate drawing subject header");
            DrawingSubject subject; subject.recordId=u32(row.payload,4); subject.typeCode=u32(row.payload,12);
            if (!subject.recordId) throw std::runtime_error("zero drawing subject ID");
            if (numericIdentity) subject.modelObjectId=u32(row.payload,16);
            else subject.modelGuid=binaryGuid(row.payload,16);
            if (subject.typeCode==1) subject.kind=DrawingSubjectKind::SinglePart;
            else if (subject.typeCode==2 && !older730 && !examDrawing) subject.kind=DrawingSubjectKind::Assembly;
            else if (numericIdentity && subject.typeCode==3) subject.kind=DrawingSubjectKind::GeneralArrangement;
            else result.diagnostics.emplace_back("unknown drawing subject type code retained without classification");
            result.subject=std::move(subject);
        }
        if(examDrawing)
        {
            std::set<std::uint32_t> ids;
            for(const auto& row:table(result.raw,322).records)
            {
                DrawingModelReference reference;
                reference.recordId=u32(row.payload,0);reference.drawingContextId=u32(row.payload,4);
                reference.modelObjectId=u32(row.payload,8);
                if(!reference.recordId || !ids.insert(reference.recordId).second)throw std::runtime_error("zero or duplicate drawing model reference ID");
                result.modelReferences.push_back(std::move(reference));
            }
            for(const auto& row:table(result.raw,260).records)result.unhandledViewRecordIds.push_back(u32(row.payload,4));
            for(const auto& row:table(result.raw,256).records)result.unhandledDimensionRecordIds.push_back(u32(row.payload,4));
            result.diagnostics.emplace_back("8.95/9.08 drawing numeric subjects and references decoded; views, dimensions, graphics and lifecycle remain raw; automatic numeric pairing is verified only for 9.08 with matching project GUID and storage version");
            if(options.retainDecompressedFileImage)result.raw.decompressedFileImage=std::move(data);
            return true;
        }
        std::set<std::uint32_t> viewIds;
        std::set<std::uint32_t> ambiguousContexts;
        std::set<std::uint32_t> allContexts;
        for (const auto& record:table(result.raw,260).records)
        {
            const auto& row=record.payload;
            if (u32(row,0)!=260) throw std::runtime_error("invalid drawing view type");
            DrawingView view; view.recordId=u32(row,4); view.contextId=u32(row,8);
            if (!numericIdentity) view.modelGuid=binaryGuid(row,24);
            if (!view.recordId || !view.contextId || !viewIds.insert(view.recordId).second)
                throw std::runtime_error("zero or duplicate drawing view ID");
            if (!allContexts.insert(view.contextId).second) ambiguousContexts.insert(view.contextId);
            if (numericIdentity && collapsedCoordinates(row,40) && collapsedCoordinates(row,160))
            {
                result.unhandledViewRecordIds.push_back(view.recordId);
                result.diagnostics.push_back("legacy drawing view has collapsed coordinate blocks; retained raw: "+std::to_string(view.recordId));
                continue;
            }
            view.viewCoordinates=coordinates(row,numericIdentity?40:48); view.displayCoordinates=coordinates(row,numericIdentity?160:168);
            view.restriction=volume(row,numericIdentity?112:120); view.storedAttributeVolume=volume(row,numericIdentity?232:240);
            if (result.raw.storageVersion=="9.54")
            {
                // Expose these as stored values only: the scalar's transform
                // direction varies across otherwise matching 9.54 drawings.
                view.storedPaperPosition=std::array<double,2>{real(row,352),real(row,360)};
                view.storedScaleValue=real(row,384);
            }
            const auto context=view.contextId;
            result.viewsByRecordId.emplace(view.recordId,view);
            if (!result.viewsByContext.emplace(context,std::move(view)).second)
            {
                if (!numericIdentity) throw std::runtime_error("duplicate drawing view context");
                ambiguousContexts.insert(context);
            }
        }
        for (const auto context:ambiguousContexts)
        {
            result.viewsByContext.erase(context);
            result.diagnostics.push_back("ambiguous legacy drawing view context retained by record ID: "+std::to_string(context));
        }
        std::map<std::uint32_t,std::size_t> dimensionSetIdCounts, dimensionIdCounts;
        std::set<std::uint32_t> conflictingDimensionSetIds;
        if (older782 || older764)
        {
            std::map<std::uint32_t,const Bytes*> firstSetPayloads;
            for (const auto& record:table(result.raw,257).records)
            {
                const auto id=u32(record.payload,4);
                ++dimensionSetIdCounts[id];
                const auto first=firstSetPayloads.emplace(id,&record.payload);
                if (!first.second && *first.first->second!=record.payload) conflictingDimensionSetIds.insert(id);
            }
            for (const auto& record:table(result.raw,256).records) ++dimensionIdCounts[u32(record.payload,4)];
        }
        for (const auto& record:table(result.raw,257).records)
            {
                const auto& row=record.payload;
                if (u32(row,0)!=257) throw std::runtime_error("invalid straight dimension set type");
                DrawingStraightDimensionSet set;
                set.recordId=u32(row,4); set.contextId=u32(row,8);
                if ((older782 || older764) && dimensionSetIdCounts[set.recordId]>1)
                {
                    result.unhandledDimensionSetRecordIds.push_back(set.recordId);
                    if (older782)
                    {
                        result.duplicateStraightDimensionSetRecords.push_back({set.recordId,set.contextId,record.fileOffset});
                        if (conflictingDimensionSetIds.count(set.recordId))
                        {
                            result.diagnostics.push_back("conflicting duplicate legacy straight dimension set ID retained raw: "+
                                                         std::to_string(set.recordId));
                            continue;
                        }
                        if (result.straightDimensionSets.count(set.recordId))
                        {
                            result.diagnostics.push_back("byte-identical duplicate legacy dimension set row coalesced in ID map: "+
                                                         std::to_string(set.recordId));
                            continue;
                        }
                    }
                    else
                    {
                        result.diagnostics.push_back("duplicate legacy straight dimension set ID retained raw: "+
                                                     std::to_string(set.recordId));
                        continue;
                    }
                }
                if (!((older730 || older782 || older764) ? allContexts.count(set.contextId) : result.viewsByContext.count(set.contextId)))
                {
                    if (older782 || older764)
                    {
                        result.unhandledDimensionSetRecordIds.push_back(set.recordId);
                        result.diagnostics.push_back("legacy straight dimension set has no stored view context; retained raw: "+std::to_string(set.recordId));
                        continue;
                    }
                    throw std::runtime_error("straight dimension set view is missing");
                }
                if ((older730 || older782 || older764) && !result.viewsByContext.count(set.contextId))
                    result.diagnostics.push_back("straight dimension set has no uniquely decoded legacy view: "+std::to_string(set.recordId));
                const auto id=set.recordId; unique(result.straightDimensionSets,id,std::move(set));
        }
        std::set<std::uint32_t> dimensionIds;
        for (const auto& record:table(result.raw,256).records)
            {
                const auto& row=record.payload;
                if (u32(row,0)!=256) throw std::runtime_error("invalid straight dimension type");
                DrawingStraightDimension dimension;
                dimension.recordId=u32(row,4); dimension.contextId=u32(row,8);
                dimension.dimensionSetId=u32(row,12); dimension.subtypeCode=u32(row,16);
                if (!dimension.recordId) throw std::runtime_error("zero straight dimension ID");
                const bool supportedSubtype=older730 ? (dimension.subtypeCode==0U || dimension.subtypeCode==1U) :
                    older782 ? (dimension.subtypeCode==0U || dimension.subtypeCode==1U) : dimension.subtypeCode==1U;
                if ((older782 || older764) && dimensionIdCounts[dimension.recordId]>1)
                {
                    result.unhandledDimensionRecordIds.push_back(dimension.recordId);
                    if (older782 && supportedSubtype)
                    {
                        for (std::size_t i=0;i<3;++i)
                        {
                            dimension.startPoint[i]=real(row,24+i*8);
                            dimension.endPoint[i]=real(row,48+i*8);
                            dimension.upDirection[i]=real(row,168+i*8);
                        }
                        dimension.distance=real(row,192);
                        const auto& up=dimension.upDirection;
                        const double norm=std::hypot(up[0],up[1]);
                        if (std::abs(norm-1)<=1e-8 && std::abs(up[2])<=1e-8 &&
                            std::abs(dimension.endPoint[2]-dimension.startPoint[2])<=1e-8)
                        {
                            const double value=std::abs((dimension.endPoint[0]-dimension.startPoint[0])*up[1]-
                                                        (dimension.endPoint[1]-dimension.startPoint[1])*up[0]);
                            if (!std::isfinite(value)) throw std::runtime_error("duplicate straight dimension projection overflow");
                            dimension.projectedLength=value;
                        }
                        result.duplicateStraightDimensionRecords.push_back({std::move(dimension),record.fileOffset});
                        result.diagnostics.push_back("duplicate legacy straight dimension ID decoded per physical record; ID-keyed lookup remains ambiguous: "+
                                                     std::to_string(u32(row,4)));
                    }
                    else result.diagnostics.push_back("duplicate legacy straight dimension ID retained raw: "+std::to_string(dimension.recordId));
                    continue;
                }
                if (!dimensionIds.insert(dimension.recordId).second)
                    throw std::runtime_error("zero or duplicate straight dimension ID");
                const auto set=result.straightDimensionSets.find(dimension.dimensionSetId);
                if (set==result.straightDimensionSets.end())
                {
                    if (!(older782 || older764)) throw std::runtime_error("straight dimension set is missing");
                    result.diagnostics.push_back("legacy straight dimension "+std::to_string(dimension.recordId)+
                                                 " references missing or ambiguous set ID "+std::to_string(dimension.dimensionSetId));
                }
                else if (dimension.contextId!=set->second.contextId)
                {
                    if (!(older782 || older764)) throw std::runtime_error("straight dimension context differs from its set");
                    result.diagnostics.push_back("legacy straight dimension "+std::to_string(dimension.recordId)+" context "+
                                                 std::to_string(dimension.contextId)+" differs from set "+
                                                 std::to_string(dimension.dimensionSetId)+" context "+std::to_string(set->second.contextId));
                }
                else set->second.dimensionIds.push_back(dimension.recordId);
                if (!supportedSubtype)
                {
                    result.unhandledDimensionRecordIds.push_back(dimension.recordId);
                    result.diagnostics.push_back("unknown straight dimension subtype retained raw: "+std::to_string(dimension.recordId));
                    continue;
                }
                for (std::size_t i=0;i<3;++i)
                {
                    dimension.startPoint[i]=real(row,24+i*8);
                    dimension.endPoint[i]=real(row,48+i*8);
                    dimension.upDirection[i]=real(row,168+i*8);
                }
                dimension.distance=real(row,192);
                const auto& up=dimension.upDirection;
                const double norm=std::hypot(up[0],up[1]);
                if (std::abs(norm-1)<=1e-8 && std::abs(up[2])<=1e-8 &&
                    std::abs(dimension.endPoint[2]-dimension.startPoint[2])<=1e-8)
                {
                    const double value=std::abs((dimension.endPoint[0]-dimension.startPoint[0])*up[1]-
                                                (dimension.endPoint[1]-dimension.startPoint[1])*up[0]);
                    if (!std::isfinite(value)) throw std::runtime_error("straight dimension projection overflow");
                    dimension.projectedLength=value;
                }
                else result.diagnostics.push_back("straight dimension projection requires a unit XY direction and coplanar endpoints: "+std::to_string(dimension.recordId));
                const auto id=dimension.recordId; result.straightDimensions.emplace(id,std::move(dimension));
        }
        for (const auto& link:result.propertyLinks)
        {
            auto view=result.viewsByContext.find(link.ownerId); const auto& property=result.properties.at(link.propertyId);
            if (view==result.viewsByContext.end() || property.name!="gr_cl_view_prop" || !property.isString) continue;
            auto& names=view->second.storedPropertySetNames;
            if (std::find(names.begin(),names.end(),property.stringValue)==names.end()) names.push_back(property.stringValue);
            if (names.size()>1 && !numericIdentity) throw std::runtime_error("conflicting drawing view property sets");
        }
        for (auto& entry:result.viewsByContext)
        {
            auto& view=entry.second; auto& names=view.storedPropertySetNames;
            std::sort(names.begin(),names.end());
            if (names.size()==1) view.propertySetName=names.front();
            else if (names.size()>1) result.diagnostics.push_back("legacy drawing view has conflicting stored property sets: "+std::to_string(view.recordId));
            result.viewsByRecordId.at(view.recordId)=view;
        }
        std::set<std::uint32_t> referenceIds;
        if (!older730)
        for (const auto& row:table(result.raw,322).records)
        {
            DrawingModelReference reference; reference.recordId=u32(row.payload,0); reference.drawingContextId=u32(row.payload,4);
            if (!reference.recordId || !referenceIds.insert(reference.recordId).second) throw std::runtime_error("zero or duplicate drawing model reference ID");
            if (older782 || older764) reference.modelObjectId=u32(row.payload,8);
            else reference.modelGuid=binaryGuid(row.payload,8);
            if (!result.viewsByContext.count(reference.drawingContextId))
                result.diagnostics.emplace_back("drawing model reference has no decoded view context: "+std::to_string(reference.drawingContextId));
            result.modelReferences.push_back(std::move(reference));
        }
        result.diagnostics.emplace_back("partial drawing semantics: paper placement, scale/shortening, dimension styling, other reference types and rendered primitives remain raw; Mark XML structure is exposed without evaluating template expressions");
        if (older764) result.diagnostics.emplace_back("7.64 dimension anchor/set fields decoded from the validated shared 7.30/7.82 layout; these ten drawings have no independent plotted-dimension truth; numeric IDs remain unscoped");
        if (older782 && (!table(result.raw,256).records.empty() || !table(result.raw,257).records.empty()))
            result.diagnostics.emplace_back("legacy drawing dimension layouts remain raw");
        if (older730) result.diagnostics.emplace_back("7.30 drawing model references remain raw; an empty modelReferences collection does not mean no model references exist");
        if (numericIdentity) result.diagnostics.emplace_back("legacy numeric model references are unscoped; no project GUID or automatic DB1 join is inferred; record metadata and lifecycle remain unclassified");
        if (options.retainDecompressedFileImage) result.raw.decompressedFileImage=std::move(data);
        return true;
    }
    catch (const std::exception& e) { result={}; error=e.what(); return false; }
}
}
