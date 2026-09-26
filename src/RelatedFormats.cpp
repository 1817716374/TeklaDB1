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
        const bool legacy = version=="7.30" || version=="7.82";
        if (version=="7.30" && (result.raw.preamble.size()!=12 || result.raw.preamble[6]!=' '))
            throw std::runtime_error("unsupported 7.30 numbering preamble");
        if (!legacy && version!="8.95" && version!="9.52" && version!="9.60")
            throw std::runtime_error("unsupported numbering semantic version "+version);
        if (!legacy && result.raw.databaseGuid.empty()) throw std::runtime_error("numbering database GUID missing");
        std::set<std::string> keys;
        for (const auto& item:result.raw.tables)
        {
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
        const bool older782=result.raw.storageVersion=="7.82";
        const bool older730=result.raw.storageVersion=="7.30";
        const bool numericIdentity=older730 || older782;
        if (!numericIdentity && result.raw.storageVersion!="9.54") throw std::runtime_error("unsupported drawing semantic version "+result.raw.storageVersion);
        // Complete observed directory signature; unknown layouts stay available
        // through parseRawDatabase, never guessed into this semantic mapping.
        constexpr std::array<std::uint32_t,47> types{{253,254,256,257,259,260,263,264,266,268,269,273,275,277,278,279,280,281,293,295,296,297,298,301,302,303,304,305,306,307,308,309,310,311,312,313,314,315,316,317,318,319,320,321,322,323,324}};
        constexpr std::array<std::uint32_t,47> widths{{128,620,1496,144,712,4788,580,584,52,32,6196,964,240,608,296,496,144,32,45,12,37,12,110,3438,296,876,24,64,28,72,48,32,64,120,24,64,40,56,164,24,180,112,20,60,144,152,60}};
        if (older730)
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
        else
        {
            if (result.raw.tables.size()!=48) throw std::runtime_error("unsupported drawing table count");
            for (std::size_t i=0;i<types.size();++i)
                if (result.raw.tables[i+1].ordinal!=types[i] || result.raw.tables[i+1].payloadSize!=widths[i])
                    throw std::runtime_error("unsupported drawing table signature");
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
            if (numericIdentity) subject.modelObjectId=u32(row.payload,16);
            else subject.modelGuid=binaryGuid(row.payload,16);
            if (subject.typeCode==1) subject.kind=DrawingSubjectKind::SinglePart;
            else if (subject.typeCode==2 && !older730) subject.kind=DrawingSubjectKind::Assembly;
            else if (numericIdentity && subject.typeCode==3) subject.kind=DrawingSubjectKind::GeneralArrangement;
            else result.diagnostics.emplace_back("unknown drawing subject type code retained without classification");
            result.subject=std::move(subject);
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
            if (older782) reference.modelObjectId=u32(row.payload,8);
            else reference.modelGuid=binaryGuid(row.payload,8);
            if (!result.viewsByContext.count(reference.drawingContextId))
                result.diagnostics.emplace_back("drawing model reference has no decoded view context: "+std::to_string(reference.drawingContextId));
            result.modelReferences.push_back(std::move(reference));
        }
        result.diagnostics.emplace_back("partial drawing semantics: paper placement, scale/shortening, dimensions, other reference types and rendered primitives remain raw; mark XML is stored text");
        if (older730) result.diagnostics.emplace_back("7.30 drawing model references remain raw; an empty modelReferences collection does not mean no model references exist");
        if (numericIdentity) result.diagnostics.emplace_back("legacy numeric model references are unscoped; no project GUID or automatic DB1 join is inferred; record metadata and lifecycle remain unclassified");
        if (options.retainDecompressedFileImage) result.raw.decompressedFileImage=std::move(data);
        return true;
    }
    catch (const std::exception& e) { result={}; error=e.what(); return false; }
}
}
