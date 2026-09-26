#include <tekla/Project.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <regex>

namespace
{
struct Fingerprint
{
    std::uint64_t value = 14695981039346656037ULL;
    void byte(unsigned char b) { value = (value ^ b) * 1099511628211ULL; }
    void number(std::uint64_t n) { for (int i=0;i<8;++i) { byte(static_cast<unsigned char>(n)); n >>= 8; } }
    void text(const std::string& s) { number(s.size()); for (unsigned char c : s) byte(c); }
    void real(double v)
    {
        if (!std::isfinite(v)) throw std::runtime_error("non-finite semantic coordinate");
        std::uint64_t bits; std::memcpy(&bits,&v,sizeof(bits)); number(bits);
    }
};
void summary(const tekla::db1::Model& model)
{
    Fingerprint hash;
    std::vector<std::uint32_t> ids;
    for (const auto& entry : model.parts) ids.push_back(entry.first);
    std::sort(ids.begin(),ids.end());
    for (auto id : ids)
    {
        const auto& p=model.parts.at(id);
        if (!model.definitions.count(p.definitionId) || !model.points.count(p.startPointId) ||
            !model.points.count(p.endPointId) || !model.frames.count(p.orientationId))
            throw std::runtime_error("part has missing required reference");
        hash.number(p.id); hash.number(p.internalType); hash.text(p.guid); hash.text(p.name);
        hash.text(p.profile); hash.text(p.material); hash.real(p.length);
        for (const auto* point : {&p.start,&p.end,&p.origin,&p.axis,&p.secondary})
            for (auto coordinate : *point) hash.real(coordinate);
        for (const auto& point : p.contour) for (auto coordinate : point.value) hash.real(coordinate);
    }
    for (auto id : model.actualPartIds)
        if (!model.parts.count(id)) throw std::runtime_error("actual part ID missing");
    for (const auto& group : model.boltGroups)
    {
        if (!model.boltDefinitions.count(group.definitionId)) throw std::runtime_error("bolt definition missing");
        for (const auto& point : group.positions) for (auto coordinate : point) if (!std::isfinite(coordinate)) throw std::runtime_error("non-finite bolt coordinate");
    }
    std::cout << "version=" << model.storageVersion << " parts=" << model.actualPartIds.size()
              << " operative=" << model.operativePartIds.size() << " profiles=" << model.profiles.size()
              << " bolts=" << model.boltGroups.size() << " welds=" << model.welds.size()
              << " assemblies=" << model.assemblies.size() << " components=" << model.components.size()
              << " properties=" << model.properties.size() << " parameters=" << model.parameterDefinitions.size()
              << " custom=" << model.customComponentDefinitions.size() << " unhandled=" << model.unhandledPartIds.size()
              << " fingerprint=" << std::hex << hash.value << std::dec << '\n';
}
int run(const std::string& mode, const std::filesystem::path& path)
{
    try
    {
        std::string error;
        if (mode=="model" || mode=="library")
        {
            tekla::db1::Model model;
            const auto ok=mode=="model" ? tekla::db1::parseModelFile(path,model,error) : tekla::db1::parseComponentLibrary(path,model,error);
            if (!ok) { std::cerr << error << '\n'; return 1; }
            summary(model);
        }
        else if (mode=="project" || mode=="related_evidence")
        {
            tekla::Project project;
            if (!tekla::readProject(path,project,error)) { std::cerr << error << '\n'; return 1; }
            summary(project.model);
            std::size_t failed=0,drawings=0;
            for (const auto& file : project.files)
            {
                failed += file.level==tekla::ReadLevel::Failed;
                drawings += file.role==tekla::FileRole::Drawing;
                if (file.level==tekla::ReadLevel::Failed) std::cerr << file.diagnostic << '\n';
            }
            std::cout << "files=" << project.files.size() << " associations=" << project.associations.size()
                      << " raw=" << project.rawCompanions.size() << " failed=" << failed << " drawings=" << drawings << '\n';
            if (failed) return 1;
            if (mode=="related_evidence")
            {
                // Independent source: Tekla's text log, not output from this parser.
                std::ifstream log(path/"numberinghistory.txt",std::ios::binary);
                if (!log) throw std::runtime_error("numbering evidence log missing");
                const std::string content((std::istreambuf_iterator<char>(log)),{});
                const std::regex pattern(R"((Part|Assembly)\s+guid:\s+\S+\s+series:(\S+)\s+[^\r\n]* -> [^\r\n/]*?/(\d+))");
                std::map<std::pair<std::string,std::string>,std::uint64_t> maxima;
                for (auto it=std::sregex_iterator(content.begin(),content.end(),pattern);it!=std::sregex_iterator();++it)
                {
                    const auto key=std::make_pair((*it)[1].str(),(*it)[2].str());
                    maxima[key]=(std::max)(maxima[key],static_cast<std::uint64_t>(std::stoull((*it)[3].str())));
                }
                std::size_t compared=0;
                for (const auto& file:project.numbering)
                    if (file.first.stem()==project.model.databasePath.stem())
                        for (const auto& series:file.second.series)
                            for (const auto& field:{std::make_pair("Part",series.partCounter),std::make_pair("Assembly",series.assemblyCounter)})
                                if (field.second)
                                {
                                    const auto found=maxima.find({field.first,series.key});
                                    if (found==maxima.end() || found->second+1!=std::uint64_t(series.startNumber)+field.second)
                                        throw std::runtime_error("numbering counter disagrees with Tekla history: "+series.key);
                                    ++compared;
                                }
                std::size_t references=0;
                for (const auto& file:project.drawings)
                {
                    if (std::filesystem::u8path(file.second.storedFileName)!=file.first.filename()) throw std::runtime_error("stored drawing filename mismatch");
                    if (file.second.projectGuid!=project.model.databaseGuid) throw std::runtime_error("stored drawing project GUID mismatch");
                    references+=file.second.modelReferences.size();
                }
                if (compared!=10 || project.drawings.size()!=7 || references!=28 || project.drawingModelAssociations.size()!=references)
                    throw std::runtime_error("public related-format evidence count mismatch");
                std::cout<<"independent_numbering_counters="<<compared<<" drawing_model_guid_links="<<references<<'\n';
            }
        }
        else if (mode=="raw")
        {
            tekla::db1::RawDatabase raw;
            tekla::db1::RawDatabaseOptions options; options.retainDecompressedFileImage=true;
            if (!tekla::db1::parseRawDatabase(path,raw,error,options)) { std::cerr << error << '\n'; return 1; }
            std::size_t rows=0,opaque=0; Fingerprint hash;
            for (const auto& table : raw.tables) { rows+=table.records.size(); opaque+=!table.schemaValid; }
            for (auto b : raw.decompressedFileImage) hash.byte(b);
            std::cout << "version=" << raw.storageVersion << " tables=" << raw.tables.size() << " records=" << rows
                      << " opaque=" << opaque << " bytes=" << raw.decompressedFileImage.size()
                      << " fingerprint=" << std::hex << hash.value << std::dec << '\n';
        }
        else if (mode=="numbering")
        {
            tekla::NumberingDatabase numbering;
            if (!tekla::parseNumberingDatabase(path,numbering,error)) { std::cerr<<error<<'\n'; return 1; }
            Fingerprint hash;
            for (const auto& series:numbering.series)
            {
                hash.text(series.key); hash.text(series.prefix); hash.number(series.startNumber);
                hash.number(series.partCounter); hash.number(series.assemblyCounter);
                for (auto field:series.additionalFields) hash.number(field);
            }
            std::cout<<"version="<<numbering.raw.storageVersion<<" tables="<<numbering.raw.tables.size()
                     <<" series="<<numbering.series.size()<<" guid="<<numbering.raw.databaseGuid
                     <<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
        }
        else if (mode=="drawing")
        {
            tekla::Drawing drawing;
            if (!tekla::parseDrawing(path,drawing,error)) { std::cerr<<error<<'\n'; return 1; }
            Fingerprint hash;
            for (const auto& entry:drawing.strings) { hash.number(entry.first); hash.text(entry.second.text); }
            for (const auto& entry:drawing.properties)
            {
                const auto& p=entry.second; hash.number(p.id); hash.text(p.name); hash.number(p.isString);
                if (p.isString) hash.text(p.stringValue); else hash.real(p.numericValue);
            }
            for (const auto& link:drawing.propertyLinks) { hash.number(link.id); hash.number(link.propertyId); hash.number(link.ownerId); }
            for (const auto& sheet:drawing.sheets) { hash.number(sheet.id); hash.real(sheet.width); hash.real(sheet.height); }
            for (const auto& ref:drawing.modelReferences) { hash.number(ref.recordId); hash.number(ref.drawingContextId); hash.text(ref.modelGuid); }
            std::cout<<"version="<<drawing.raw.storageVersion<<" tables="<<drawing.raw.tables.size()
                     <<" strings="<<drawing.strings.size()<<" properties="<<drawing.properties.size()
                     <<" links="<<drawing.propertyLinks.size()<<" sheets="<<drawing.sheets.size()
                     <<" model_refs="<<drawing.modelReferences.size()
                     <<" guid="<<drawing.projectGuid<<" stored="<<drawing.storedFileName
                     <<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
        }
        else return 2;
        return 0;
    }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv)
{
    if(argc!=3) return 2;
    const std::wstring wide(argv[1]); return run(std::string(wide.begin(),wide.end()),std::filesystem::path(argv[2]));
}
#else
int main(int argc,char** argv)
{
    return argc==3 ? run(argv[1],std::filesystem::u8path(argv[2])) : 2;
}
#endif
