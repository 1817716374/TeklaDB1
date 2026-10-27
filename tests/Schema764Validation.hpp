// Pinned U52 evidence only: IFC GUIDs identify the 7.30 snapshot. Numeric IDs
// bridge that snapshot to 7.64 only after comparing all 67 stored parts. This
// test does not introduce a general cross-project or GUID migration rule.
int schema764Validation(const std::filesystem::path& directory)
{
    using namespace tekla::db1;
    const auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("7.64 evidence: "+why);};
    const auto sampleText=[&](const std::string& bytes){
        std::string out;
        for(unsigned char c:bytes)
        {
            require(c<128 || c>=160,"unverified sample text code page");
            if(c<128)out+=static_cast<char>(c);
            else {out+=static_cast<char>(0xc0|(c>>6));out+=static_cast<char>(0x80|(c&63));}
        }
        return out;
    };
    const auto near=[&](const Vec3& a,const Vec3& b,double tolerance,const std::string& label){
        for(std::size_t n=0;n<3;++n)
            require(std::isfinite(a[n]) && std::isfinite(b[n]) && std::abs(a[n]-b[n])<=tolerance,label);
    };
    Model model,old,library;std::string error;
    require(parseModelFile(directory/"u52_2007vierge.db1",model,error),error);
    require(parseModelFile(directory/"u52_2007v4.db1.bak",old,error),error);
    require(parseComponentLibrary(directory/"xslib.db1",library,error),error);
    require(model.storageVersion=="7.64" && library.storageVersion=="7.64" && old.storageVersion=="7.30","version scope");
    require(model.actualPartIds.size()==67 && old.actualPartIds.size()==67 && library.actualPartIds.size()==5,"stored count baseline");
    std::map<std::string,std::uint32_t> oldGuids;
    std::set<std::uint32_t> actual(model.actualPartIds.begin(),model.actualPartIds.end());
    for(auto id:old.actualPartIds)
    {
        const auto& a=old.parts.at(id);const auto& b=model.parts.at(id);
        require(actual.count(id) && oldGuids.emplace(a.guid,id).second,"snapshot identity ambiguity");
        require(a.guid!=b.guid,"sample GUID reset assumption changed");
        require(a.name==sampleText(b.name) && a.profile==sampleText(b.profile) && a.material==sampleText(b.material) && a.classNumber==b.classNumber,"snapshot definition mismatch");
        near(a.start,b.start,1e-7,"snapshot start");near(a.end,b.end,1e-7,"snapshot end");
        near(a.origin,b.origin,1e-7,"snapshot origin");near(a.axis,b.axis,1e-7,"snapshot axis");
        near(a.secondary,b.secondary,1e-7,"snapshot secondary");
        require(std::abs(a.length-b.length)<1e-7 && a.contour.size()==b.contour.size(),"snapshot length/contour count");
        for(std::size_t n=0;n<a.contour.size();++n)near(a.contour[n].value,b.contour[n].value,1e-7,"snapshot contour");
    }
    const auto all=ifc730::read(directory/"U52_2007V4.ifc");
    std::map<unsigned,std::map<std::string,std::string>> properties;
    std::size_t units=0;
    for(const auto& item:all)
    {
        const auto& e=item.second;
        if(e.type=="IFCSIUNIT" && e.args.at(1)==".LENGTHUNIT.")
        {require(e.args.at(2)==".MILLI." && e.args.at(3)==".METRE.","IFC length units");++units;}
        if(e.type!="IFCRELDEFINESBYPROPERTIES")continue;
        const auto& set=all.at(ifc730::entityRef(e.args.at(5)));
        if(set.type!="IFCPROPERTYSET")continue;
        for(auto target:ifc730::refs(e.args.at(4)))for(auto p:ifc730::refs(set.args.at(4)))
        {
            const auto& a=ifc730::entity(all,p,"IFCPROPERTYSINGLEVALUE",4).args;
            require(properties[target].emplace(ifc730::string(a[0]),a[2]).second,"duplicate IFC property");
        }
    }
    require(units==1,"IFC unit count");
    std::set<std::uint32_t> seen,bolts;
    for(const auto& bolt:model.individualBolts)require(bolts.insert(bolt.id).second,"duplicate bolt");
    std::size_t partMatches=0,boltMatches=0,profileMatches=0;
    for(const auto& item:all)
    {
        const auto& e=item.second;const bool bolt=e.type=="IFCMECHANICALFASTENER";
        if(!bolt && e.type!="IFCBEAM" && e.type!="IFCCOLUMN" && e.type!="IFCPLATE")continue;
        const auto tag=ifc730::string(e.args.at(7));require(tag.rfind("TS_",0)==0,"IFC numeric tag");
        const auto id=ifc730::number(tag.substr(3));require(seen.insert(id).second,"duplicate IFC object");
        const auto& p=model.parts.at(id);const auto f=ifc730::placement(all,ifc730::entityRef(e.args.at(5)));
        near(p.origin,f.origin,.002,"IFC placement "+tag);
        if(bolt)
        {
            require(bolts.count(id) && p.internalType==10,"IFC fastener class");++boltMatches;continue;
        }
        const auto guid=ifc730::guid(ifc730::string(e.args.at(0)));
        require(oldGuids.at(guid)==id && actual.count(id),"IFC GUID/tag disagrees with old snapshot");
        const auto& props=properties.at(item.first);
        const auto text=[&](const std::string& key){const auto& v=props.at(key);require(v.rfind("IFCLABEL(",0)==0 && v.back()==')',"IFC label type");return ifc730::string(v.substr(9,v.size()-10));};
        const auto point=[&](const std::string& key){Vec3 v{};for(std::size_t n=0;n<3;++n){const auto& raw=props.at(key+"XYZ"[n]);require(raw.rfind("IFCLENGTHMEASURE(",0)==0 && raw.back()==')',"IFC length type");v[n]=ifc730::real(raw.substr(17,raw.size()-18));}return v;};
        require(sampleText(p.name)==ifc730::string(e.args.at(2)) && sampleText(p.name)==text("Nom"),"IFC name "+tag);
        require(sampleText(p.material)==text("Matériau") && p.classNumber==text("Classe"),"IFC material/class "+tag);
        near(p.start,point("Origine"),.002,"IFC start "+tag);near(p.end,point("Extrémité"),.002,"IFC end "+tag);
        // 22 plate profiles are rewritten by the exporter (widths/PLAT aliases).
        // Do not treat their differing names as verified profile semantics.
        if(sampleText(p.profile)==text("Profil"))++profileMatches;
        ++partMatches;
    }
    require(partMatches==66 && boltMatches==20 && bolts.size()==20 && profileMatches==44,"independent evidence coverage");
    // The extra current part is absent from this earlier IFC; no invented match.
    require(actual.count(10123) && !seen.count(10123),"unexported part scope");
    RawDatabase raw;require(parseRawDatabase(directory/"u52_2007vierge.db1",raw,error),error);
    require(raw.tables.size()==217 && raw.tables.at(188).records.size()==7,"weld table scope");
    for(const auto& row:raw.tables.at(188).records)
    {
        require(row.payload.size()==60,"weld row width");std::uint32_t id=0,type=0;float size=0;
        std::memcpy(&id,row.payload.data(),4);std::memcpy(&size,row.payload.data()+8,4);std::memcpy(&type,row.payload.data()+12,4);
        const auto& d=model.weldDefinitions.at(id);require(d.size==size && d.type==type,"raw weld offset consistency");
    }
    tekla::Project project;tekla::ProjectOptions options;options.strictCompanions=true;
    require(tekla::readProject(directory,project,error,options),error);
    require(project.componentLibrary.has_value(),"project library");
    std::size_t partial=0;
    for(const auto& file:project.files)if(file.role==tekla::FileRole::Model || file.role==tekla::FileRole::ComponentLibrary)
    {require(file.level==tekla::ReadLevel::PartialSemantic && !file.diagnostic.empty(),"coverage overstated");++partial;}
    require(partial==2,"project model/library scope");
    std::cout<<"7.64: 67 snapshot parts, 66 IFC parts, 20 IFC bolt placements, 44 exact IFC profiles; GUID migration and remaining geometry unverified\n";
    return 0;
}
