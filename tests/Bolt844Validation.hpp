int bolt844Validation(const std::filesystem::path& directory)
{
    using namespace ifc730;
    tekla::db1::Model model;std::string error;
    require(tekla::db1::parseModelFile(directory/"Philippe_Boineau_CCF_BE_AMCR_2019.db1.bak",model,error),error);
    require(model.storageVersion=="8.44" && model.boltGroups.size()==79,"8.44 bolt scope");
    const auto all=read(directory/"PB_AUVENT.ifc");
    std::map<std::string,const tekla::db1::BoltGroup*> groups;
    for(const auto& g:model.boltGroups)require(groups.emplace(g.guid,&g).second,"duplicate bolt GUID");
    std::map<unsigned,std::string> standards;
    std::map<unsigned,double> propertyDiameters;
    for(const auto& entry:all)
    {
        const auto& e=entry.second;if(e.type!="IFCRELDEFINESBYPROPERTIES")continue;
        const auto& set=all.at(entityRef(e.args.at(5)));if(set.type!="IFCPROPERTYSET")continue;
        for(auto property:refs(set.args.at(4)))
        {
            const auto& p=entity(all,property,"IFCPROPERTYSINGLEVALUE",4);
            if(string(p.args[0])=="Diam\xc3\xa8tre")
            {
                const auto& value=p.args[2];const auto open=value.find('(');
                require(open!=std::string::npos && value.back()==')',"diameter property value");
                for(auto target:refs(e.args[4]))propertyDiameters[target]=real(value.substr(open+1,value.size()-open-2));
            }
            if(string(p.args[0])!="Standard")continue;
            const auto& v=p.args[2];require(v.size()>10 && v.substr(0,9)=="IFCLABEL(" && v.back()==')',"standard value");
            for(auto target:refs(e.args[4]))standards[target]=string(v.substr(9,v.size()-10));
        }
    }
    std::set<std::string> seen;std::size_t total=0;bool tolerancePair=false;
    for(const auto& entry:all)
    {
        const auto& e=entry.second;if(e.type!="IFCMECHANICALFASTENER")continue;
        require(e.args.size()==10,"fastener attributes");const auto id=guid(string(e.args[0]));
        require(seen.insert(id).second && groups.count(id),"bolt GUID pairing");const auto& g=*groups.at(id);
        const auto& d=model.boltDefinitions.at(g.definitionId);
        require(d.legacy844Parameters.has_value() && d.standard==standards.at(entry.first),"8.44 bolt settings/standard");
        // Direct IFC attributes are nominal values. The optional property set
        // diameter is 14 for a hole-only M12 group and must not replace them.
        coordinateNear(d.diameter,real(e.args[8]),1e-6,"nominal bolt diameter");
        coordinateNear(d.length,real(e.args[9]),1e-6,"nominal bolt length");
        if(id=="IDC016E8ED-FA34-4A95-8781-669A0E65F501")
        {
            require(d.diameter==12 && propertyDiameters.at(entry.first)==14,"fixed nominal/hole pair");
            coordinateNear(d.tolerance,propertyDiameters.at(entry.first)-d.diameter,1e-6,"diametral hole tolerance");
            tolerancePair=true;
        }
        const auto f=placement(all,entityRef(e.args[5]));
        for(std::size_t i=0;i<3;++i)coordinateNear(g.origin[i],f.origin[i],.002,"bolt origin");
        const auto& shape=entity(all,entityRef(e.args[6]),"IFCPRODUCTDEFINITIONSHAPE",3);
        const auto reps=refs(shape.args[2]);require(reps.size()==1,"bolt representation count");
        const auto& rep=entity(all,reps[0],"IFCSHAPEREPRESENTATION",4);
        std::vector<V> expected;
        for(auto item:refs(rep.args[3]))
        {
            const auto& mapped=entity(all,item,"IFCMAPPEDITEM",2);
            const auto& mapping=entity(all,entityRef(mapped.args[0]),"IFCREPRESENTATIONMAP",2);
            const auto origin=frame(all,entityRef(mapping.args[0]));
            for(std::size_t i=0;i<3;++i)
            {
                coordinateNear(origin.origin[i],0,1e-12,"map origin");
                coordinateNear(origin.x[i],i==0?1:0,1e-12,"map X");
                coordinateNear(origin.y[i],i==1?1:0,1e-12,"map Y");
                coordinateNear(origin.z[i],i==2?1:0,1e-12,"map Z");
            }
            const auto& t=entity(all,entityRef(mapped.args[1]),"IFCCARTESIANTRANSFORMATIONOPERATOR3D",5);
            require(t.args[0]=="$" && t.args[1]=="$" && t.args[3]=="$" && t.args[4]=="$","mapping basis/scale");
            const auto v=vector(all,entityRef(t.args[2]),"IFCCARTESIANPOINT");V world{};
            for(std::size_t i=0;i<3;++i)world[i]=f.origin[i]+v[0]*f.x[i]+v[1]*f.y[i]+v[2]*f.z[i];
            expected.push_back(world);
        }
        require(g.positions.size()==expected.size() && d.count==expected.size(),"8.44 full position count");
        for(const auto& p:g.positions)
        {
            V world{};for(std::size_t i=0;i<3;++i)world[i]=g.origin[i]+p[0]*g.axis[i]+p[1]*g.secondary[i]+p[2]*g.normal[i];
            const auto found=std::find_if(expected.begin(),expected.end(),[&](const V& v){return std::abs(v[0]-world[0])<.002 && std::abs(v[1]-world[1])<.002 && std::abs(v[2]-world[2])<.002;});
            require(found!=expected.end(),"8.44 bolt world position");expected.erase(found);++total;
        }
    }
    require(seen.size()==79 && total==201 && tolerancePair,"8.44 bolt evidence coverage");
    std::cout<<"version=8.44 groups=79 positions=201 diameters=79 lengths=79 standards=79 origins=79 tolerance_pairs=1\n";
    return 0;
}
