// Internal row consistency is separate from the two independent observations:
// July's official API BEAM count and profile names in the paired profdb.bin.
int schema908Validation(const std::filesystem::path& directory)
{
    using namespace tekla::db1;
    const auto require=[](bool ok,const std::string& reason){if(!ok)throw std::runtime_error(reason);};
    std::size_t identities=0,classes=0,bolts=0,profileMatches=0,beams=0;
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
        }
    }
    require(profileMatches==49,"9.08 family/dimension names do not match independent profile catalog");
    tekla::Project project;tekla::ProjectOptions options;options.strictCompanions=true;std::string error;
    require(tekla::readProject(directory,project,error,options),error);
    require(project.componentLibrary.has_value() && project.model.profiles.size()==3050,
        "9.08 project lost library or paired catalog");
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
        <<" catalog_profiles="<<profileMatches<<" beam_candidates="<<beams<<" official_api_count="<<(std::filesystem::exists(source)?"checked":"not_applicable")<<'\n';
    return 0;
}
