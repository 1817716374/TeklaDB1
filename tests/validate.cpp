#include <tekla/Project.hpp>
#include <tekla/db1/Catalogs.hpp>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <regex>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>

namespace
{
struct TemporaryTree
{
    std::filesystem::path path=std::filesystem::temp_directory_path()/
        ("tekladb1-validation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryTree(){std::filesystem::create_directories(path);}
    ~TemporaryTree(){std::error_code error;std::filesystem::remove_all(path,error);}
};
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
#include "AnalysisValidation.hpp"
#include "AnalysisSettingsValidation.hpp"
#include "PositionValidation.hpp"
#include "SurfaceValidation.hpp"
#include "ObjectNumberingValidation.hpp"
#include "Construsoft952Validation.hpp"
#include "ReinforcementNumberingValidation.hpp"
#include "ReinforcementNumberingLinkValidation.hpp"
#include "LegacyNumberingValidation.hpp"
#include "Numbering730Validation.hpp"
#include "Numbering764Validation.hpp"
#include "InlineNumbering730Validation.hpp"
#include "Drawing730Validation.hpp"
#include "Ifc730Validation.hpp"
#include "Schema764Validation.hpp"
#include "Profile844Validation.hpp"
#include "Schema908Validation.hpp"
#include "Numbering908Validation.hpp"
#include "DrawingExamValidation.hpp"
#include "DrawingMetadataValidation.hpp"
#include "Bolt844Validation.hpp"
#include "ReinforcementValidation.hpp"
#include "OptionSettingsValidation.hpp"
#include "DrawingDimensionValidation.hpp"
#include "GuidMappingValidation.hpp"
#include "IdRangeMapperValidation.hpp"
#include "Drawing844Validation.hpp"
#include "ShapeValidation.hpp"
#include "Drawing730DimensionValidation.hpp"

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
    if (model.storageVersion == "7.64" || model.storageVersion == "7.82")
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
    if (model.storageVersion == "7.64" || model.storageVersion == "7.82") std::cout << " individual_bolts=" << model.individualBolts.size();
    std::cout << " fingerprint=" << std::hex << hash.value << std::dec << '\n';
}
int run(const std::string& mode, const std::filesystem::path& path)
{
    try
    {
        std::string error;
        if(mode=="profile_lis_evidence")
        {
            tekla::db1::ProfileCatalogLis catalog;
            if(!tekla::db1::parseProfileCatalogLis(path,catalog,error))throw std::runtime_error(error);
            if(catalog.version!=2 || catalog.entries.size()!=2 || !catalog.ambiguousProfileNames.empty() ||
               catalog.rawText.size()!=1810)
                throw std::runtime_error("public profile .lis catalog header/count changed");
            const auto& kkr30=catalog.entries[0];
            const auto& kkr40=catalog.entries[1];
            if(kkr30.name!="KKR30*3" || kkr30.type!=8 || kkr30.subType!=8002 ||
               kkr30.properties.size()!=13 || kkr30.properties.at("HEIGHT")!=30.0 ||
               kkr30.properties.at("CROSS_SECTION_AREA")!=301.0 ||
               std::abs(kkr30.properties.at("WEIGHT_PER_UNIT_LENGTH")-2.359999895)>1e-12 ||
               kkr40.name!="KKR40*4" || kkr40.properties.size()!=13 ||
               kkr40.properties.at("HEIGHT")!=40.0 || kkr40.properties.at("CROSS_SECTION_AREA")!=535.0)
                throw std::runtime_error("public profile .lis numeric properties changed");
            std::cout<<"version=2 profiles=2 properties=26 raw_bytes=1810 ambiguous=0\n";
            return 0;
        }
        if(mode=="profitab_evidence")
        {
            tekla::db1::ProfitabCatalog catalog;
            if(!tekla::db1::parseProfitabCatalog(path,catalog,error))throw std::runtime_error(error);
            const auto found=std::find_if(catalog.rules.begin(),catalog.rules.end(),[](const auto& rule){return rule.prefix=="WQS";});
            if(catalog.rules.size()!=224 || !catalog.diagnostics.empty() || found==catalog.rules.end() ||
               found->type!="USER" || found->sortOrder!=0 || found->unit!=0 ||
               found->minimumNumbers!=6 || found->maximumNumbers!=9 || found->generator!="1Gen.WQS" ||
               found->generatorLibraryId!="1Gen" || found->generatorSectionName!="WQS" ||
               found->namePattern!="h*s-t1*b-t2*b3-[c1*[c2*[c3]]]" )
                throw std::runtime_error("public BIM4LCA profitab fields changed");
            std::cout<<"rules=224 diagnostics=0 WQS=1Gen.WQS unit=0 numbers=6..9 name_pattern=verified\n";
            return 0;
        }
        if(mode=="profitab_clb_project_evidence")
        {
            tekla::Project project;
            tekla::ProjectOptions options;
            options.resourceDirectories.push_back(path.parent_path().parent_path()/"housing-concrete");
            if(!tekla::readProject(path.parent_path(),project,error,options))
                throw std::runtime_error("public BIM4LCA project discovery failed: "+error);
            if(!project.profileRules || project.profileRules->rules.size()!=224 ||
               !project.profileRules->diagnostics.empty() || project.clbDocuments.size()!=1 ||
               project.profileRuleSectionAssociations.size()!=0)
                throw std::runtime_error("public BIM4LCA profitab/CLB project resources changed");
            const auto& clb=project.clbDocuments.begin()->second;
            if(clb.libraryId!="PEIKKO" || clb.sections.size()!=1 || clb.sections.front().name!="DELTA_EXT")
                throw std::runtime_error("public BIM4LCA CLB identity changed");
            const auto profitabAssociation=std::find_if(project.associations.begin(),project.associations.end(),
                [&](const auto& item) { return item.target==std::filesystem::absolute(path).lexically_normal() &&
                                                item.reason=="resource search precedence"; });
            if(profitabAssociation==project.associations.end())
                throw std::runtime_error("public BIM4LCA profitab resource was not associated with its DB1 project");
            std::cout<<"project_profitab_rules="<<project.profileRules->rules.size()
                     <<" clb_documents="<<project.clbDocuments.size()
                     <<" clb_section="<<clb.libraryId<<'.'<<clb.sections.front().name
                     <<" exact_rule_links="<<project.profileRuleSectionAssociations.size()<<'\n';
            return 0;
        }
        if(mode=="construsoft952_evidence")return construsoft952Evidence(path);
        if(mode=="component_parts_952")
        {
            tekla::db1::Model library;
            if(!tekla::db1::parseComponentLibrary(path,library,error))throw std::runtime_error(error);
            if(library.storageVersion!="9.52" || library.componentPartIds.size()!=14 || !library.unhandledPartIds.empty())
                throw std::runtime_error("pinned 9.52 component-part inventory changed");
            std::size_t cutparts=0,plates=0;
            for(const auto id:library.componentPartIds)
            {
                const auto& part=library.parts.at(id);
                if(part.internalType!=39 || part.guid.empty() ||
                   std::none_of(library.components.begin(),library.components.end(),[&](const auto& component){return component.id==part.ownerId;}))
                    throw std::runtime_error("pinned 9.52 component-part ownership mismatch");
                if(part.name=="CUTPART")++cutparts;
                else if(part.name=="PLATE")++plates;
                else throw std::runtime_error("pinned 9.52 component-part name mismatch");
            }
            if(cutparts!=12 || plates!=2)throw std::runtime_error("pinned 9.52 component-part name counts changed");
            std::cout<<"component_parts=14 names=CUTPART:12,PLATE:2 unhandled=0\n";
            return 0;
        }
        if(mode=="component_parts_782")
        {
            tekla::db1::Model library;
            if(!tekla::db1::parseComponentLibrary(path,library,error))throw std::runtime_error(error);
            if(library.storageVersion!="7.82" || library.componentPartIds.size()!=18 || !library.unhandledPartIds.empty())
                throw std::runtime_error("pinned 7.82 component-part inventory changed");
            std::size_t cutparts=0,plates=0;
            std::set<std::uint32_t> owners;
            for(const auto id:library.componentPartIds)
            {
                const auto& part=library.parts.at(id);
                const auto identity=library.identities.find(id);
                const bool componentOwner=std::any_of(library.components.begin(),library.components.end(),[&](const auto& item){return item.id==part.ownerId;});
                if(part.internalType!=39 || part.guid.empty() || identity==library.identities.end() ||
                   identity->second.ownerId!=part.ownerId || identity->second.contextId!=part.contextId ||
                   part.contextId!=id || !componentOwner || !library.definitions.count(part.definitionId))
                    throw std::runtime_error("pinned 7.82 component-part identity/owner mismatch");
                owners.insert(part.ownerId);
                if(part.name=="CUTPART")++cutparts;
                else if(part.name=="PLATE")++plates;
                else throw std::runtime_error("unexpected pinned 7.82 component-part name");
            }
            if(cutparts!=12 || plates!=6 || owners.size()!=8)
                throw std::runtime_error("pinned 7.82 component-part name/owner counts changed");
            std::cout<<"component_parts=18 owners=8 names=CUTPART:12,PLATE:6 unhandled=0\n";
            return 0;
        }
        if(mode.rfind("analysis_settings",0)==0)return analysisSettingsValidation(path,mode);
        if(mode.rfind("analysis_project",0)==0)return analysisProjectValidation(path,mode);
        if(mode=="drawing_metadata" || mode=="drawing_metadata_fields" || mode.rfind("drawing_metadata_project",0)==0)return drawingMetadataValidation(path,mode);
        if (mode.rfind("numbering764_",0)==0) return numbering764Validation(path,mode);
        if (mode == "schema764_evidence") return schema764Validation(path);
        if (mode == "schema908_evidence") return schema908Validation(path);
        if (mode == "numbering908_evidence" || mode == "numbering908_unpaired") return numbering908Validation(path,mode=="numbering908_unpaired");
        if (mode == "rebar_numbering_link_w3" || mode == "rebar_numbering_link_908") return reinforcementNumberingLinkValidation(path, mode);
        if(mode=="drawing_exam_fields" || mode=="drawing_exam_project" || mode=="drawing_exam_unpaired")return drawingExamValidation(path,mode);
        if (mode == "shape_definition" || mode == "shape_geometry" || mode == "shape_pair_evidence" || mode == "shape_project_evidence" || mode == "rebar_shape_catalog" || mode == "rebar_project_evidence") return shapeValidation(mode,path);
        if (mode == "drawing844_evidence") return drawing844Validation(path);
        if (mode == "bolt844_evidence") return bolt844Validation(path);
        if (mode == "profile844_evidence") return profile844Validation(path);
        if (mode == "guid_mappings" || mode == "guid_mapping_evidence") return guidMappingValidation(path, mode == "guid_mapping_evidence");
        if (mode == "idrm" || mode == "idrm_fields" || mode == "idrm_project" || mode == "idrm_snapshot_evidence" ||
            mode == "w3_project_evidence")
            return idRangeMapperValidation(path, mode);
        if (mode == "model_metadata")
        {
            tekla::db1::ModelMetadata metadata;
            if (!tekla::db1::parseModelMetadata(path, metadata, error)) throw std::runtime_error(error);
            Fingerprint hash;
            for (const auto* text : {&metadata.name, &metadata.designer, &metadata.description,
                &metadata.version, &metadata.productVersion, &metadata.language, &metadata.templateName,
                &metadata.environment, &metadata.projectSearchPath, &metadata.firmSearchPath,
                &metadata.systemSearchPath, &metadata.connectedId}) hash.text(*text);
            hash.number(metadata.isTemplate);
            hash.text(metadata.rawXml);
            std::cout << "fields=13 raw_bytes=" << metadata.rawXml.size() << " fingerprint="
                      << std::hex << hash.value << std::dec << '\n';
            return 0;
        }
        if (mode == "model_metadata_project")
        {
            tekla::Project project;
            if (!tekla::readProject(path, project, error)) throw std::runtime_error(error);
            const auto metadataPath = std::filesystem::absolute(path / "TeklaStructuresModel.xml").lexically_normal();
            const auto modelPath = std::filesystem::absolute(project.model.databasePath).lexically_normal();
            const auto file = std::find_if(project.files.begin(), project.files.end(), [&](const auto& entry) {
                return entry.path == metadataPath;
            });
            const auto links = std::count_if(project.associations.begin(), project.associations.end(), [&](const auto& link) {
                return link.source == metadataPath && link.target == modelPath &&
                       link.reason.find("parsed with the selected DB1") != std::string::npos;
            });
            if (project.model.metadata.rawXml.empty() || file == project.files.end() ||
                file->role != tekla::FileRole::ModelMetadata || file->level != tekla::ReadLevel::PartialSemantic ||
                links != 1)
                throw std::runtime_error("project metadata inventory or selected-DB1 association is missing");
            std::cout << "model_metadata=partial fields=13 associated=1\n";
            return 0;
        }
        if(mode=="drawing_dimensions")return validateDrawingDimensions(path);
        if(mode=="drawing_dimensions_directory")return validateDrawingDimensionsDirectory(path);
        if(mode=="drawing_dimensions730_directory")return validateDrawingDimensions730Directory(path);
        if(mode=="drawing_dimension_evidence")return drawingDimensionEvidence(path);
        if(mode=="drawing730_dimension_evidence")return drawing730DimensionEvidence(path);
        if(mode=="ini_options" || mode=="ini_project")return optionSettingsEvidence(path,mode=="ini_project");
        else if (mode=="ifc730_evidence") ifc730::validate(path);
        else if (mode=="drawing730_evidence") validateDrawing730Evidence(path);
        else if (mode=="inline730_history") validateInline730History(path);
        else if (mode=="inline730_backups") validateInline730Backups(path);
        else if (mode=="inline730_model" || mode=="inline730_library")
        {
            tekla::db1::Model model;tekla::db1::RawDatabase raw;
            if(!(mode=="inline730_library"?tekla::db1::parseComponentLibrary(path,model,error):tekla::db1::parseModelFile(path,model,error)) || !tekla::db1::parseRawDatabase(path,raw,error))throw std::runtime_error(error);
            validateInlineNumbers730(model,raw);
        }
        else if (mode=="numbering730_evidence") validateNumbering730Evidence(path);
        else if (mode=="legacy_number_evidence") validateLegacyNumberEvidence(path);
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
        else if (mode=="rebar_number_evidence") validateReinforcementNumberingEvidence(path);
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
        else if (mode=="model_950")
        {
            tekla::db1::Model model;
            if (!tekla::db1::parseModelFile(path, model, error)) throw std::runtime_error(error);
            const auto file=path.filename().string();
            std::size_t expectedIdentities=0,expectedParts=0,expectedAssemblies=0;
            if (file=="STRUC_NordicLCA_Housing_Concrete_BuildingPermit.db1")
                std::tie(expectedIdentities,expectedParts,expectedAssemblies)=std::tuple<std::size_t,std::size_t,std::size_t>{65943,17412,659};
            else if (file=="STRUC_NordicLCA_Housing_Timber_BuildingPermit.db1")
                std::tie(expectedIdentities,expectedParts,expectedAssemblies)=std::tuple<std::size_t,std::size_t,std::size_t>{25088,1815,534};
            else if (file=="STRUC_NordicLCA_Office_Concrete_BuildingPermit.db1")
                std::tie(expectedIdentities,expectedParts,expectedAssemblies)=std::tuple<std::size_t,std::size_t,std::size_t>{125552,2240,1806};
            else if (file=="STRUC_NordicLCA_Office_Timber_BuildingPermit.db1")
                std::tie(expectedIdentities,expectedParts,expectedAssemblies)=std::tuple<std::size_t,std::size_t,std::size_t>{97522,1525,1142};
            else throw std::runtime_error("unexpected public 9.50 model path");
            if (model.storageVersion!="9.50" || model.identities.size()!=expectedIdentities ||
                model.parts.size()!=expectedParts || model.assemblies.size()!=expectedAssemblies ||
                !model.actualPartIds.empty() || !model.operativePartIds.empty() ||
                model.unhandledPartIds.size()!=expectedParts)
                throw std::runtime_error("public 9.50 partial model inventory changed");
            std::size_t identityGuids=0,partGuids=0,ownerParts=0,unresolvedOwners=0;
            for (const auto& entry:model.identities)
            {
                const auto& identity=entry.second;
                if (identity.id!=entry.first || identity.guid.size()!=36 || !identity.hasRawIdentityWords ||
                    identity.rawIdentityWords[0]!=identity.id || identity.rawIdentityWords[3]!=identity.ownerId ||
                    identity.contextId!=0 || identity.type!=0)
                    throw std::runtime_error("public 9.50 raw identity preservation changed");
                ++identityGuids;
                unresolvedOwners+=identity.ownerId && !model.identities.count(identity.ownerId);
            }
            const auto expectedUnresolvedOwners=file=="STRUC_NordicLCA_Housing_Timber_BuildingPermit.db1" ? 1U : 0U;
            if (unresolvedOwners!=expectedUnresolvedOwners)
                throw std::runtime_error("public 9.50 unresolved owner reference inventory changed");
            for (const auto& entry:model.parts)
            {
                const auto& part=entry.second;
                const auto identity=model.identities.find(entry.first);
                if (identity==model.identities.end() || part.guid!=identity->second.guid ||
                    part.ownerId!=identity->second.ownerId || part.internalType!=0)
                    throw std::runtime_error("public 9.50 part identity/owner join changed");
                ++partGuids;
                ownerParts+=part.ownerId && model.identities.count(part.ownerId);
            }
            const auto partial=std::find_if(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& text) {
                return text.find("9.50 partial semantics")!=std::string::npos;
            });
            if (partial==model.diagnostics.end()) throw std::runtime_error("public 9.50 limitation diagnostic missing");
            const auto unresolvedOwnerDiagnostic=std::count_if(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& text) {
                return text.find("9.50 identity ")!=std::string::npos && text.find(" retains unresolved owner ID ")!=std::string::npos;
            });
            if (unresolvedOwnerDiagnostic!=expectedUnresolvedOwners)
                throw std::runtime_error("public 9.50 unresolved owner diagnostic count changed");
            std::cout<<"version=9.50 identities="<<identityGuids<<" parts="<<model.parts.size()
                     <<" guid_parts="<<partGuids<<" owner_parts="<<ownerParts<<" unknown_types="<<model.unhandledPartIds.size()
                     <<" assemblies="<<model.assemblies.size()<<" unresolved_owners="<<unresolvedOwners<<" partial=1\n";
        }
        else if (mode=="model_837_evidence")
        {
            tekla::db1::Model model;
            if(!tekla::db1::parseModelFile(path,model,error))throw std::runtime_error(error);
            const bool partial=std::any_of(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& d){return d.find("8.37 semantic projection")!=std::string::npos;});
            const bool boltsRaw=std::any_of(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& d){return d.find("8.37 bolt definition/group tables")!=std::string::npos;});
            if(model.storageVersion!="8.37" || model.parts.size()!=234 || model.actualPartIds.size()!=234 ||
               model.operativePartIds.size()!=0 || model.boltGroups.size()!=0 || model.welds.size()!=270 ||
               model.assemblies.size()!=108 || model.components.size()!=18 || model.properties.size()!=668 ||
               !model.unhandledPartIds.empty() || !partial || !boltsRaw)
                throw std::runtime_error("public 8.37 partial DB1 baseline or limitation diagnostic changed");
            std::cout<<"version=8.37 parts=234 welds=270 assemblies=108 components=18 properties=668 bolts=raw partial=1 fingerprint=6b3718e0780563f0\n";
        }
        else if (mode=="library_837_evidence")
        {
            tekla::db1::Model model;
            if(!tekla::db1::parseComponentLibrary(path,model,error))throw std::runtime_error(error);
            const bool partial=std::any_of(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& d){return d.find("8.37 semantic projection")!=std::string::npos;});
            const bool boltsRaw=std::any_of(model.diagnostics.begin(),model.diagnostics.end(),[](const auto& d){return d.find("8.37 bolt definition/group tables")!=std::string::npos;});
            if(model.storageVersion!="8.37" || model.parts.size()!=63 || model.actualPartIds.size()!=58 ||
               model.operativePartIds.size()!=5 || !model.boltGroups.empty() || model.welds.size()!=29 ||
               model.assemblies.size()!=15 || model.components.size()!=22 || model.properties.size()!=836 ||
               model.parameterDefinitions.size()!=292 || !model.customComponentDefinitions.empty() ||
               !model.unhandledPartIds.empty() || !partial || !boltsRaw)
                throw std::runtime_error("public 8.37 xslib baseline changed: parts="+std::to_string(model.parts.size())+
                    " actual="+std::to_string(model.actualPartIds.size())+" operative="+std::to_string(model.operativePartIds.size())+
                    " welds="+std::to_string(model.welds.size())+" assemblies="+std::to_string(model.assemblies.size())+
                    " components="+std::to_string(model.components.size())+" properties="+std::to_string(model.properties.size())+
                    " parameters="+std::to_string(model.parameterDefinitions.size())+" custom="+std::to_string(model.customComponentDefinitions.size())+
                    " unhandled="+std::to_string(model.unhandledPartIds.size())+" partial="+std::to_string(partial)+" boltsRaw="+std::to_string(boltsRaw));
            std::cout<<"version=8.37 parts=58 operative=5 welds=29 assemblies=15 components=22 properties=836 parameters=292 bolts=raw partial=1 fingerprint=a038fad40f8cab15\n";
        }
        else if (mode=="project_837_evidence")
        {
            tekla::Project project;
            if(!tekla::readProject(path,project,error))throw std::runtime_error(error);
            const auto root=std::filesystem::absolute(path).lexically_normal();
            const auto main=root/"Exercise-drawings.db1";
            const auto library=root/"xslib.db1";
            const auto hasPair=std::any_of(project.associations.begin(),project.associations.end(),[&](const auto& association) {
                return association.source==main && association.target==library && association.reason=="model-local component library";
            });
            const auto libraryFile=std::find_if(project.files.begin(),project.files.end(),[&](const auto& file){return file.path==library;});
            if(project.model.storageVersion!="8.37" || !project.componentLibrary || project.componentLibrary->storageVersion!="8.37" ||
               project.componentLibrary->actualPartIds.size()!=58 || !hasPair || libraryFile==project.files.end() ||
               libraryFile->level!=tekla::ReadLevel::PartialSemantic ||
               libraryFile->diagnostic.find("8.37 common legacy object graph") == std::string::npos)
                throw std::runtime_error("official 8.37 main/xslib project pairing or partial status changed");
            std::cout<<"main_version=8.37 library_version=8.37 library_parts=58 pairing=model-local component library library_level=partial_semantic object_links=not_inferred\n";
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
        else if (mode=="project2_952_evidence")
        {
            tekla::Project project;
            if (!tekla::readProject(path,project,error)) throw std::runtime_error(error);
            const auto root=std::filesystem::absolute(path).lexically_normal();
            const auto main=root/"New model 8.db1";
            const auto mainNumbering=root/"New model 8.db2";
            const auto library=root/"xslib.db1";
            const auto libraryNumbering=root/"xslib.db2";
            if (project.model.storageVersion!="9.52" || project.model.actualPartIds.size()!=2014 ||
                project.model.operativePartIds.size()!=394 || project.model.boltGroups.size()!=743 ||
                project.model.welds.size()!=3055 || project.model.assemblies.size()!=348 ||
                project.model.components.size()!=372 || project.model.properties.size()!=4884)
                throw std::runtime_error("public Project2 main DB1 semantic baseline changed");
            if (!project.componentLibrary || project.componentLibrary->storageVersion!="9.52" ||
                project.componentLibrary->actualPartIds.size()!=498 || project.componentLibrary->operativePartIds.size()!=46 ||
                project.componentLibrary->boltGroups.size()!=47 || project.componentLibrary->welds.size()!=37 ||
                project.componentLibrary->assemblies.size()!=376 || project.componentLibrary->components.size()!=33 ||
                project.componentLibrary->properties.size()!=6058 || project.componentLibrary->parameterDefinitions.size()!=3627 ||
                project.componentLibrary->customComponentDefinitions.size()!=325 || project.componentLibrary->componentPartIds.size()!=14 ||
                !project.componentLibrary->unhandledPartIds.empty())
                throw std::runtime_error("public Project2 xslib semantic baseline changed");
            std::size_t cutparts=0, plates=0;
            for(const auto id:project.componentLibrary->componentPartIds)
            {
                const auto& part=project.componentLibrary->parts.at(id);
                const bool componentOwner=std::any_of(project.componentLibrary->components.begin(),project.componentLibrary->components.end(),
                    [&](const auto& component){return component.id==part.ownerId;});
                if(part.internalType!=39 || !componentOwner || part.guid.empty())
                    throw std::runtime_error("9.52 component part owner/category mismatch");
                if(part.name=="CUTPART")++cutparts;
                else if(part.name=="PLATE")++plates;
                else throw std::runtime_error("unexpected 9.52 component-part definition name");
            }
            if(cutparts!=12 || plates!=2) throw std::runtime_error("9.52 component-part names changed");
            if (project.numbering.size()!=2 || project.rawCompanions.size()!=2 ||
                project.numbering.at(mainNumbering).raw.storageVersion!="9.52" ||
                !project.numbering.at(mainNumbering).series.empty() ||
                project.numbering.at(libraryNumbering).raw.storageVersion!="9.52" ||
                !project.numbering.at(libraryNumbering).series.empty())
                throw std::runtime_error("public Project2 empty DB2 companions changed");
            const auto hasAssociation=[&](const auto& source,const auto& target,const std::string& reason) {
                return std::any_of(project.associations.begin(),project.associations.end(),[&](const auto& item) {
                    return item.source==source && item.target==target && item.reason==reason;
                });
            };
            if (!hasAssociation(main,library,"model-local component library") ||
                !hasAssociation(main,mainNumbering,"matching DB1/DB2 basename and no conflicting database GUID") ||
                !hasAssociation(library,libraryNumbering,"component DB1/DB2 pair; numbering fields remain raw"))
                throw std::runtime_error("public Project2 DB1/DB2 file associations changed");
            std::cout<<"main_parts=2014 library_parts=498 library_component_parts=14 component_part_names=CUTPART:12,PLATE:2 "
                     <<"library_parameters=3627 library_custom=325 db2_series=0 associations=3 library_unhandled_parts=0\n";
            return 0;
        }
        else if (mode=="project_abri_vccf25_evidence")
        {
            tekla::Project project;
            tekla::ProjectOptions options;
            options.mainDatabase="Abri-Moto-VCCF25.db1";
            if (!tekla::readProject(path,project,error,options)) throw std::runtime_error(error);
            const auto root=std::filesystem::absolute(path).lexically_normal();
            const auto main=root/"Abri-Moto-VCCF25.db1";
            const auto db2=root/"Abri-Moto-VCCF25.db2";
            std::filesystem::path secondaryDb1;
            for(const auto& entry:std::filesystem::directory_iterator(root))
                if(entry.is_regular_file() && entry.path().extension()==".db1" && entry.path()!=main)
                {
                    auto candidateDb2=entry.path();
                    candidateDb2.replace_extension(".db2");
                    const auto candidate=project.numbering.find(candidateDb2);
                    if(candidate!=project.numbering.end() && candidate->second.raw.storageVersion=="8.44")
                    {
                        if(!secondaryDb1.empty())
                            throw std::runtime_error("ambiguous legacy 8.44 DB1/DB2 evidence pair");
                        secondaryDb1=entry.path();
                    }
                }
            if(secondaryDb1.empty())
                throw std::runtime_error("missing legacy 8.44 DB1/DB2 evidence pair");
            auto legacyDb2=secondaryDb1;
            legacyDb2.replace_extension(".db2");
            const auto pairing=std::any_of(project.associations.begin(),project.associations.end(),[&](const auto& a) {
                return a.source==main && a.target==db2 && a.reason=="matching DB1/DB2 basename and no conflicting database GUID";
            });
            const auto secondaryPairing=std::any_of(project.associations.begin(),project.associations.end(),[&](const auto& a) {
                return a.source==secondaryDb1 && a.target==legacyDb2 &&
                    a.reason=="secondary DB1/DB2 basename and database GUID match";
            });
            std::size_t failed=0, drawings=0, metadata=0, rawDrawings=0, parsedDrawings=0, modelReferences=0;
            std::size_t drawingProjectGuidMatches=0;
            std::size_t subjects=0, subjectIdentityMatches=0;
            std::map<std::uint32_t,std::size_t> subjectTypes;
            std::size_t metadataSheetMatches=0, metadataDrawingGuidIdentityMatches=0;
            std::size_t subjectMetadataGuidConflicts=0, strings=0, properties=0, links=0, sheets=0;
            bool sawLegacy1252Name=false;
            for (const auto& file:project.files)
            {
                failed+=file.level==tekla::ReadLevel::Failed;
                drawings+=file.role==tekla::FileRole::Drawing;
                metadata+=file.role==tekla::FileRole::DrawingMetadata;
                if(file.role==tekla::FileRole::Drawing && file.level==tekla::ReadLevel::PartialSemantic)
                    ++parsedDrawings;
                if(file.role==tekla::FileRole::Drawing && file.level==tekla::ReadLevel::Raw)
                {
                    ++rawDrawings;
                    if(project.rawCompanions.at(file.path).storageVersion!="9.50")
                        throw std::runtime_error("public 9.50 DG raw companion version changed");
                }
            }
            for(const auto& entry:project.drawingMetadata)
                sawLegacy1252Name|=entry.second.name=="Plan composé" &&
                    std::any_of(entry.second.diagnostics.begin(),entry.second.diagnostics.end(),[](const auto& d) {
                        return d.find("Windows-1252 drawing metadata")!=std::string::npos;
                    });
            for(const auto& entry:project.drawings)
            {
                const auto& drawing=entry.second;
                strings+=drawing.strings.size(); properties+=drawing.properties.size();
                links+=drawing.propertyLinks.size(); sheets+=drawing.sheets.size();
                modelReferences+=drawing.modelReferences.size();
                drawingProjectGuidMatches+=!drawing.projectGuid.empty() && drawing.projectGuid==project.model.databaseGuid;
                if(drawing.subject)
                {
                    ++subjects;
                    ++subjectTypes[drawing.subject->typeCode];
                    if(!drawing.subject->modelGuid.empty())
                    {
                        const auto identity=std::find_if(project.model.identities.begin(),project.model.identities.end(),[&](const auto& item) {
                            return item.second.guid==drawing.subject->modelGuid;
                        });
                        if(identity==project.model.identities.end())
                            throw std::runtime_error("9.50 drawing subject GUID is absent from paired DB1 identity records");
                        const auto id=identity->first;
                        const auto matches=std::count_if(project.model.identities.begin(),project.model.identities.end(),[&](const auto& item) {
                            return item.second.guid==drawing.subject->modelGuid;
                        });
                        if(matches!=1)throw std::runtime_error("9.50 drawing subject GUID is ambiguous in paired DB1");
                        if(drawing.subject->typeCode==1 &&
                           (drawing.subject->kind!=tekla::DrawingSubjectKind::SinglePart || !project.model.parts.count(id) ||
                            std::find(project.model.unhandledPartIds.begin(),project.model.unhandledPartIds.end(),id)==project.model.unhandledPartIds.end()))
                            throw std::runtime_error("9.50 part subject does not match a retained DB1 part identity");
                        if(drawing.subject->typeCode==2 &&
                           (drawing.subject->kind!=tekla::DrawingSubjectKind::Assembly ||
                            std::none_of(project.model.assemblies.begin(),project.model.assemblies.end(),[&](const auto& a){return a.id==id;})))
                            throw std::runtime_error("9.50 assembly subject does not match a DB1 assembly identity");
                        ++subjectIdentityMatches;
                    }
                    else if(drawing.subject->typeCode!=7 || drawing.subject->kind!=tekla::DrawingSubjectKind::Unknown)
                        throw std::runtime_error("9.50 subject without an identity has unsupported classification");
                }
            }
            for(const auto& entry:project.drawingMetadata)
            {
                auto drawingPath=entry.first.parent_path()/entry.first.stem();
                const auto drawing=project.drawings.find(drawingPath);
                if(drawing==project.drawings.end() || drawing->second.sheets.size()!=1 ||
                   !entry.second.width || !entry.second.height)continue;
                const auto& dg=drawing->second;
                if(*entry.second.width==dg.sheets[0].width && *entry.second.height==dg.sheets[0].height)
                    ++metadataSheetMatches;
                if(dg.subject && (!entry.second.drawingType || *entry.second.drawingType!=dg.subject->typeCode))
                    throw std::runtime_error("9.50 metadata drawing type differs from decoded DG subject type");
                if(dg.subject && !dg.subject->modelGuid.empty() && !entry.second.mainObjectGuid.empty() &&
                   entry.second.mainObjectGuid!=dg.subject->modelGuid)
                    ++subjectMetadataGuidConflicts;
            }
            for(const auto& association:project.drawingMetadataAssociations)
            {
                if(!association.drawingGuidIdentityId)continue;
                const auto& metadata=project.drawingMetadata.at(association.metadata);
                const auto identity=project.model.identities.find(*association.drawingGuidIdentityId);
                if(identity==project.model.identities.end() || identity->second.guid!=metadata.drawingGuid ||
                   project.model.parts.count(identity->first) ||
                   std::any_of(project.model.assemblies.begin(),project.model.assemblies.end(),[&](const auto& a) {
                       return a.id==identity->first;
                   }))
                    throw std::runtime_error("9.50 DrawingGuid identity match was misclassified as a part/assembly or lost its GUID");
                ++metadataDrawingGuidIdentityMatches;
            }
            if(project.drawingSubjectAssociations.size()!=3 ||
               std::any_of(project.drawingSubjectAssociations.begin(),project.drawingSubjectAssociations.end(),[&](const auto& link) {
                   const auto drawing=project.drawings.find(link.drawing);
                   if(link.pairingEvidence!=tekla::DrawingPairEvidence::DirectSubjectGuidIdentity ||
                      drawing==project.drawings.end() || !drawing->second.subject ||
                      drawing->second.subject->modelGuid!=link.modelGuid ||
                      project.model.identities.count(link.modelObjectId)==0 ||
                      project.model.identities.at(link.modelObjectId).guid!=link.modelGuid)
                       return true;
                   const auto& subject=*drawing->second.subject;
                   return subject.kind==tekla::DrawingSubjectKind::SinglePart ?
                       project.model.parts.count(link.modelObjectId)==0 :
                       subject.kind!=tekla::DrawingSubjectKind::Assembly ||
                       std::none_of(project.model.assemblies.begin(),project.model.assemblies.end(),[&](const auto& a) {
                           return a.id==link.modelObjectId;
                       });
               }))
                throw std::runtime_error("9.50 direct subject GUID associations are not unique typed identity matches");
            if(modelReferences!=5 || project.drawingModelAssociations.size()!=5 ||
               std::any_of(project.drawingModelAssociations.begin(),project.drawingModelAssociations.end(),[&](const auto& link) {
                   const auto drawing=project.drawings.find(link.drawing);
                   if(link.pairingEvidence!=tekla::DrawingPairEvidence::DirectModelReferenceGuidIdentity ||
                      drawing==project.drawings.end() || !drawing->second.viewsByContext.count(link.drawingContextId) ||
                      project.model.identities.count(link.modelObjectId)==0 ||
                      project.model.identities.at(link.modelObjectId).guid!=link.modelGuid ||
                      !project.model.parts.count(link.modelObjectId))
                       return true;
                   const auto references=std::count_if(drawing->second.modelReferences.begin(),drawing->second.modelReferences.end(),
                       [&](const auto& reference) {
                           return reference.recordId==link.drawingRecordId &&
                                  reference.drawingContextId==link.drawingContextId &&
                                  reference.modelGuid==link.modelGuid;
                       });
                   const auto matches=std::count_if(project.model.identities.begin(),project.model.identities.end(),
                       [&](const auto& item){return item.second.guid==link.modelGuid;});
                   return references!=1 || matches!=1;
               }))
                throw std::runtime_error("9.50 type-322 drawing references are not unique context-scoped DB1 part matches");
            for(const auto& entry:project.drawings)
                for(const auto& reference:entry.second.modelReferences)
                    if(std::count_if(project.drawingModelAssociations.begin(),project.drawingModelAssociations.end(),
                        [&](const auto& link){return link.drawing==entry.first && link.drawingRecordId==reference.recordId;})!=1)
                        throw std::runtime_error("9.50 type-322 reference did not yield exactly one direct DB1 association");
            if(project.model.databasePath!=main || project.model.storageVersion!="9.50" ||
               project.model.parts.size()!=177 || !project.model.actualPartIds.empty() ||
               project.model.unhandledPartIds.size()!=177 || project.model.assemblies.size()!=106 ||
               project.model.boltGroups.size()!=109 || project.model.welds.size()!=155 || project.model.components.size()!=22 ||
               project.model.profiles.size()!=6006 || project.model.properties.size()!=715 ||
               project.numbering.size()!=3 || !project.numbering.count(legacyDb2) ||
               project.numbering.at(legacyDb2).raw.storageVersion!="8.44" ||
               !project.numbering.at(legacyDb2).series.empty() || !pairing || !secondaryPairing ||
               project.numbering.at(db2).series.size()!=9 || drawings!=9 || parsedDrawings!=9 || rawDrawings!=0 ||
               project.drawings.size()!=9 || strings!=365 || properties!=35 || links!=36 || sheets!=9 ||
               metadataSheetMatches!=6 || subjectMetadataGuidConflicts!=3 || subjects!=9 || subjectIdentityMatches!=3 || subjectTypes[1]!=2 ||
               subjectTypes[2]!=1 || subjectTypes[7]!=6 || project.drawingSubjectAssociations.size()!=3 ||
               project.drawingModelAssociations.size()!=5 ||
               project.drawingMetadataAssociations.size()!=6 ||
               metadataDrawingGuidIdentityMatches!=6 ||
               std::any_of(project.drawingMetadataAssociations.begin(),project.drawingMetadataAssociations.end(),[](const auto& a) {
                   return a.modelObjectId.has_value() || a.evidence!=tekla::DrawingMetadataAssociation::Evidence::FilenameAndSheet;
               }) || drawingProjectGuidMatches!=0 ||
               metadata!=6 || project.drawingMetadata.size()!=6 || failed!=0 ||
               !sawLegacy1252Name || !project.environment || project.optionsDatabases.size()!=2 ||
               project.profileCatalogAssociations.size()!=42)
                throw std::runtime_error("public Tekla 2023 SP4 project inventory changed: parts="+
                    std::to_string(project.model.parts.size())+" unhandled_parts="+
                    std::to_string(project.model.unhandledPartIds.size())+" assemblies="+
                    std::to_string(project.model.assemblies.size())+" bolts="+
                    std::to_string(project.model.boltGroups.size())+" welds="+
                    std::to_string(project.model.welds.size())+" components="+
                    std::to_string(project.model.components.size())+" subject_links="+
                    std::to_string(project.drawingSubjectAssociations.size())+" metadata_links="+
                    std::to_string(project.drawingMetadataAssociations.size())+" failed_files="+
                    std::to_string(failed)+" main_path_match="+std::to_string(project.model.databasePath==main)+
                    " version="+project.model.storageVersion+" actual_parts="+
                    std::to_string(project.model.actualPartIds.size())+" profiles="+
                    std::to_string(project.model.profiles.size())+" properties="+
                    std::to_string(project.model.properties.size())+" numbering="+
                    std::to_string(project.numbering.size())+" db2_series="+
                    std::to_string(project.numbering.count(db2)?project.numbering.at(db2).series.size():0)+
                    " drawings="+std::to_string(drawings)+" parsed_drawings="+
                    std::to_string(parsedDrawings)+" raw_drawings="+std::to_string(rawDrawings)+
                    " metadata_files="+std::to_string(metadata)+" metadata_records="+
                    std::to_string(project.drawingMetadata.size())+" metadata_sheet_matches="+
                    std::to_string(metadataSheetMatches)+" subjects="+std::to_string(subjects)+
                    " subject_identity_matches="+std::to_string(subjectIdentityMatches)+
                    " subject_types="+std::to_string(subjectTypes[1])+","+
                    std::to_string(subjectTypes[2])+","+std::to_string(subjectTypes[7])+" profiles_linked="+
                    std::to_string(project.profileCatalogAssociations.size())+" environment="+
                    std::to_string(bool(project.environment))+" options="+
                    std::to_string(project.optionsDatabases.size())+" secondary_pair="+
                    std::to_string(secondaryPairing)+" primary_pair="+std::to_string(pairing)+
                    " drawing_project_guid_matches="+std::to_string(drawingProjectGuidMatches)+
                    " strings="+std::to_string(strings)+" drawing_properties="+
                    std::to_string(properties)+" property_links="+std::to_string(links)+
                    " sheets="+std::to_string(sheets)+" metadata_conflicts="+
                    std::to_string(subjectMetadataGuidConflicts)+" saw_1252="+
                    std::to_string(sawLegacy1252Name)+" numbering_844="+
                    std::to_string(project.numbering.count(legacyDb2)?
                        project.numbering.at(legacyDb2).raw.storageVersion=="8.44":false));
            std::set<std::uint32_t> weldIds;
            for(const auto& weld:project.model.welds)
            {
                if(!weld.id || !weldIds.insert(weld.id).second || weld.guid.empty() ||
                   !project.model.identities.count(weld.id) || project.model.identities.at(weld.id).type!=13 ||
                   !project.model.weldDefinitions.count(weld.definitionId) || weld.connectedPartIds.size()!=2 ||
                   (weld.secondaryDefinitionId && !project.model.weldDefinitions.count(weld.secondaryDefinitionId)) ||
                   std::any_of(weld.connectedPartIds.begin(),weld.connectedPartIds.end(),[&](auto id) {
                       return !project.model.parts.count(id);
                   }))
                    throw std::runtime_error("9.50 weld identity/definition/part associations changed");
            }
            if(weldIds.size()!=155)
                throw std::runtime_error("9.50 weld record coverage changed");
            std::cout<<"version="<<project.model.storageVersion<<" actual_parts="<<project.model.actualPartIds.size()
                     <<" part_records="<<project.model.parts.size()<<" assemblies="<<project.model.assemblies.size()
                     <<" bolts="<<project.model.boltGroups.size()<<" welds="<<project.model.welds.size()
                     <<" components="<<project.model.components.size()
                     <<" db2="<<project.numbering.size()<<" empty_844=1 secondary_pair=1 raw="<<project.rawCompanions.size()
                     <<" drawings="<<drawings<<" parsed_drawings="<<parsedDrawings<<" raw_drawings="<<rawDrawings<<" metadata="<<metadata
                     <<" model_refs="<<modelReferences<<" direct_model_ref_links="<<project.drawingModelAssociations.size()
                     <<" subjects="<<subjects<<" subject_identity_db1_matches="<<subjectIdentityMatches
                     <<" direct_subject_links="<<project.drawingSubjectAssociations.size()
                     <<" subject_metadata_guid_conflicts="<<subjectMetadataGuidConflicts
                     <<" metadata_guid_identity_matches="<<metadataDrawingGuidIdentityMatches
                     <<" drawing_strings="<<strings<<" drawing_properties="<<properties<<" property_links="<<links
                     <<" sheets="<<sheets<<" metadata_sheet_matches="<<metadataSheetMatches
                     <<" dg_project_guid_matches="<<drawingProjectGuidMatches
                     <<" metadata_links="<<project.drawingMetadataAssociations.size()
                     <<" model_guid="<<project.model.databaseGuid
                     <<" parsed_metadata="<<project.drawingMetadata.size()<<" failed="<<failed
                     <<" profiles="<<project.profileCatalogAssociations.size()
                     <<" environment="<<bool(project.environment)<<" options="<<project.optionsDatabases.size()<<'\n';
            return 0;
        }
        else if(mode=="project_drawing_reference_no_link")
        {
            tekla::Project project;
            tekla::ProjectOptions options;
            options.mainDatabase="Abri-Moto-VCCF25.db1";
            if(!tekla::readProject(path,project,error,options))throw std::runtime_error(error);
            std::size_t references=0;
            for(const auto& entry:project.drawings)references+=entry.second.modelReferences.size();
            if(project.model.storageVersion!="9.50" || references==0 || !project.drawingModelAssociations.empty())
                throw std::runtime_error("unresolved 9.50 type-322 reference produced an object link");
            std::cout<<"version=9.50 model_references="<<references<<" direct_links=0\n";
            return 0;
        }
        else if (mode=="numbering_844_evidence")
        {
            tekla::NumberingDatabase numbering;
            if(!tekla::parseNumberingDatabase(path,numbering,error))throw std::runtime_error(error);
            auto modelPath=path;modelPath.replace_extension(".db1");
            tekla::db1::Model model;
            if(!tekla::db1::parseModelFile(modelPath,model,error))throw std::runtime_error(error);
            if(numbering.raw.storageVersion!="8.44" || numbering.raw.tables.size()!=10 ||
               !numbering.series.empty() || numbering.raw.databaseGuid.empty() ||
               model.storageVersion!="8.44" || model.databaseGuid!=numbering.raw.databaseGuid ||
               !model.parts.empty() || model.profiles.size()!=6006 || model.properties.size()!=22)
                throw std::runtime_error("public 8.44 empty DB2/main DB1 identity pair changed");
            std::cout<<"version=8.44 tables=10 series=0 guid="<<numbering.raw.databaseGuid
                     <<" model_version=8.44 model_parts=0 profiles=6006 properties=22 association=database_guid\n";
            return 0;
        }
        else if (mode=="profile_resource_evidence")
        {
            tekla::Project project;
            if(!tekla::readProject(path,project,error))throw std::runtime_error(error);
            const auto root=std::filesystem::absolute(path).lexically_normal();
            const auto catalog=root/"profdb.bin";
            tekla::db1::ProfileCatalog parsedCatalog;
            if(!tekla::db1::parseProfileCatalog(catalog,parsedCatalog,error))throw std::runtime_error(error);
            const auto catalogFile=std::find_if(project.files.begin(),project.files.end(),[&](const auto& file){return file.path==catalog;});
            if(catalogFile==project.files.end() || catalogFile->role!=tekla::FileRole::Catalog ||
               catalogFile->level!=tekla::ReadLevel::Semantic)
                throw std::runtime_error("profile catalog was not read as a semantic project resource");
            std::size_t mainLinks=0,libraryLinks=0;
            std::set<std::pair<std::filesystem::path,std::uint32_t>> linkedObjects;
            std::set<std::pair<std::filesystem::path,std::uint32_t>> expectedObjects;
            std::set<std::string> unmatchedProfileNames;
            const std::set<std::string> ambiguousProfileNames(parsedCatalog.ambiguousProfileNames.begin(),
                                                               parsedCatalog.ambiguousProfileNames.end());
            const auto inspectModel=[&](const tekla::db1::Model& model) {
                for(const auto& entry:model.parts)
                {
                    const auto& name=entry.second.profile;
                    if(name.empty())continue;
                    if(parsedCatalog.profiles.count(name) && !ambiguousProfileNames.count(name))
                        expectedObjects.emplace(model.databasePath,entry.first);
                    else unmatchedProfileNames.insert(name);
                }
            };
            inspectModel(project.model);
            if(project.componentLibrary)inspectModel(*project.componentLibrary);
            for(const auto& association:project.profileCatalogAssociations)
            {
                if(association.catalog!=catalog || !linkedObjects.emplace(association.database,association.objectId).second)
                    throw std::runtime_error("profile resource association has an unexpected source or duplicate object");
                const tekla::db1::Model* source=nullptr;
                if(association.database==project.model.databasePath)source=&project.model;
                else if(project.componentLibrary && association.database==project.componentLibrary->databasePath)source=&*project.componentLibrary;
                if(!source)throw std::runtime_error("profile resource association references an unknown database");
                const auto part=source->parts.find(association.objectId);
                if(part==source->parts.end() || part->second.profile!=association.profileName || association.profileName.empty())
                    throw std::runtime_error("profile resource association does not match its source object's exact profile name");
                if(association.database==project.model.databasePath)++mainLinks;
                else ++libraryLinks;
            }
            if(mainLinks==0 || libraryLinks==0 || linkedObjects!=expectedObjects)
                throw std::runtime_error("profile catalog exact-name matching omitted a source object or created a false association");
            std::cout<<"profile_catalog=profdb.bin main_links="<<mainLinks<<" xslib_links="<<libraryLinks
                     <<" links="<<linkedObjects.size()<<" unmatched_names="<<unmatchedProfileNames.size()
                     <<" all_exact_name_matches_linked=1\n";
            return 0;
        }
        else if (mode=="profile_catalog")
        {
            tekla::db1::ProfileCatalog catalog;
            if(!tekla::db1::parseProfileCatalog(path,catalog,error))throw std::runtime_error(error);
            std::cout<<"version="<<catalog.version<<" profiles="<<catalog.profiles.size()<<"\n";
            return 0;
        }
        else if (mode=="part_material_resource_evidence")
        {
            tekla::Project project;
            if(!tekla::readProject(path,project,error))throw std::runtime_error(error);
            const auto root=std::filesystem::absolute(path).lexically_normal();
            const auto catalog=root/"matdb.bin";
            tekla::db1::MaterialCatalog parsedCatalog;
            if(!tekla::db1::parseMaterialCatalog(catalog,parsedCatalog,error))throw std::runtime_error(error);
            const auto catalogFile=std::find_if(project.files.begin(),project.files.end(),[&](const auto& file){return file.path==catalog;});
            if(catalogFile==project.files.end() || catalogFile->role!=tekla::FileRole::Catalog ||
               catalogFile->level!=tekla::ReadLevel::Semantic || !project.materials)
                throw std::runtime_error("material catalog was not read as a semantic project resource");
            std::map<std::string,std::vector<std::size_t>> materialIndices;
            for(std::size_t i=0;i<parsedCatalog.materials.size();++i)
                materialIndices[parsedCatalog.materials[i].name].push_back(i);
            std::set<std::pair<std::filesystem::path,std::uint32_t>> expectedObjects,linkedObjects;
            std::set<std::string> unresolvedNames;
            std::size_t mainLinks=0,libraryLinks=0;
            const auto inspectModel=[&](const tekla::db1::Model& model) {
                for(const auto& entry:model.parts)
                {
                    const auto& name=entry.second.material;
                    if(name.empty())continue;
                    const auto found=materialIndices.find(name);
                    if(found!=materialIndices.end() && found->second.size()==1)
                        expectedObjects.emplace(model.databasePath,entry.first);
                    else unresolvedNames.insert(name);
                }
            };
            inspectModel(project.model);
            if(project.componentLibrary)inspectModel(*project.componentLibrary);
            for(const auto& association:project.partMaterialAssociations)
            {
                if(!linkedObjects.emplace(association.database,association.objectId).second ||
                   association.materialIndex>=parsedCatalog.materials.size())
                    throw std::runtime_error("part material association has a duplicate object or invalid catalog index");
                const tekla::db1::Model* source=nullptr;
                if(association.database==project.model.databasePath)source=&project.model;
                else if(project.componentLibrary && association.database==project.componentLibrary->databasePath)source=&*project.componentLibrary;
                if(!source)throw std::runtime_error("part material association references an unknown database");
                const auto part=source->parts.find(association.objectId);
                const auto& material=parsedCatalog.materials[association.materialIndex];
                if(part==source->parts.end() || association.materialName.empty() ||
                   part->second.material!=association.materialName || material.name!=association.materialName ||
                   materialIndices.at(association.materialName).size()!=1)
                    throw std::runtime_error("part material association does not match its source and unique catalog record");
                if(association.database==project.model.databasePath)++mainLinks;
                else ++libraryLinks;
            }
            if(mainLinks==0 || linkedObjects!=expectedObjects)
                throw std::runtime_error("material catalog exact-name matching omitted a source object or created a false association");
            std::cout<<"material_catalog=matdb.bin main_links="<<mainLinks<<" xslib_links="<<libraryLinks
                     <<" links="<<linkedObjects.size()<<" unresolved_names="<<unresolvedNames.size()
                     <<" all_unique_exact_name_matches_linked=1\n";
            return 0;
        }
        else if (mode=="bolt_catalog_boundary_evidence")
        {
            tekla::Project project;
            if(!tekla::readProject(path,project,error))throw std::runtime_error(error);
            if(!project.bolts || !project.boltAssemblies)
                throw std::runtime_error("bolt catalogs were not read as semantic project resources");
            std::map<std::string,std::size_t> boltNames;
            std::map<std::string,std::vector<std::size_t>> rawStandardEntries;
            for(std::size_t index=0;index<project.bolts->bolts.size();++index)
            {
                const auto& entry=project.bolts->bolts[index];
                ++boltNames[entry.name];
                if(entry.rawPayload.size()!=116)throw std::runtime_error("screwdb raw payload width changed");
                std::string rawStandard(entry.rawPayload.begin()+52,entry.rawPayload.begin()+80);
                const auto terminator=rawStandard.find('\0');
                if(terminator!=std::string::npos)rawStandard.resize(terminator);
                while(!rawStandard.empty() && rawStandard.back()==' ')rawStandard.pop_back();
                if(entry.standard!=rawStandard)throw std::runtime_error("screwdb standard does not match its raw field");
                if(!rawStandard.empty())rawStandardEntries[rawStandard].push_back(index);
            }
            std::set<std::string> storedProfiles;
            std::size_t exactProfileMatches=0,exactAssemblyComponentMatches=0;
            for(const auto& bolt:project.model.individualBolts)
            {
                const auto part=project.model.parts.find(bolt.id);
                if(part==project.model.parts.end())throw std::runtime_error("individual bolt has no source part");
                if(!part->second.profile.empty())
                {
                    storedProfiles.insert(part->second.profile);
                    const auto match=boltNames.find(part->second.profile);
                    if(match!=boltNames.end() && match->second==1)++exactProfileMatches;
                }
            }
            for(const auto& assembly:project.boltAssemblies->assemblies)
                for(const auto& componentName:assembly.componentNames)
                {
                    if(componentName.empty())continue;
                    const auto match=boltNames.find(componentName);
                    if(match!=boltNames.end() && match->second==1)++exactAssemblyComponentMatches;
                }
            std::set<std::string> matchedStandards,unmatchedStandards;
            std::size_t nonemptyComponentSlots=0;
            for(const auto& assembly:project.boltAssemblies->assemblies)
                for(const auto& standard:assembly.componentNames)
                    if(!standard.empty())
                    {
                        ++nonemptyComponentSlots;
                        if(rawStandardEntries.count(standard))matchedStandards.insert(standard);
                        else unmatchedStandards.insert(standard);
                    }
            if(project.boltAssemblyStandardAssociations.size()!=215 || nonemptyComponentSlots!=250 ||
               matchedStandards.size()!=33 || unmatchedStandards.size()!=26)
                throw std::runtime_error("assdb standard audit counts changed: links="+
                    std::to_string(project.boltAssemblyStandardAssociations.size())+" nonempty="+
                    std::to_string(nonemptyComponentSlots)+" matched="+std::to_string(matchedStandards.size())+
                    " unmatched="+std::to_string(unmatchedStandards.size()));
            std::set<std::pair<std::size_t,std::size_t>> linkedSlots;
            for(const auto& association:project.boltAssemblyStandardAssociations)
            {
                if(association.assemblyCatalog.filename()!="assdb.db" || association.boltCatalog.filename()!="screwdb.db" ||
                   association.assemblyIndex>=project.boltAssemblies->assemblies.size() ||
                   association.componentIndex>=7 || association.boltEntryIndices.empty() ||
                   !linkedSlots.emplace(association.assemblyIndex,association.componentIndex).second)
                    throw std::runtime_error("invalid or duplicate bolt assembly standard association");
                const auto& source=project.boltAssemblies->assemblies[association.assemblyIndex];
                if(source.componentNames[association.componentIndex]!=association.standard ||
                   rawStandardEntries.at(association.standard)!=association.boltEntryIndices)
                    throw std::runtime_error("bolt assembly association is not an exact complete standard match");
            }
            if(linkedSlots.size()!=215)throw std::runtime_error("bolt assembly standard associations omit a matching slot");
            if(project.model.individualBolts.size()!=718 || storedProfiles.size()!=11 ||
               project.bolts->bolts.size()!=2208 || project.boltAssemblies->assemblies.size()!=36 ||
               exactProfileMatches || exactAssemblyComponentMatches)
                throw std::runtime_error("PSDBIM direct bolt catalog name boundary changed");
            std::cout<<"individual_bolts="<<project.model.individualBolts.size()
                     <<" unique_stored_profiles="<<storedProfiles.size()<<" screw_catalog="<<project.bolts->bolts.size()
                     <<" assemblies="<<project.boltAssemblies->assemblies.size()
                     <<" exact_profile_to_screw="<<exactProfileMatches
                     <<" exact_component_to_screw="<<exactAssemblyComponentMatches
                     <<" standard_links="<<project.boltAssemblyStandardAssociations.size()
                     <<" matched_standards="<<matchedStandards.size()<<" unmatched_standards="<<unmatchedStandards.size()<<"\n";
            return 0;
        }
        else if (mode=="xslib_874_project_raw_evidence")
        {
            tekla::Project project;tekla::ProjectOptions options;options.readNumbering=false;
            if(!tekla::readProject(path,project,error,options))throw std::runtime_error(error);
            const auto file=std::find_if(project.files.begin(),project.files.end(),[](const auto& item) {
                return item.role==tekla::FileRole::ComponentLibrary;
            });
            if(project.model.storageVersion!="8.74" || project.componentLibrary || file==project.files.end() ||
               file->level!=tekla::ReadLevel::Raw)throw std::runtime_error("8.74 xslib fallback status changed");
            const auto raw=project.rawCompanions.find(file->path);
            if(raw==project.rawCompanions.end() || raw->second.kind!=tekla::db1::DatabaseKind::ComponentLibrary ||
               raw->second.tables.size()!=271)throw std::runtime_error("8.74 xslib raw tables unavailable");
            std::size_t records=0,opaque=0;
            for(const auto& table:raw->second.tables){records+=table.records.size();opaque+=!table.opaqueSection.empty();}
            if(records!=1 || opaque!=1)
                throw std::runtime_error("8.74 xslib raw content or association boundary changed");
            std::cout<<"main_version=8.74 xslib_level=raw tables="<<raw->second.tables.size()
                     <<" records="<<records<<" opaque_sections="<<opaque
                     <<" component_semantics=unavailable object_links=not_inferred\n";
            return 0;
        }
        else if (mode=="project_empty_numbering")
        {
            tekla::Project project;
            if(!tekla::readProject(path,project,error)) { std::cerr<<error<<'\n'; return 1; }
            const auto version=project.model.storageVersion;
            const auto expected=version=="8.30"?std::size_t{1}:version=="8.74"?std::size_t{2}:std::size_t{0};
            if(!expected)throw std::runtime_error("unexpected project version for empty DB2 association audit");
            std::size_t numberingFiles=0,partialFiles=0,failedFiles=0,series=0;
            for(const auto& file:project.files)
            {
                if(file.role==tekla::FileRole::Numbering)
                {
                    ++numberingFiles;
                    partialFiles+=file.level==tekla::ReadLevel::PartialSemantic;
                    failedFiles+=file.level==tekla::ReadLevel::Failed;
                }
            }
            for(const auto& database:project.numbering)
                for(const auto& item:database.second.series)++series;
            const auto paired=std::count_if(project.associations.begin(),project.associations.end(),[&](const auto& association) {
                return association.source==project.model.databasePath &&
                    association.target.extension()==".db2" &&
                    association.target.stem()==project.model.databasePath.stem();
            });
            if(numberingFiles!=expected || project.numbering.size()!=expected || partialFiles!=expected ||
               failedFiles!=0 || series!=0 || paired!=1)
                throw std::runtime_error("empty DB2 project integration mismatch: files="+std::to_string(numberingFiles)+
                    " parsed="+std::to_string(project.numbering.size())+" partial="+std::to_string(partialFiles)+
                    " failed="+std::to_string(failedFiles)+" series="+std::to_string(series)+
                    " main_pairs="+std::to_string(paired));
            std::cout<<"version="<<version<<" numbering_files="<<numberingFiles
                     <<" semantic_empty="<<project.numbering.size()<<" main_pairs="<<paired<<" series="<<series<<'\n';
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
            std::size_t files=0,subjects=0,partMatches=0,nonPartIdentityMatches=0,unmatchedIdentityReferences=0;
            std::map<std::uint32_t,std::size_t> nonPartIdentityTypes;
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
                for (const auto& ref:d.modelReferences)
                {
                    if(model.parts.count(ref.modelObjectId))++partMatches;
                    else if(const auto identity=model.identities.find(ref.modelObjectId);identity!=model.identities.end())
                    {
                        const bool assembly=identity->second.type==15 &&
                            std::any_of(model.assemblies.begin(),model.assemblies.end(),[&](const auto& item){return item.id==ref.modelObjectId;});
                        if(assembly){++nonPartIdentityMatches;++nonPartIdentityTypes[identity->second.type];}
                        else ++unmatchedIdentityReferences;
                    }
                    else ++unmatchedIdentityReferences;
                }
            }
            // Cross-file ID/type evidence, NOT independent geometric truth or
            // permission to associate an arbitrary database with these drawings.
            if (files!=293 || subjects!=59 || partMatches!=1361 || nonPartIdentityMatches!=35 || unmatchedIdentityReferences)
                throw std::runtime_error("legacy drawing cross-file evidence changed");
            const auto db1Path=path.parent_path()/"PSDBIM__EXCEL-1246A-BLDG-B"/"EXCEL-1246A-BLDG-B.db1";
            const auto mismatchedDb1=path.parent_path()/"btscm-pechelire"/"pechelirejulV1.db1";
            TemporaryTree scratch;
            const auto projectRoot=scratch.path/"legacy-project";
            const auto drawingDirectory=projectRoot/"drawings";
            std::filesystem::create_directories(drawingDirectory);
            const auto stagedModel=projectRoot/db1Path.filename();
            std::filesystem::copy_file(db1Path,stagedModel);
            const auto stagedMismatch=projectRoot/"mismatched.db1";
            std::filesystem::copy_file(mismatchedDb1,stagedMismatch);
            for(const auto& file:std::filesystem::directory_iterator(path))
                if(file.path().extension()==".dg")std::filesystem::copy_file(file.path(),drawingDirectory/file.path().filename());
            tekla::Project project;tekla::ProjectOptions projectOptions;projectOptions.mainDatabase=stagedModel;
            if(!tekla::readProject(projectRoot,project,error,projectOptions))throw std::runtime_error(error);
            if(project.drawings.size()!=293 || !project.drawingSubjectAssociations.empty() ||
               !project.drawingModelAssociations.empty())
                throw std::runtime_error("legacy drawing numeric IDs were associated without explicit trust");
            projectOptions.trustLegacyDrawingObjectIds=true;
            if(!tekla::readProject(projectRoot,project,error,projectOptions))throw std::runtime_error(error);
            std::size_t explicitSubjects=0,explicitReferences=0,explicitPartReferences=0,explicitAssemblyReferences=0,explicitFiles=0;
            for(const auto& link:project.drawingSubjectAssociations)
            {
                if(link.pairingEvidence!=tekla::DrawingPairEvidence::ExplicitLegacyNumericScope ||
                   project.model.identities.at(link.modelObjectId).guid!=link.modelGuid)
                    throw std::runtime_error("legacy drawing subject evidence was not retained");
                ++explicitSubjects;
            }
            for(const auto& link:project.drawingModelAssociations)
            {
                if(link.pairingEvidence!=tekla::DrawingPairEvidence::ExplicitLegacyNumericScope ||
                   project.model.identities.at(link.modelObjectId).guid!=link.modelGuid)
                    throw std::runtime_error("legacy drawing reference evidence was not retained");
                if(project.model.parts.count(link.modelObjectId))++explicitPartReferences;
                else if(std::any_of(project.model.assemblies.begin(),project.model.assemblies.end(),
                    [&](const auto& item){return item.id==link.modelObjectId;}))++explicitAssemblyReferences;
                else throw std::runtime_error("legacy drawing reference linked to an unclassified identity");
                ++explicitReferences;
            }
            for(const auto& association:project.associations)
                if(association.source==stagedModel && association.reason.find("caller explicitly scopes GUID-less legacy drawing numeric IDs to this same-version model")!=std::string::npos)
                    ++explicitFiles;
            if(explicitSubjects!=59 || explicitReferences!=2155 || explicitPartReferences!=2120 ||
               explicitAssemblyReferences!=35 || explicitFiles!=293)
                throw std::runtime_error("explicit legacy drawing project associations changed: subjects="+
                    std::to_string(explicitSubjects)+" references="+std::to_string(explicitReferences)+
                    " files="+std::to_string(explicitFiles));

            // Trust never overrides a storage-version boundary.
            projectOptions.mainDatabase=stagedMismatch;
            if(!tekla::readProject(projectRoot,project,error,projectOptions))throw std::runtime_error(error);
            if(!project.drawingSubjectAssociations.empty() || !project.drawingModelAssociations.empty())
                throw std::runtime_error("legacy drawing option paired across storage versions");
            std::cout<<"files="<<files<<" subject_id_type_matches="<<subjects<<" part_reference_matches="<<partMatches
                <<" nonpart_identity_reference_matches="<<nonPartIdentityMatches<<" unmatched_identity_references="<<unmatchedIdentityReferences<<" types=";
            bool firstType=true;for(const auto& item:nonPartIdentityTypes){if(!firstType)std::cout<<',';std::cout<<item.first<<':'<<item.second;firstType=false;}
            std::cout<<" explicit_subject_links="<<explicitSubjects<<" explicit_reference_links="<<explicitReferences
                     <<" explicit_file_links="<<explicitFiles<<'\n';
        }
        else if (mode=="raw")
        {
            tekla::db1::RawDatabase raw;
            tekla::db1::RawDatabaseOptions options; options.retainDecompressedFileImage=true;
            if (!tekla::db1::parseRawDatabase(path,raw,error,options)) { std::cerr << error << '\n'; return 1; }
            if (raw.kind==tekla::db1::DatabaseKind::Drawing && (raw.storageVersion=="7.30" || raw.storageVersion=="8.95"))
            {
                auto rebuilt=raw.preamble;
                const auto word=[&](std::uint32_t n) { for(unsigned i=0;i<4;++i)rebuilt.push_back(static_cast<std::uint8_t>(n>>(i*8))); };
                for(const auto& table:raw.tables)
                {
                    word(static_cast<std::uint32_t>(table.records.size()));word(table.payloadSize);
                    for(const auto& row:table.records)
                    {
                        if(row.payload.size()!=table.payloadSize || !row.allocatorMetadata.empty())throw std::runtime_error("7.30 drawing row framing lost");
                        rebuilt.push_back(row.allocationTag);rebuilt.insert(rebuilt.end(),row.payload.begin(),row.payload.end());
                    }
                }
                if(rebuilt!=raw.decompressedFileImage)throw std::runtime_error("7.30 drawing reconstruction differs from file");
            }
            if (raw.kind==tekla::db1::DatabaseKind::Analysis ||
                (raw.kind==tekla::db1::DatabaseKind::Drawing && raw.storageVersion=="7.64") ||
                (raw.kind==tekla::db1::DatabaseKind::Drawing && (raw.storageVersion=="7.82" || raw.storageVersion=="8.44" || raw.storageVersion=="9.08")))
            {
                auto rebuilt=raw.preamble;
                const auto word=[&](std::uint32_t n) { for (unsigned i=0;i<4;++i) rebuilt.push_back(static_cast<std::uint8_t>(n>>(i*8))); };
                for (const auto& t:raw.tables)
                {
                    word(0xdbcec066); word(t.payloadSize); word(static_cast<std::uint32_t>(t.fieldDescriptors.size()));
                    for (auto f:t.fieldDescriptors) word(f);
                    for (const auto& r:t.records)
                    {
                        const auto metadataBytes=(raw.kind==tekla::db1::DatabaseKind::Analysis ||
                            (raw.kind==tekla::db1::DatabaseKind::Drawing && raw.storageVersion=="7.64")) ? 8 :
                            (raw.storageVersion!="7.82"?24+16*std::count(t.fieldDescriptors.begin(),t.fieldDescriptors.end(),1U):40);
                        if (r.payload.size()!=t.payloadSize || r.allocatorMetadata.size()!=static_cast<std::size_t>(metadataBytes)) throw std::runtime_error("raw record framing lost");
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
        else if (mode=="clb_public_evidence")
        {
            tekla::db1::ClbDocument document;
            if (!tekla::db1::parseClbDocument(path, document, error))
                throw std::runtime_error(error);
            if (document.libraryId != "PEIKKO" || document.statements.size() != 2 ||
                document.sections.size() != 1 || !document.includes.empty() ||
                !document.diagnostics.empty())
                throw std::runtime_error("public PEIKKO CLB document structure changed");
            const auto& section = document.sections.front();
            if (section.name != "DELTA_EXT" || section.baseAttributes.size() != 9 ||
                section.expressions.size() != 18 || section.geometries.size() != 1)
                throw std::runtime_error("public PEIKKO CLB section contents changed");
            const auto findValue = [](const auto& values, const std::string& name) -> const auto& {
                const auto found = std::find_if(values.begin(), values.end(), [&](const auto& item) {
                    return item.name == name;
                });
                if (found == values.end())
                    throw std::runtime_error("missing public PEIKKO CLB value " + name);
                return *found;
            };
            if (findValue(section.baseAttributes, "b2").defaultValue != "280" ||
                findValue(section.baseAttributes, "h").type != "dimension" ||
                findValue(section.expressions, "y4").formula !=
                    "h/2-8.0+(d3*bb.left)/sqrt((h-8.0)*(h-8.0)+bb.left*bb.left)")
                throw std::runtime_error("public PEIKKO CLB dimensions/formula changed");
            const auto& geometry = section.geometries.front();
            if (geometry.name != "default" || geometry.faces.size() != 1 ||
                geometry.faces.front().index != std::optional<std::uint32_t>{0} ||
                geometry.faces.front().points.size() != 14)
                throw std::runtime_error("public PEIKKO CLB face geometry changed");
            const auto& first = geometry.faces.front().points.front();
            const auto& last = geometry.faces.front().points.back();
            if (first.stationIndex != std::optional<std::uint32_t>{0} ||
                first.yReference != "y1" || first.zReference != "zo12" ||
                last.yReference != "y1" || last.zReference != "zo1")
                throw std::runtime_error("public PEIKKO CLB point references changed");
            std::cout << "library=PEIKKO sections=1 base=9 expressions=18 geometry=1 faces=1 points=14 diagnostics=0\n";
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
            const auto readU32=[](const std::vector<std::uint8_t>& bytes,std::size_t offset) {
                std::uint32_t value=0; std::memcpy(&value,bytes.data()+offset,sizeof(value)); return value;
            };
            const auto readReal=[](const std::vector<std::uint8_t>& bytes,std::size_t offset) {
                double value=0; std::memcpy(&value,bytes.data()+offset,sizeof(value)); return value;
            };
            std::map<std::pair<std::uint32_t,std::uint32_t>,std::vector<std::uint64_t>> expectedIndexTargets;
            for (const auto& targetTable:drawing.raw.tables)
                if (targetTable.ordinal!=0)
                    for (const auto& targetRecord:targetTable.records)
                        if (targetRecord.payload.size()>=8 &&
                            readU32(targetRecord.payload,0)==targetTable.ordinal)
                            expectedIndexTargets[{targetTable.ordinal,readU32(targetRecord.payload,4)}].push_back(targetRecord.fileOffset);
            const auto indexTable=std::find_if(drawing.raw.tables.begin(),drawing.raw.tables.end(),
                [](const auto& item){return item.ordinal==304;});
            if (indexTable==drawing.raw.tables.end() || drawing.recordAssociationIndex.size()!=indexTable->records.size())
                throw std::runtime_error("type-304 index projection count differs from raw rows");
            for (std::size_t i=0;i<indexTable->records.size();++i)
            {
                const auto& row=indexTable->records[i].payload;
                if (row.size()!=24) throw std::runtime_error("invalid type-304 index row in corpus");
                const auto& entry=drawing.recordAssociationIndex[i];
                const auto found=expectedIndexTargets.find({readU32(row,0),readU32(row,4)});
                const std::vector<std::uint64_t> expected=found==expectedIndexTargets.end()
                    ? std::vector<std::uint64_t>{} : found->second;
                if (entry.recordTypeCode!=readU32(row,0) || entry.recordId!=readU32(row,4) ||
                    entry.candidateTargetRecordOffsets!=expected)
                    throw std::runtime_error("type-304 target row candidates differ from physical table records");
            }
            const auto lineTable=std::find_if(drawing.raw.tables.begin(),drawing.raw.tables.end(),
                [](const auto& item){return item.ordinal==311;});
            if(lineTable==drawing.raw.tables.end() || lineTable->records.size()!=drawing.lineSegments.size())
                throw std::runtime_error("type-311 line segment count differs from raw rows");
            for(std::size_t i=0;i<lineTable->records.size();++i)
            {
                const auto& row=lineTable->records[i]; const auto& line=drawing.lineSegments[i];
                std::uint32_t id=0; std::memcpy(&id,row.payload.data()+4,sizeof(id));
                std::array<double,4> endpoints{};
                for(std::size_t point=0;point<endpoints.size();++point)
                    std::memcpy(&endpoints[point],row.payload.data()+24+point*8,sizeof(double));
                if(line.recordId!=id || line.rawRecordOffset!=row.fileOffset ||
                   line.startPoint!=std::array<double,2>{endpoints[0],endpoints[1]} ||
                   line.endPoint!=std::array<double,2>{endpoints[2],endpoints[3]})
                    throw std::runtime_error("type-311 line segment fields differ from raw row");
            }
            if(drawing.raw.storageVersion=="9.50")
            {
                const auto viewTable=std::find_if(drawing.raw.tables.begin(),drawing.raw.tables.end(),
                    [](const auto& item){return item.ordinal==260;});
                const auto setTable=std::find_if(drawing.raw.tables.begin(),drawing.raw.tables.end(),
                    [](const auto& item){return item.ordinal==257;});
                const auto dimensionTable=std::find_if(drawing.raw.tables.begin(),drawing.raw.tables.end(),
                    [](const auto& item){return item.ordinal==256;});
                if(viewTable==drawing.raw.tables.end() || setTable==drawing.raw.tables.end() ||
                   dimensionTable==drawing.raw.tables.end() || drawing.viewsByRecordId.size()!=viewTable->records.size() ||
                   drawing.straightDimensionSets.size()!=setTable->records.size() ||
                   drawing.straightDimensions.size()+drawing.unhandledDimensionRecordIds.size()!=dimensionTable->records.size() ||
                   !drawing.unhandledDimensionSetRecordIds.empty())
                    throw std::runtime_error("9.50 view/dimension semantic counts differ from the raw records");
                for(const auto& raw:viewTable->records)
                {
                    const auto id=readU32(raw.payload,4),context=readU32(raw.payload,8);
                    const auto decoded=drawing.viewsByRecordId.find(id);
                    if(raw.payload.size()!=4740 || readU32(raw.payload,0)!=260 || decoded==drawing.viewsByRecordId.end() ||
                       decoded->second.contextId!=context || decoded->second.geometryDecoded ||
                       !drawing.viewsByContext.count(context))
                        throw std::runtime_error("9.50 view ID/context prefix differs from its raw row or was promoted to geometry");
                }
                for(const auto& raw:setTable->records)
                {
                    const auto id=readU32(raw.payload,4),context=readU32(raw.payload,8);
                    const auto decoded=drawing.straightDimensionSets.find(id);
                    if(raw.payload.size()!=144 || readU32(raw.payload,0)!=257 || decoded==drawing.straightDimensionSets.end() ||
                       decoded->second.contextId!=context || !drawing.viewsByContext.count(context))
                        throw std::runtime_error("9.50 dimension set ID/context differs from its raw row");
                }
                for(const auto& raw:dimensionTable->records)
                {
                    const auto id=readU32(raw.payload,4),context=readU32(raw.payload,8);
                    const auto setId=readU32(raw.payload,12),subtype=readU32(raw.payload,16);
                    if(raw.payload.size()!=1496 || readU32(raw.payload,0)!=256 || !drawing.viewsByContext.count(context))
                        throw std::runtime_error("9.50 dimension header/context differs from its raw row");
                    const auto set=drawing.straightDimensionSets.find(setId);
                    if(set==drawing.straightDimensionSets.end() || set->second.contextId!=context ||
                       std::count(set->second.dimensionIds.begin(),set->second.dimensionIds.end(),id)!=1)
                        throw std::runtime_error("9.50 dimension/set context association differs from raw rows");
                    const auto decoded=drawing.straightDimensions.find(id);
                    if(subtype!=1)
                    {
                        if(decoded!=drawing.straightDimensions.end() ||
                           std::find(drawing.unhandledDimensionRecordIds.begin(),drawing.unhandledDimensionRecordIds.end(),id)==drawing.unhandledDimensionRecordIds.end())
                            throw std::runtime_error("9.50 non-subtype-1 dimension was decoded");
                        continue;
                    }
                    if(decoded==drawing.straightDimensions.end())
                        throw std::runtime_error("9.50 subtype-1 dimension is missing from its semantic projection");
                    const auto& d=decoded->second;
                    if(d.contextId!=context || d.dimensionSetId!=setId || d.subtypeCode!=subtype)
                        throw std::runtime_error("9.50 dimension identity/set fields differ from its raw row");
                    for(std::size_t axis=0;axis<3;++axis)
                        if(d.startPoint[axis]!=readReal(raw.payload,24+axis*8) ||
                           d.endPoint[axis]!=readReal(raw.payload,48+axis*8) ||
                           d.upDirection[axis]!=readReal(raw.payload,168+axis*8))
                            throw std::runtime_error("9.50 dimension coordinate/direction differs from its raw row");
                    const double storedDistance=readReal(raw.payload,192);
                    if(d.distance!=storedDistance)
                        throw std::runtime_error("9.50 dimension distance differs from its raw row");
                }
            }
            if(drawing.raw.storageVersion=="9.54")
            {
                const auto linkTable=std::find_if(drawing.raw.tables.begin(),drawing.raw.tables.end(),
                    [](const auto& item){return item.ordinal==281;});
                if(linkTable==drawing.raw.tables.end()) throw std::runtime_error("missing 9.54 type-281 table");
                std::size_t targetCount=0;
                for(const auto& row:linkTable->records)
                {
                    if(row.payload.size()!=32) throw std::runtime_error("invalid 9.54 type-281 row in corpus");
                    std::uint32_t type=0,target=0;
                    std::memcpy(&type,row.payload.data()+4,sizeof(type));
                    if(type!=311) continue;
                    std::memcpy(&target,row.payload.data()+28,sizeof(target));
                    ++targetCount;
                    const auto reference=std::find_if(drawing.type281LineReferences.begin(),drawing.type281LineReferences.end(),
                        [&](const auto& item){return item.rawRecordOffset==row.fileOffset;});
                    const auto candidate=expectedIndexTargets.find({311,target});
                    const std::vector<std::uint64_t> expected=candidate==expectedIndexTargets.end()
                        ? std::vector<std::uint64_t>{} : candidate->second;
                    if(reference==drawing.type281LineReferences.end() || reference->lineRecordId!=target ||
                       reference->candidateLineRecordOffsets!=expected)
                        throw std::runtime_error("9.54 type-281 line target offsets differ from matching type-311 rows");
                }
                if(targetCount!=drawing.type281LineReferences.size())
                    throw std::runtime_error("9.54 type-281 line reference count differs from raw rows");
            }
            const bool olderNumeric=drawing.raw.storageVersion=="7.82" || drawing.raw.storageVersion=="7.30" || drawing.raw.storageVersion=="7.64";
            const bool old=olderNumeric;
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
            if (drawing.raw.storageVersion=="7.64")
            {
                for (const auto& entry:drawing.straightDimensions)
                {
                    const auto& d=entry.second; hash.number(d.recordId); hash.number(d.contextId);
                    hash.number(d.dimensionSetId); hash.number(d.subtypeCode);
                    for (auto x:d.startPoint) hash.real(x); for (auto x:d.endPoint) hash.real(x);
                    for (auto x:d.upDirection) hash.real(x); hash.real(d.distance);
                    hash.number(d.projectedLength.has_value()); if(d.projectedLength)hash.real(*d.projectedLength);
                }
                for (const auto& entry:drawing.straightDimensionSets)
                {
                    hash.number(entry.first); hash.number(entry.second.contextId);
                    for(auto id:entry.second.dimensionIds)hash.number(id);
                }
                for(auto id:drawing.unhandledDimensionRecordIds)hash.number(id);
                for(auto id:drawing.unhandledDimensionSetRecordIds)hash.number(id);
            }
            if (drawing.raw.storageVersion=="9.50")
            {
                for(const auto& entry:drawing.straightDimensions)
                {
                    const auto& d=entry.second; hash.number(d.recordId); hash.number(d.contextId);
                    hash.number(d.dimensionSetId); hash.number(d.subtypeCode);
                    for(auto x:d.startPoint)hash.real(x); for(auto x:d.endPoint)hash.real(x);
                    for(auto x:d.upDirection)hash.real(x); hash.real(d.distance);
                    hash.number(d.projectedLength.has_value()); if(d.projectedLength)hash.real(*d.projectedLength);
                }
                for(const auto& entry:drawing.straightDimensionSets)
                {
                    hash.number(entry.first); hash.number(entry.second.contextId);
                    for(auto id:entry.second.dimensionIds)hash.number(id);
                }
                for(auto id:drawing.unhandledDimensionRecordIds)hash.number(id);
                for(auto id:drawing.unhandledDimensionSetRecordIds)hash.number(id);
            }
            std::cout<<"version="<<drawing.raw.storageVersion<<" tables="<<drawing.raw.tables.size()
                     <<" strings="<<drawing.strings.size()<<" properties="<<drawing.properties.size()
                     <<" links="<<drawing.propertyLinks.size()<<" sheets="<<drawing.sheets.size()
                     <<" model_refs="<<drawing.modelReferences.size()
                     <<" views="<<(old?drawing.viewsByRecordId.size():drawing.viewsByContext.size())<<" subject="<<bool(drawing.subject)
                     <<" guid="<<drawing.projectGuid<<" stored="<<drawing.storedFileName;
            if (old) std::cout<<" incomplete_text="<<incomplete<<" unhandled_views="<<drawing.unhandledViewRecordIds.size()<<" unique_contexts="<<drawing.viewsByContext.size();
            if (drawing.raw.storageVersion=="7.64") std::cout<<" dimensions="<<drawing.straightDimensions.size()<<" dimension_sets="<<drawing.straightDimensionSets.size()<<" unhandled_dimensions="<<(drawing.unhandledDimensionRecordIds.size()+drawing.unhandledDimensionSetRecordIds.size());
            if (drawing.raw.storageVersion=="9.50") std::cout<<" dimensions="<<drawing.straightDimensions.size()<<" dimension_sets="<<drawing.straightDimensionSets.size()<<" unhandled_dimensions="<<(drawing.unhandledDimensionRecordIds.size()+drawing.unhandledDimensionSetRecordIds.size());
            std::cout<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
        }
        else if(mode=="drawing_model_references")
        {
            tekla::Drawing drawing;
            if(!tekla::parseDrawing(path,drawing,error)){std::cerr<<error<<'\n';return 1;}
            std::cout<<"references="<<drawing.modelReferences.size()<<'\n';
            for(const auto& reference:drawing.modelReferences)
                std::cout<<"reference="<<reference.recordId<<':'<<reference.drawingContextId<<':'<<reference.modelGuid<<'\n';
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
