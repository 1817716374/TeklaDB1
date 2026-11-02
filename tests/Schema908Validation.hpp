// Internal row consistency is separate from the two independent observations:
// July's official API BEAM count and profile names in the paired profdb.bin.
#include <algorithm>
#include <map>
#include <set>
int schema908Validation(const std::filesystem::path& directory)
{
    using namespace tekla::db1;
    const auto require=[](bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);};
    std::size_t identities=0,classes=0,bolts=0,profileMatches=0,beams=0,componentParts=0;
    for(bool library:{false,true})
    {
        const auto path=directory/(library?"xslib.db1":"API_Developer_Exam_01.db1");
        Model model;RawDatabase raw;std::string error;
        require(library?parseComponentLibrary(path,model,error):parseModelFile(path,model,error),error);
        require(parseRawDatabase(path,raw,error),error);
        require(model.storageVersion=="9.08" && raw.tables.size()==(library?303U:339U),"9.08 scope changed");
        const auto word=[](const std::vector<std::uint8_t>& data,std::size_t offset){
            if(offset+4>data.size())throw std::runtime_error("short 9.08 evidence row");
            return std::uint32_t(data[offset]) | std::uint32_t(data[offset+1])<<8 |
                std::uint32_t(data[offset+2])<<16 | std::uint32_t(data[offset+3])<<24;
        };
        for(const auto& r:raw.tables.at(library?293:329).records)
        {
            const auto& c=model.identityClasses.at(word(r.payload,0));
            require(c.rawPayload==r.payload && c.recordKind==word(r.payload,4),"9.08 full class payload lost");++classes;
        }
        for(const auto& r:raw.tables.at(library?260:293).records)
        {
            const auto& i=model.identities.at(word(r.payload,0));
            require(i.classReferenceId==word(r.payload,4) && i.ownerId==word(r.payload,8) &&
                i.type==model.identityClasses.at(i.classReferenceId).recordKind,"9.08 identity class/owner join changed");++identities;
        }
        for(const auto& r:raw.tables.at(library?291:327).records)
        {
            const auto& b=model.boltDefinitions.at(word(r.payload,0));
            require(r.payload.size()==312 && b.legacy908Parameters &&
                std::equal(b.legacy908Parameters->begin(),b.legacy908Parameters->end(),r.payload.begin()+252) &&
                b.diameter==0 && b.tolerance==0 && b.length==0 && b.extraLength==0 && b.boltType==0,
                "unverified 9.08 bolt parameters interpreted or lost");++bolts;
        }
        if(!library)
        {
            require(model.profiles.size()==3050,"paired 9.08 profile catalog missing");
            for(const auto& entry:model.definitions)if(model.profiles.count(entry.second.profile))++profileMatches;
            for(auto id:model.actualPartIds)if(model.definitions.at(model.parts.at(id).definitionId).subtype%10==0)++beams;
            require(model.componentPartIds.size()==104,"9.08 component-owned part inventory changed");
            require(model.unhandledPartIds.empty(),"9.08 component parts remain unhandled");
            std::set<std::uint32_t> componentOwners;
            std::map<std::uint32_t,std::size_t> childrenPerOwner;
            for(const auto id:model.componentPartIds)
            {
                const auto& part=model.parts.at(id);
                const auto& identity=model.identities.at(id);
                const auto& definition=model.definitions.at(part.definitionId);
                const auto component=std::find_if(model.components.begin(),model.components.end(),[&](const auto& value){return value.id==part.ownerId;});
                require(part.internalType==39 && part.name=="PLATE" && definition.subtype==2 && !part.guid.empty(),
                    "9.08 component-owned type-39 fields changed");
                require(identity.ownerId==part.ownerId && identity.contextId==part.contextId && part.contextId==part.ownerId,
                    "9.08 component-owned type-39 identity/context join changed");
                require(component!=model.components.end() &&
                    std::find(component->childIds.begin(),component->childIds.end(),id)!=component->childIds.end(),
                    "9.08 type-39 owner is not a parsed component child");
                require(std::find(model.actualPartIds.begin(),model.actualPartIds.end(),id)==model.actualPartIds.end() &&
                    std::find(model.operativePartIds.begin(),model.operativePartIds.end(),id)==model.operativePartIds.end(),
                    "9.08 component-owned part counted as standalone part/operative");
                componentOwners.insert(part.ownerId);
                ++childrenPerOwner[part.ownerId];
            }
            require(componentOwners.size()==26 && std::all_of(childrenPerOwner.begin(),childrenPerOwner.end(),
                [](const auto& entry){return entry.second==4;}),"9.08 type-39 component owner distribution changed");
            componentParts+=model.componentPartIds.size();
        }
        else require(model.componentPartIds.empty(),"9.08 xslib unexpectedly contains verified type-39 parts");
    }
    require(profileMatches==49,"9.08 family/dimension names do not match independent profile catalog");
    tekla::Project project;tekla::ProjectOptions options;options.strictCompanions=true;std::string error;
    require(tekla::readProject(directory,project,error,options),error);
    require(project.componentLibrary.has_value() && project.model.profiles.size()==3050 &&
        project.model.componentPartIds.size()==104 && project.model.unhandledPartIds.empty(),
        "9.08 project lost library or paired catalog");
    ProfileCatalog independentCatalog;
    require(parseProfileCatalog(directory/"profdb.bin",independentCatalog,error),error);
    using ProfileLinkKey=std::tuple<std::filesystem::path,std::uint32_t,std::string,std::filesystem::path,ProfileSource>;
    std::set<ProfileLinkKey> expectedProfileLinks,actualProfileLinks;
    const auto collectExpected=[&](const Model& model) {
        for(const auto& entry:model.parts)
            if(independentCatalog.profiles.count(entry.second.profile))
                expectedProfileLinks.emplace(model.databasePath,entry.first,entry.second.profile,
                    std::filesystem::absolute(directory/"profdb.bin").lexically_normal(),ProfileSource::Catalog);
    };
    collectExpected(project.model);collectExpected(*project.componentLibrary);
    for(const auto& link:project.profileCatalogAssociations)
        actualProfileLinks.emplace(link.database,link.objectId,link.profileName,link.catalog,link.source);
    require(actualProfileLinks==expectedProfileLinks && !actualProfileLinks.empty(),
        "project profile-object associations differ from exact unique profdb names");
    std::size_t partialModels=0;
    for(const auto& file:project.files)
        if(file.role==tekla::FileRole::Model || file.role==tekla::FileRole::ComponentLibrary)
        {
            require(file.level==tekla::ReadLevel::PartialSemantic && !file.diagnostic.empty(),"9.08 project overstates semantic coverage");
            ++partialModels;
        }
    require(partialModels==2 && std::any_of(project.associations.begin(),project.associations.end(),[&](const auto& link){
        return link.source==project.model.databasePath && link.target==project.componentLibrary->databasePath;
    }),"9.08 project model/library association missing");
    const auto source=directory/"Form1.cs";
    if(std::filesystem::exists(source))
    {
        std::ifstream input(source,std::ios::binary);std::string text((std::istreambuf_iterator<char>(input)),{});
        std::smatch match;
        require(text.find("ModelObject.ModelObjectEnum.BEAM")!=std::string::npos &&
            std::regex_search(text,match,std::regex(R"(moe\.GetSize\(\)\s*==\s*(\d+))")),"official API beam count missing");
        require(beams==std::stoul(match[1].str()),"July 9.08 model disagrees with official API BEAM count");
    }
    std::cout<<"version=9.08 classes="<<classes<<" identities="<<identities<<" retained_bolt_parameters="<<bolts
        <<" catalog_profiles="<<profileMatches<<" catalog_profile_object_links="<<actualProfileLinks.size()
        <<" beam_candidates="<<beams<<" component_parts="<<componentParts
        <<" official_api_count="<<(std::filesystem::exists(source)?"checked":"not_applicable")<<'\n';
    return 0;
}
