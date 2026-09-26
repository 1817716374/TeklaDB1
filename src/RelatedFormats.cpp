#include <tekla/Drawing.hpp>
#include <tekla/Numbering.hpp>
#include "BinaryIO.hpp"
#include "RelatedContainers.hpp"
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
const db1::RawTable& table(const db1::RawDatabase& raw,std::uint32_t type)
{
    for (const auto& item:raw.tables) if (item.ordinal==type) return item;
    throw std::runtime_error("missing drawing table type "+std::to_string(type));
}
template<class Map,class T> void unique(Map& items,std::uint32_t id,T&& value)
{
    if (!id || !items.emplace(id,std::forward<T>(value)).second) throw std::runtime_error("zero or duplicate related object ID");
}
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
        if (version!="7.82" && version!="8.95" && version!="9.52" && version!="9.60")
            throw std::runtime_error("unsupported numbering semantic version "+version);
        if (version!="7.82" && result.raw.databaseGuid.empty()) throw std::runtime_error("numbering database GUID missing");
        std::set<std::string> keys;
        for (const auto& item:result.raw.tables)
        {
            if (item.ordinal!=22)
            {
                if (!item.records.empty()) result.diagnostics.push_back("numbering table "+std::to_string(item.ordinal)+" retains raw records; semantic mapping unavailable");
                continue;
            }
            if (item.payloadSize!=(version=="7.82" ? 76U : 80U)) throw std::runtime_error("unsupported numbering series record size");
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
        if (result.raw.storageVersion!="9.54") throw std::runtime_error("unsupported drawing semantic version "+result.raw.storageVersion);
        // Complete observed directory signature; unknown layouts stay available
        // through parseRawDatabase, never guessed into this semantic mapping.
        constexpr std::array<std::uint32_t,47> types{{253,254,256,257,259,260,263,264,266,268,269,273,275,277,278,279,280,281,293,295,296,297,298,301,302,303,304,305,306,307,308,309,310,311,312,313,314,315,316,317,318,319,320,321,322,323,324}};
        constexpr std::array<std::uint32_t,47> widths{{128,620,1496,144,712,4788,580,584,52,32,6196,964,240,608,296,496,144,32,45,12,37,12,110,3438,296,876,24,64,28,72,48,32,64,120,24,64,40,56,164,24,180,112,20,60,144,152,60}};
        if (result.raw.tables.size()!=48) throw std::runtime_error("unsupported drawing table count");
        for (std::size_t i=0;i<types.size();++i)
            if (result.raw.tables[i+1].ordinal!=types[i] || result.raw.tables[i+1].payloadSize!=widths[i])
                throw std::runtime_error("unsupported drawing table signature");

        struct Chunk { std::uint32_t next; std::string text; };
        std::map<std::uint32_t,Chunk> chunks;
        std::set<std::uint32_t> continuations;
        for (const auto& row:table(result.raw,293).records)
        {
            const auto id=u32(row.payload,0), next=u32(row.payload,4);
            unique(chunks,id,Chunk{next,fixed(row.payload,16,28)});
            if (next) continuations.insert(next);
        }
        for (auto id:continuations) if (!chunks.count(id)) throw std::runtime_error("missing drawing string continuation");
        std::map<std::uint32_t,unsigned> state;
        for (const auto& entry:chunks)
        {
            std::vector<std::uint32_t> stack;
            auto id=entry.first;
            while (id && state[id]==0) { state[id]=1; stack.push_back(id); id=chunks.at(id).next; }
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
                const auto& text=chunks.at(id).text;
                if (text.size()>options.maxDecodedBytes-total) throw std::runtime_error("drawing strings exceed decode budget");
                total+=text.size(); string.text+=text;
            }
            result.strings.emplace(entry.first,std::move(string));
        }
        for (const auto& row:table(result.raw,298).records)
        {
            if (u32(row.payload,4)!=2) throw std::runtime_error("unsupported drawing string property discriminator");
            DrawingProperty p; p.id=u32(row.payload,0); p.name=fixed(row.payload,8,21); p.stringValue=fixed(row.payload,29,81);
            if (p.name=="grProjectGuid")
            {
                if (!db1::detail::guidText(p.stringValue) || (!result.projectGuid.empty() && result.projectGuid!=p.stringValue))
                    throw std::runtime_error("invalid or conflicting drawing project GUID");
                result.projectGuid=p.stringValue;
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
            subject.modelGuid=binaryGuid(row.payload,16);
            if (subject.typeCode==1) subject.kind=DrawingSubjectKind::SinglePart;
            else if (subject.typeCode==2) subject.kind=DrawingSubjectKind::Assembly;
            else result.diagnostics.emplace_back("unknown drawing subject type code retained without classification");
            result.subject=std::move(subject);
        }
        std::set<std::uint32_t> viewIds;
        for (const auto& record:table(result.raw,260).records)
        {
            const auto& row=record.payload;
            if (u32(row,0)!=260) throw std::runtime_error("invalid drawing view type");
            DrawingView view; view.recordId=u32(row,4); view.contextId=u32(row,8); view.modelGuid=binaryGuid(row,24);
            if (!view.recordId || !view.contextId || !viewIds.insert(view.recordId).second)
                throw std::runtime_error("zero or duplicate drawing view ID");
            view.viewCoordinates=coordinates(row,48); view.displayCoordinates=coordinates(row,168);
            view.restriction=volume(row,120); view.storedAttributeVolume=volume(row,240);
            const auto context=view.contextId;
            if (!result.viewsByContext.emplace(context,std::move(view)).second) throw std::runtime_error("duplicate drawing view context");
        }
        for (const auto& link:result.propertyLinks)
        {
            auto view=result.viewsByContext.find(link.ownerId); const auto& property=result.properties.at(link.propertyId);
            if (view==result.viewsByContext.end() || property.name!="gr_cl_view_prop" || !property.isString) continue;
            if (!view->second.propertySetName.empty() && view->second.propertySetName!=property.stringValue)
                throw std::runtime_error("conflicting drawing view property sets");
            view->second.propertySetName=property.stringValue;
        }
        std::set<std::uint32_t> referenceIds;
        for (const auto& row:table(result.raw,322).records)
        {
            DrawingModelReference reference; reference.recordId=u32(row.payload,0); reference.drawingContextId=u32(row.payload,4);
            if (!reference.recordId || !referenceIds.insert(reference.recordId).second) throw std::runtime_error("zero or duplicate drawing model reference ID");
            reference.modelGuid=binaryGuid(row.payload,8);
            if (!result.viewsByContext.count(reference.drawingContextId))
                result.diagnostics.emplace_back("drawing model reference has no decoded view context: "+std::to_string(reference.drawingContextId));
            result.modelReferences.push_back(std::move(reference));
        }
        result.diagnostics.emplace_back("partial drawing semantics: paper placement, scale/shortening, dimensions, other reference types and rendered primitives remain raw; mark XML is stored text");
        if (options.retainDecompressedFileImage) result.raw.decompressedFileImage=std::move(data);
        return true;
    }
    catch (const std::exception& e) { result={}; error=e.what(); return false; }
}
}
