// The independently exported IFC has generated plate width suffixes. Compare
// the 19 rolled/tubular profiles exactly; do not invent stored plate dimensions.
int profile844Validation(const std::filesystem::path& directory)
{
    using namespace ifc730;
    tekla::db1::Model model; std::string error;
    require(tekla::db1::parseModelFile(directory/"Philippe_Boineau_CCF_BE_AMCR_2019.db1.bak",model,error),error);
    require(model.storageVersion=="8.44" && model.actualPartIds.size()==58,"8.44 model scope");
    const auto all=read(directory/"PB_AUVENT.ifc");
    std::map<std::string,const tekla::db1::Part*> parts;
    for (auto id:model.actualPartIds) require(parts.emplace(model.parts.at(id).guid,&model.parts.at(id)).second,"duplicate model GUID");
    const std::set<std::string> profiles{"IPE160","IPE120","TUBE-60.3*3.2","L40*4","UPF-100*50*4"};
    std::set<std::string> seen; std::size_t matchedProfiles=0;
    for (const auto& entry:all)
    {
        const auto& e=entry.second;
        if (e.type!="IFCBEAM" && e.type!="IFCCOLUMN" && e.type!="IFCMEMBER" && e.type!="IFCDISCRETEACCESSORY") continue;
        require(e.args.size()>=8,"IFC product fields");
        const auto id=guid(string(e.args[0]));const auto found=parts.find(id);
        require(found!=parts.end() && seen.insert(id).second,"IFC/model GUID pairing");
        const auto& part=*found->second;
        require(part.name==string(e.args[2]),"8.44 exported name "+id);
        const auto profile=string(e.args[3]);
        if (profiles.count(profile)) { require(part.profile==profile,"8.44 exported profile "+id); ++matchedProfiles; }
        const auto f=placement(all,entityRef(e.args[5]));
        for (std::size_t i=0;i<3;++i) coordinateNear(part.origin[i],f.origin[i],0.002,"8.44 exported origin "+id);
    }
    require(seen.size()==58 && matchedProfiles==19,"8.44 independent evidence coverage");
    std::cout<<"version=8.44 identities=58 names=58 profiles=19 origins=58\n";
    return 0;
}
