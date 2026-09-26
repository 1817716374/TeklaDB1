#include <tekla/Project.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <regex>
#include <limits>

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
#include "OwnershipValidation.hpp"
#include "PositionValidation.hpp"
#include "SurfaceValidation.hpp"
#include "ObjectNumberingValidation.hpp"
#include "LegacyNumberingValidation.hpp"
#include "ReinforcementValidation.hpp"

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
    if (model.storageVersion == "7.82")
    {
        auto bolts = model.individualBolts;
        std::sort(bolts.begin(), bolts.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        for (const auto& bolt : bolts)
        {
            hash.number(bolt.id);
            if (model.parts.at(bolt.id).internalType != 10) throw std::runtime_error("individual bolt type mismatch");
            for (auto partId : bolt.connectedPartIds) { model.parts.at(partId); hash.number(partId); }
        }
        auto assemblies = model.assemblies;
        std::sort(assemblies.begin(), assemblies.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
        for (const auto& assembly : assemblies)
        {
            hash.number(assembly.id); hash.text(assembly.name);
            for (auto partId : assembly.memberIds) { model.parts.at(partId); hash.number(partId); }
        }
    }
    std::cout << "version=" << model.storageVersion << " parts=" << model.actualPartIds.size()
              << " operative=" << model.operativePartIds.size() << " profiles=" << model.profiles.size()
              << " bolts=" << model.boltGroups.size() << " welds=" << model.welds.size()
              << " assemblies=" << model.assemblies.size() << " components=" << model.components.size()
              << " properties=" << model.properties.size() << " parameters=" << model.parameterDefinitions.size()
              << " custom=" << model.customComponentDefinitions.size() << " unhandled=" << model.unhandledPartIds.size();
    if (model.storageVersion == "7.82") std::cout << " individual_bolts=" << model.individualBolts.size();
    std::cout << " fingerprint=" << std::hex << hash.value << std::dec << '\n';
}
int run(const std::string& mode, const std::filesystem::path& path)
{
    try
    {
        std::string error;
        if (mode=="legacy_number_evidence") validateLegacyNumberEvidence(path);
        else if (mode=="empty_bolt_evidence")
        {
            tekla::Project project;
            if(!tekla::readProject(path,project,error))throw std::runtime_error(error);
            const auto& m=project.model;
            if(m.boltGroups.size()!=2 || m.actualPartIds.size()!=21 || m.reinforcementDefinitions.size()!=137 || !m.reinforcements.empty())
                throw std::runtime_error("training model coverage changed");
            std::vector<std::uint32_t> ids;
            for(const auto& b:m.boltGroups)
            {
                if(b.positionArrayId!=0 || !b.positions.empty() || b.definitionId!=453966 || m.boltDefinitions.at(b.definitionId).count!=0)
                    throw std::runtime_error("empty bolt position/reference evidence changed");
                if(!m.identities.count(b.id) || b.connectedPartIds.empty())throw std::runtime_error("empty bolt object/connection lost");
                ids.push_back(b.id);
            }
            std::sort(ids.begin(),ids.end());
            if(ids!=std::vector<std::uint32_t>{436963,442032})throw std::runtime_error("empty bolt identities changed");
            if(!project.environment || project.optionsDatabases.size()!=2 || project.numbering.size()!=2)
                throw std::runtime_error("training companion databases missing");
            std::cout<<"empty_bolt_groups=2 model_parts=21 unused_reinforcement_definitions=137 numbering_databases=2 options_databases=2\n";
        }
        else if (mode=="object_number_evidence") validateObjectNumberEvidence(path);
        else if (mode=="reinforcement_evidence") validateReinforcementEvidence(path);
        else if (mode=="reinforcement_model" || mode=="reinforcement_library")
        {
            const bool library=mode=="reinforcement_library";
            tekla::db1::Model model;tekla::db1::RawDatabase raw;std::string error;
            const bool ok=library?tekla::db1::parseComponentLibrary(path,model,error):tekla::db1::parseModelFile(path,model,error);
            if(!ok || !tekla::db1::parseRawDatabase(path,raw,error))throw std::runtime_error(error);
            validateReinforcement(model,raw,library);
        }
        else if (mode=="object_number_model" || mode=="object_number_library")
        {
            const bool library=mode=="object_number_library";
            tekla::db1::Model model; tekla::db1::RawDatabase raw;
            const bool ok=library?tekla::db1::parseComponentLibrary(path,model,error):tekla::db1::parseModelFile(path,model,error);
            if (!ok || !tekla::db1::parseRawDatabase(path,raw,error)) throw std::runtime_error(error);
            validateObjectNumbers(model,raw,library);
        }
        else if (mode=="surface_evidence") validateSurfaceEvidence(path);
        else if (mode=="surface_model" || mode=="surface_library")
        {
            const bool library=mode=="surface_library";
            tekla::db1::Model model; tekla::db1::RawDatabase raw;
            const bool ok=library?tekla::db1::parseComponentLibrary(path,model,error):tekla::db1::parseModelFile(path,model,error);
            if (!ok || !tekla::db1::parseRawDatabase(path,raw,error)) throw std::runtime_error(error);
            validateSurfaces(model,raw,library);
        }
        else if (mode=="position_source_evidence") validatePositionSourceEvidence(path);
        else if (mode=="position_model" || mode=="position_library")
        {
            const bool library=mode=="position_library";
            tekla::db1::Model model; tekla::db1::RawDatabase raw;
            const bool ok=library?tekla::db1::parseComponentLibrary(path,model,error):tekla::db1::parseModelFile(path,model,error);
            if (!ok || !tekla::db1::parseRawDatabase(path,raw,error)) throw std::runtime_error(error);
            validatePositions(model,raw,library);
        }
        else if (mode=="ownership_model" || mode=="ownership_library")
        {
            const bool library=mode=="ownership_library";
            tekla::db1::Model model; tekla::db1::RawDatabase raw;
            const bool ok=library?tekla::db1::parseComponentLibrary(path,model,error):tekla::db1::parseModelFile(path,model,error);
            if (!ok || !tekla::db1::parseRawDatabase(path,raw,error)) throw std::runtime_error(error);
            validateOwnership(model,raw,library);
        }
        else if (mode=="modern_component_dialog_evidence")
        {
            tekla::db1::Model model;
            const auto libraryPath=path.parent_path()/"MohamedHasan94__AUTRA"/"AUTRA"/"wwwroot"/"Outputs"/"Tekla"/"ITIFinal02_35"/"xslib.db1";
            if (!tekla::db1::parseComponentLibrary(libraryPath,model,error)) throw std::runtime_error(error);
            const std::pair<std::uint32_t,const char*> dialogs[]={{158023,"GenericFormworkPlatform"},{157338,"GenericFormworkItem"},{156768,"GenericFormworkFillerPanel"}};
            const std::regex pattern(R"rx(parameter\(\s*"[^"]*"\s*,\s*"([^"]+)")rx");
            std::size_t matched=0; Fingerprint hash;
            for (const auto& entry : dialogs)
            {
                const auto definition=std::find_if(model.customComponentDefinitions.begin(),model.customComponentDefinitions.end(),[&](const auto& c){return c.id==entry.first;});
                if (definition==model.customComponentDefinitions.end() || definition->name!=entry.second) throw std::runtime_error("modern dialog owner name mismatch");
                if (model.identityClasses.at(model.identities.at(entry.first).classReferenceId).recordKind!=4) throw std::runtime_error("custom definition class evidence mismatch");
                std::ifstream f(path/(std::string(entry.second)+".inp"),std::ios::binary);
                if (!f) throw std::runtime_error("modern dialog evidence file missing");
                const std::string text((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
                if (text.find(std::string("macro(1, \"")+entry.second+"\")")==std::string::npos) throw std::runtime_error("dialog macro name mismatch");
                hash.number(entry.first); hash.text(entry.second);
                for (auto i=std::sregex_iterator(text.begin(),text.end(),pattern);i!=std::sregex_iterator();++i)
                {
                    const auto name=(*i)[1].str(); std::size_t count=0;
                    for (auto id : definition->parameterIds)
                    {
                        const auto& p=model.parameterDefinitions.at(id);
                        if (p.name!=name) continue;
                        if (p.ownerId!=entry.first || model.identityClasses.at(model.identities.at(id).classReferenceId).recordKind!=64)
                            throw std::runtime_error("dialog parameter owner/class mismatch");
                        hash.number(id); hash.text(p.name); hash.text(p.label); hash.text(p.expression); ++count;
                    }
                    if (count!=1) throw std::runtime_error("dialog parameter missing or ambiguous: "+name);
                    ++matched;
                }
            }
            if (matched!=15) throw std::runtime_error("modern dialog evidence count changed");
            std::cout<<"dialogs=3 parameter_owner_matches=15 fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
        }
        else if (mode=="model" || mode=="library")
        {
            tekla::db1::Model model;
            const auto ok=mode=="model" ? tekla::db1::parseModelFile(path,model,error) : tekla::db1::parseComponentLibrary(path,model,error);
            if (!ok) { std::cerr << error << '\n'; return 1; }
            summary(model);
        }
        else if (mode=="legacy_component_evidence" || mode=="legacy_component_graph_evidence")
        {
            tekla::db1::Model model;
            if (!tekla::db1::parseComponentLibrary(path.parent_path()/"PSDBIM__EXCEL-1246A-BLDG-B"/"xslib.db1",model,error))
                throw std::runtime_error(error);
            const auto readText=[](const std::filesystem::path& p) {
                std::ifstream f(p,std::ios::binary);
                if (!f) throw std::runtime_error("missing component evidence file");
                return std::string(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
            };
            const auto catalog=readText(path/"ComponentCatalog.txt");
            auto definitions=model.customComponentDefinitions;
            std::sort(definitions.begin(),definitions.end(),[](const auto& a,const auto& b){return a.id<b.id;});
            Fingerprint hash; std::size_t named=0,anonymous=0,parameters=0,children=0;
            for (const auto& d : definitions)
            {
                if (d.name.empty()) ++anonymous;
                else
                {
                    if (catalog.find("-10\t"+d.name+"\t")==std::string::npos)
                        throw std::runtime_error("custom part name disagrees with ComponentCatalog.txt");
                    ++named;
                }
                hash.number(d.id); hash.number(d.kind); hash.number(d.classificationCode);
                hash.text(d.name); hash.text(d.description); hash.text(d.guid);
                if (d.referenceIds[2]) throw std::runtime_error("invented third legacy custom reference");
                for (auto id : d.parameterIds)
                {
                    const auto& p=model.parameterDefinitions.at(id);
                    if (p.ownerId!=d.id) throw std::runtime_error("custom parameter ownership mismatch");
                    hash.number(id); hash.text(p.name); hash.text(p.label); hash.text(p.expression); hash.number(p.valueType);
                    ++parameters;
                }
                for (auto id : d.childObjectIds)
                {
                    if (id==d.id || model.identities.at(id).ownerId!=d.id) throw std::runtime_error("custom child ownership mismatch");
                    hash.number(id); ++children;
                }
            }
            const std::pair<std::uint32_t,const char*> dialogs[]={{103393,"Anchor-Bent Rod"},{112580,"Anchor-Epoxy"},
                {31989,"2-Angle+Plate lintel"},{50,"1 Sided Beam to Column Flg MC"}};
            const std::regex parameter(R"rx(parameter\(\s*"[^"]*"\s*,\s*"([^"]+)")rx");
            std::size_t matched=0,unresolved=0;
            for (const auto& dialog : dialogs)
            {
                const auto definition=std::find_if(definitions.begin(),definitions.end(),[&](const auto& d){return d.id==dialog.first && d.name==dialog.second;});
                const auto component=std::find_if(model.components.begin(),model.components.end(),[&](const auto& c){return c.id==dialog.first && c.name==dialog.second;});
                if (definition==definitions.end() && component==model.components.end())
                    throw std::runtime_error("dialog owner/name mismatch");
                const auto text=readText(path/(std::string(dialog.second)+".inp"));
                if (text.find(std::string("\"")+dialog.second+"\"")==std::string::npos)
                    throw std::runtime_error("dialog component name missing");
                for (auto i=std::sregex_iterator(text.begin(),text.end(),parameter); i!=std::sregex_iterator(); ++i)
                {
                    const auto name=(*i)[1].str();
                    const auto found=std::any_of(model.parameterDefinitions.begin(),model.parameterDefinitions.end(),[&](const auto& p){
                        return p.second.ownerId==dialog.first && p.second.name==name;
                    });
                    const auto distanceFound=std::any_of(model.distanceParameters.begin(),model.distanceParameters.end(),[&](const auto& p){
                        return p.second.ownerId==dialog.first && p.second.name==name;
                    });
                    if (found || distanceFound) ++matched;
                    else throw std::runtime_error("unexpected missing dialog parameter "+name);
                }
            }
            if (definitions.size()!=40 || named!=28 || anonymous!=12 || parameters!=660 || children!=2726 || matched!=69 || unresolved!=0)
                throw std::runtime_error("legacy custom component evidence changed");
            std::cout << "definitions=40 named=28 anonymous=12 parameters=660 children=2726 dialog_matches=69 unresolved=0 fingerprint="
                      << std::hex << hash.value << std::dec << '\n';
            if (mode=="legacy_component_graph_evidence")
            {
                Fingerprint graph; std::vector<std::uint32_t> distanceIds,formulaIds;
                for (const auto& d : model.distanceParameters) distanceIds.push_back(d.first);
                for (const auto& f : model.formulaBindings) formulaIds.push_back(f.first);
                std::sort(distanceIds.begin(),distanceIds.end()); std::sort(formulaIds.begin(),formulaIds.end());
                std::size_t bound=0,references=0,inputs=0,crossOwner=0;
                for (auto id : distanceIds)
                {
                    const auto& d=model.distanceParameters.at(id);
                    graph.number(id); graph.number(d.ownerId); graph.text(d.guid); graph.text(d.name); graph.text(d.label);
                    graph.real(d.storedDistance); graph.real(d.secondaryStoredValue); graph.text(d.propertyToken); graph.text(d.planeToken);
                    for (auto v : d.rawFields) graph.number(v);
                    if (d.boundObjectIds.size()!=2 || model.identities.at(id).ownerId!=d.ownerId) throw std::runtime_error("distance bindings/owner changed");
                    for (auto v : d.boundObjectIds) { model.identities.at(v); graph.number(v); ++bound; }
                    for (auto v : d.formulaBindingIds)
                    {
                        if (model.formulaBindings.at(v).targetObjectId!=id) throw std::runtime_error("distance formula target mismatch");
                        graph.number(v);
                    }
                }
                for (auto id : formulaIds)
                {
                    const auto& f=model.formulaBindings.at(id);
                    graph.number(id); graph.number(f.ownerId); graph.number(f.targetObjectId); graph.number(f.storedIndex);
                    graph.text(f.guid); graph.text(f.propertyName); graph.text(f.expression);
                    if (model.identities.at(id).ownerId!=f.ownerId) throw std::runtime_error("formula owner mismatch");
                    const auto& indexed=model.formulaBindingIdsByTarget.at(f.targetObjectId);
                    if (std::count(indexed.begin(),indexed.end(),id)!=1) throw std::runtime_error("formula target index mismatch");
                    for (auto v : f.referencedObjectIds)
                    {
                        const auto owner=model.identities.at(v).ownerId;
                        crossOwner+=owner!=f.ownerId && v!=f.ownerId;
                        graph.number(v); ++references;
                    }
                    for (auto v : f.inputObjectIds)
                    {
                        if (v==f.targetObjectId) throw std::runtime_error("formula output included in inputs");
                        graph.number(v); ++inputs;
                    }
                    if (f.inputObjectIds.size()+1!=f.referencedObjectIds.size()) throw std::runtime_error("formula input/output count mismatch");
                }
                const auto verifyOwner=[&](const auto& owner) {
                    for (auto id : owner.distanceParameterIds)
                        if (model.distanceParameters.at(id).ownerId!=owner.id) throw std::runtime_error("owner distance list mismatch");
                    for (auto id : owner.formulaBindingIds)
                        if (model.formulaBindings.at(id).ownerId!=owner.id) throw std::runtime_error("owner formula list mismatch");
                };
                std::size_t ownedDistances=0,ownedFormulas=0;
                for (const auto& d : definitions) { verifyOwner(d); ownedDistances+=d.distanceParameterIds.size(); ownedFormulas+=d.formulaBindingIds.size(); }
                for (const auto& c : model.components) { verifyOwner(c); ownedDistances+=c.distanceParameterIds.size(); ownedFormulas+=c.formulaBindingIds.size(); }
                if (ownedDistances!=distanceIds.size() || ownedFormulas!=formulaIds.size()) throw std::runtime_error("owner lists omit variables");
                const auto& epoxy=model.distanceParameters.at(112715); const auto& lintel=model.distanceParameters.at(32158);
                if (epoxy.ownerId!=112580 || epoxy.name!="D5" || epoxy.label!="Plate Thickness" || epoxy.storedDistance!=12.7 ||
                    lintel.ownerId!=31989 || lintel.name!="D1" || lintel.label!="Lintel length")
                    throw std::runtime_error("dialog distance evidence disagrees");
                const auto& f=model.formulaBindings.at(112771);
                if (f.targetObjectId!=112718 || f.propertyName!="proVALUE" || f.expression!="P6+D5" ||
                    f.inputObjectIds!=std::vector<std::uint32_t>{112624,112715} ||
                    model.parameterDefinitions.at(112624).name!="P6" || model.distanceParameters.at(112718).name!="D6")
                    throw std::runtime_error("formula P6+D5 binding/input evidence disagrees");
                if (distanceIds.size()!=6838 || formulaIds.size()!=10450 || bound!=13676 || references!=24442 || inputs!=13992 || crossOwner!=360)
                    throw std::runtime_error("component variable graph changed");
                std::cout << "distances=6838 formulas=10450 bound=13676 references=24442 inputs=13992 cross_owner=360 fingerprint="
                          << std::hex << graph.value << std::dec << '\n';
            }
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
                if (!project.environment || project.optionsDatabases.size()!=2) throw std::runtime_error("project DBV semantics missing");
                // Independent documented example, not a claim that every DBV
                // flag or the training model's whole environment is verified.
                // https://support.tekla.com/doc/tekla-structures/2025/sys_objects_inp_properties
                const auto& attributes=project.environment->attributes;
                const auto locked=std::find_if(attributes.begin(),attributes.end(),[](const auto& e){ return e.second.name=="OBJECT_LOCKED"; });
                if (locked==attributes.end() || locked->second.label!="j_Locked" || locked->second.choices.size()<3 ||
                    locked->second.choices[0].label!="" || locked->second.choices[1].label!="j_No" || locked->second.choices[2].label!="j_Yes")
                    throw std::runtime_error("OBJECT_LOCKED differs from documented option labels/order");
                if (project.attributeDefinitionAssociations.empty()) throw std::runtime_error("no DB1/environment name/type associations");
                for (const auto& link:project.attributeDefinitionAssociations)
                {
                    const auto& property=project.model.properties.at(link.modelObjectId).at(link.propertyIndex);
                    if (property.name!=attributes.at(link.attributeDefinitionId).name) throw std::runtime_error("bad attribute definition association");
                }
                std::cout<<"documented_attribute_choices=3 model_attribute_definition_links="<<project.attributeDefinitionAssociations.size()<<'\n';
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
                std::ifstream vi(path/"attributes"/"new_FAB-PLATE_Without_Dimensions.vi",std::ios::binary);
                if (!vi) throw std::runtime_error("independent view settings missing");
                const std::string settings((std::istreambuf_iterator<char>(vi)),{});
                std::size_t views=0,volumeFields=0,plateAxes=0;
                for (const auto& file:project.drawings)
                {
                    const auto& drawing=file.second; views+=drawing.viewsByContext.size();
                    for (const auto& ref:drawing.modelReferences)
                        if (!drawing.viewsByContext.count(ref.drawingContextId)) throw std::runtime_error("drawing reference context has no view");
                    for (const auto& entry:drawing.viewsByContext)
                    {
                        const auto& view=entry.second;
                        if (!drawing.subject || view.modelGuid!=drawing.subject->modelGuid) throw std::runtime_error("view/subject GUID mismatch");
                        if (view.propertySetName!="new_FAB-PLATE_Without_Dimensions") continue;
                        const auto& v=view.storedAttributeVolume;
                        for (const auto& field:std::map<std::string,double>{{"xmin",v.minX},{"xmax",v.maxX},{"ymin",v.minY},{"ymax",v.maxY},{"depth_neg",v.depthNegative},{"depth_pos",v.depthPositive}})
                        {
                            std::smatch match;
                            if (!std::regex_search(settings,match,std::regex("(?:^|\\n)"+field.first+" ([^\\r\\n]+)")) ||
                                std::abs(std::stod(match[1].str())-field.second)>1e-6)
                                throw std::runtime_error("view volume differs from saved Tekla VI settings: "+field.first);
                            ++volumeFields;
                        }
                        for (const auto& link:project.drawingSubjectAssociations)
                            if (link.drawing==file.first)
                            {
                                const auto& part=project.model.parts.at(link.modelObjectId);
                                for (std::size_t i=0;i<3;++i)
                                    if (std::abs(part.axis[i]-view.viewCoordinates.axisX[i])>1e-6)
                                        throw std::runtime_error("plate drawing view X axis differs from model part axis");
                                ++plateAxes;
                            }
                    }
                }
                if (views!=19 || volumeFields!=6 || plateAxes!=1 || project.drawingSubjectAssociations.size()!=4)
                    throw std::runtime_error("view/subject public evidence count mismatch");
                for (const auto& link:project.drawingModelAssociations)
                    if (!project.drawings.at(link.drawing).viewsByContext.count(link.drawingContextId))
                        throw std::runtime_error("project drawing model link lost its view context");
                std::cout<<"drawing_views="<<views<<" subject_links="<<project.drawingSubjectAssociations.size()
                         <<" independent_vi_volume_fields="<<volumeFields<<" model_plate_axis_checks="<<plateAxes<<'\n';
            }
        }
        else if (mode=="legacy_drawing_evidence")
        {
            tekla::db1::Model model;
            if (!tekla::db1::parseModelFile(path.parent_path()/"PSDBIM__EXCEL-1246A-BLDG-B"/"EXCEL-1246A-BLDG-B.db1",model,error))
                throw std::runtime_error(error);
            std::size_t files=0,subjects=0,matches=0,unresolved=0;
            for (const auto& file:std::filesystem::directory_iterator(path))
            {
                if (file.path().extension()!=".dg") continue;
                tekla::Drawing d; if (!tekla::parseDrawing(file.path(),d,error)) throw std::runtime_error(error);
                ++files;
                if (!d.projectGuid.empty()) throw std::runtime_error("legacy project GUID unexpectedly inferred");
                if (d.subject && d.subject->kind!=tekla::DrawingSubjectKind::GeneralArrangement)
                {
                    const auto& s=*d.subject; bool found=false;
                    if (s.kind==tekla::DrawingSubjectKind::SinglePart)
                        found=model.parts.count(s.modelObjectId) && model.parts.at(s.modelObjectId).internalType==2;
                    if (s.kind==tekla::DrawingSubjectKind::Assembly)
                        found=std::any_of(model.assemblies.begin(),model.assemblies.end(),[&](const auto& a){return a.id==s.modelObjectId;});
                    if (!found) throw std::runtime_error("legacy subject ID/type disagrees with accompanying DB1");
                    ++subjects;
                }
                for (const auto& ref:d.modelReferences) (model.parts.count(ref.modelObjectId)?matches:unresolved)++;
            }
            // Cross-file ID/type evidence, NOT independent geometric truth or
            // permission to associate an arbitrary database with these drawings.
            if (files!=293 || subjects!=59 || matches!=1361 || unresolved!=35)
                throw std::runtime_error("legacy drawing cross-file evidence changed");
            std::cout<<"files="<<files<<" subject_id_type_matches="<<subjects<<" numeric_reference_matches="<<matches<<" unresolved_numeric_references="<<unresolved<<'\n';
        }
        else if (mode=="raw")
        {
            tekla::db1::RawDatabase raw;
            tekla::db1::RawDatabaseOptions options; options.retainDecompressedFileImage=true;
            if (!tekla::db1::parseRawDatabase(path,raw,error,options)) { std::cerr << error << '\n'; return 1; }
            if (raw.kind==tekla::db1::DatabaseKind::Drawing && raw.storageVersion=="7.82")
            {
                auto rebuilt=raw.preamble;
                const auto word=[&](std::uint32_t n) { for (unsigned i=0;i<4;++i) rebuilt.push_back(static_cast<std::uint8_t>(n>>(i*8))); };
                for (const auto& t:raw.tables)
                {
                    word(0xdbcec066); word(t.payloadSize); word(static_cast<std::uint32_t>(t.fieldDescriptors.size()));
                    for (auto f:t.fieldDescriptors) word(f);
                    for (const auto& r:t.records)
                    {
                        if (r.payload.size()!=t.payloadSize || r.allocatorMetadata.size()!=40) throw std::runtime_error("legacy record framing lost");
                        rebuilt.push_back(r.allocationTag); rebuilt.insert(rebuilt.end(),r.payload.begin(),r.payload.end());
                        rebuilt.insert(rebuilt.end(),r.allocatorMetadata.begin(),r.allocatorMetadata.end());
                    }
                    rebuilt.insert(rebuilt.end(),t.trailer.begin(),t.trailer.end());
                }
                if (rebuilt!=raw.decompressedFileImage) throw std::runtime_error("legacy drawing raw reconstruction differs from file");
            }
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
        else if (mode=="environment" || mode=="options")
        {
            Fingerprint hash;
            const auto value=[&](const tekla::StoredValue& v) {
                hash.number(v.index());
                std::visit([&](const auto& x) {
                    using T=std::decay_t<decltype(x)>;
                    if constexpr(std::is_same_v<T,std::string>) hash.text(x);
                    else if constexpr(std::is_same_v<T,double>) hash.real(x);
                    else hash.number(static_cast<std::uint64_t>(x));
                },v);
            };
            if (mode=="environment")
            {
                tekla::EnvironmentDatabase db;
                if (!tekla::parseEnvironmentDatabase(path,db,error)) { std::cerr<<error<<'\n'; return 1; }
                std::size_t links=0,choices=0;
                for (const auto& entry:db.objectClasses)
                {
                    hash.number(entry.first); hash.text(entry.second.name);
                    for (auto id:entry.second.attributeIds) { hash.number(id); ++links; }
                }
                for (const auto& entry:db.metadata) { hash.number(entry.first); for (auto x:entry.second.fields) hash.number(x); }
                for (const auto& entry:db.attributes)
                {
                    const auto& a=entry.second;
                    hash.number(a.id); hash.text(a.name); hash.text(a.label); hash.number(static_cast<unsigned>(a.storageKind));
                    hash.number(a.metadataId); if (a.storedValue) value(*a.storedValue);
                    for (const auto& v:a.additionalNumericFields) value(v);
                    for (const auto& c:a.choices) { hash.number(c.id); hash.number(c.index); hash.number(c.integerValue); hash.text(c.label); ++choices; }
                }
                std::cout<<"classes="<<db.objectClasses.size()<<" attributes="<<db.attributes.size()<<" metadata="<<db.metadata.size()
                         <<" links="<<links<<" choices="<<choices;
            }
            else
            {
                tekla::OptionsDatabase db;
                if (!tekla::parseOptionsDatabase(path,db,error)) { std::cerr<<error<<'\n'; return 1; }
                std::array<unsigned,4> counts{}; unsigned differences=0;
                for (const auto& entry:db.options)
                {
                    const auto& o=entry.second; hash.number(o.id); hash.text(o.name); hash.number(o.flags);
                    ++counts[static_cast<unsigned>(o.kind)]; differences+=o.valueSlots[0]!=o.valueSlots[1];
                    for (const auto& v:o.valueSlots) value(v);
                }
                std::cout<<"options="<<db.options.size()<<" bool="<<counts[0]<<" int="<<counts[1]<<" real="<<counts[2]<<" string="<<counts[3]<<" differing="<<differences;
            }
            std::cout<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
        }
        else if (mode=="drawing")
        {
            tekla::Drawing drawing;
            if (!tekla::parseDrawing(path,drawing,error)) { std::cerr<<error<<'\n'; return 1; }
            const bool old=drawing.raw.storageVersion=="7.82";
            Fingerprint hash;
            std::size_t incomplete=0;
            for (const auto& entry:drawing.strings)
            {
                hash.number(entry.first); hash.text(entry.second.text);
                if (old) { hash.number(entry.second.complete); hash.number(entry.second.missingContinuationId); incomplete+=!entry.second.complete; }
            }
            for (const auto& entry:drawing.properties)
            {
                const auto& p=entry.second; hash.number(p.id); hash.text(p.name); hash.number(p.isString);
                if (p.isString) hash.text(p.stringValue); else hash.real(p.numericValue);
            }
            for (const auto& link:drawing.propertyLinks) { hash.number(link.id); hash.number(link.propertyId); hash.number(link.ownerId); }
            for (const auto& sheet:drawing.sheets) { hash.number(sheet.id); hash.real(sheet.width); hash.real(sheet.height); }
            for (const auto& ref:drawing.modelReferences) { hash.number(ref.recordId); hash.number(ref.drawingContextId); hash.text(ref.modelGuid); if (old) hash.number(ref.modelObjectId); }
            if (drawing.subject) { hash.number(drawing.subject->recordId); hash.number(drawing.subject->typeCode); hash.text(drawing.subject->modelGuid); if (old) hash.number(drawing.subject->modelObjectId); }
            for (const auto& entry:old?drawing.viewsByRecordId:drawing.viewsByContext)
            {
                const auto& view=entry.second;
                hash.number(view.recordId); hash.number(view.contextId); hash.text(view.modelGuid); hash.text(view.propertySetName);
                if (old) for (const auto& name:view.storedPropertySetNames) hash.text(name);
                for (const auto* cs:{&view.viewCoordinates,&view.displayCoordinates})
                    for (const auto* p:{&cs->origin,&cs->axisX,&cs->axisY,&cs->axisZ}) for (auto x:*p) hash.real(x);
                for (const auto* v:{&view.restriction,&view.storedAttributeVolume})
                    for (auto x:{v->minX,v->maxX,v->minY,v->maxY,v->depthNegative,v->depthPositive}) hash.real(x);
            }
            if (old) for (auto id:drawing.unhandledViewRecordIds) hash.number(id);
            std::cout<<"version="<<drawing.raw.storageVersion<<" tables="<<drawing.raw.tables.size()
                     <<" strings="<<drawing.strings.size()<<" properties="<<drawing.properties.size()
                     <<" links="<<drawing.propertyLinks.size()<<" sheets="<<drawing.sheets.size()
                     <<" model_refs="<<drawing.modelReferences.size()
                     <<" views="<<(old?drawing.viewsByRecordId.size():drawing.viewsByContext.size())<<" subject="<<bool(drawing.subject)
                     <<" guid="<<drawing.projectGuid<<" stored="<<drawing.storedFileName;
            if (old) std::cout<<" incomplete_text="<<incomplete<<" unhandled_views="<<drawing.unhandledViewRecordIds.size()<<" unique_contexts="<<drawing.viewsByContext.size();
            std::cout<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
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
