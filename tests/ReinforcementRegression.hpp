void reinforcementRegression(const std::filesystem::path& path,const std::string& name)
{
    using namespace tekla::db1;
    const bool library=name=="reinforcement_library" || name=="reinforcement_895";
    const bool older=name=="reinforcement_895";
    auto s=library?componentLibrary(older):onePart();
    const auto defs=library?153U:183U, inst=library?154U:184U, floats=library?175U:205U, ints=library?176U:206U;
    const auto strings=library?53U:75U, ident=library?(older?260U:318U):355U;
    const auto place=library?43U:64U, assoc=library?161U:191U;
    for (const auto& t:std::vector<std::pair<uint32_t,std::string>>{{601,"STIRRUP"},{602,"T460"},{603,"10"}})
    {auto r=row(s[strings],t.first);str(r,17,t.second);s[strings].rows.push_back(r);}
    auto mode=row(s[ints],610);put<uint32_t>(mode,9,7);put<int32_t>(mode,21,-99);put<uint32_t>(mode,13,0xffffffffU);s[ints].rows={mode};
    const auto addArray=[&](uint32_t id,const std::vector<double>& values,uint32_t next=0) {
        auto r=row(s[floats],id);put(r,5,next);put<uint32_t>(r,9,static_cast<uint32_t>(values.size()));
        for (std::size_t i=0;i<values.size();++i) put(r,25+i*8,values[i]);
        put<uint32_t>(r,13,0x7ff80000); // opaque metadata is not a coordinate
        s[floats].rows.push_back(r);
    };
    addArray(620,{0,0,0,10,0,0,10,10,0,0,10,0},621); addArray(621,{0,0,0});
    addArray(622,{20});addArray(623,{3});addArray(624,{90,20,120,90,20,120});
    auto def=row(s[defs],600);put<uint32_t>(def,5,610);put<uint32_t>(def,9,4);
    put<uint32_t>(def,13,601);put<uint32_t>(def,17,602);put<uint32_t>(def,21,603);put<uint32_t>(def,25,624);put<uint32_t>(def,29,0x7fc01234);
    auto unused=def;put<uint32_t>(unused,1,699);s[defs].rows={def,unused};
    auto identity=row(s[ident],650);
    if (older) {auto cls=row(s[279],647);put<uint32_t>(cls,5,47);s[279].rows.push_back(cls);put<uint32_t>(identity,5,647);put<uint32_t>(identity,9,50);}
    else {put<uint32_t>(identity,29,47);if(library)put<uint32_t>(identity,5,50);}
    s[ident].rows.push_back(identity);
    auto rebar=row(s[inst],650);put<uint32_t>(rebar,5,600);put<uint32_t>(rebar,17,620);put<uint32_t>(rebar,21,622);put<uint32_t>(rebar,25,623);
    put<uint32_t>(rebar,45,0x7ff80000);s[inst].rows={rebar};
    auto p=row(s[place],650);put<uint32_t>(p,5,3);put<double>(p,9,100);put<double>(p,33,1000);s[place].rows.push_back(p);
    auto link=row(s[assoc],660);put<uint32_t>(link,5,47);put<uint32_t>(link,9,5);put<uint32_t>(link,13,650);s[assoc].rows.push_back(link);
    if(name=="reinforcement_duplicate")s[inst].rows.push_back(rebar);
    if(name=="reinforcement_duplicate_definition")s[defs].rows.push_back(def);
    if(name=="reinforcement_definition")put<uint32_t>(s[inst].rows[0],5,999);
    if(name=="reinforcement_identity")s[ident].rows.pop_back();
    if(name=="reinforcement_kind")put<uint32_t>(s[ident].rows.back(),29,99);
    if(name=="reinforcement_missing_instance")s[inst].rows.clear();
    if(name=="reinforcement_father")s[assoc].rows.pop_back();
    if(name=="reinforcement_father_target")put<uint32_t>(s[assoc].rows.back(),9,999);
    if(name=="reinforcement_father_duplicate")s[assoc].rows.push_back(link);
    if(name=="reinforcement_placement")s[place].rows.pop_back();
    if(name=="reinforcement_duplicate_placement")s[place].rows.push_back(p);
    if(name=="reinforcement_nonfinite_placement")put<double>(s[place].rows.back(),9,std::numeric_limits<double>::infinity());
    if(name=="reinforcement_array_missing")put<uint32_t>(s[inst].rows[0],17,999);
    if(name=="reinforcement_array_cycle")put<uint32_t>(s[floats].rows[1],5,620);
    if(name=="reinforcement_array_tail")put<uint32_t>(s[floats].rows[1],5,999);
    if(name=="reinforcement_array_count")put<uint32_t>(s[floats].rows[0],9,13);
    if(name=="reinforcement_integer_count")put<uint32_t>(s[ints].rows[0],9,11);
    if(name=="reinforcement_array_duplicate")s[floats].rows.push_back(s[floats].rows[0]);
    if(name=="reinforcement_nonfinite")put<double>(s[floats].rows[2],25,std::numeric_limits<double>::quiet_NaN());
    if(name=="reinforcement_coordinate_count")put<uint32_t>(s[floats].rows[1],9,2);
    if(name=="reinforcement_string")put<uint32_t>(s[defs].rows[0],13,999);
    if(name=="reinforcement_fields")s[defs].fields[3]=1;
    if(name=="reinforcement_width"){s[inst].payload=60;s[inst].rows.clear();}
    if(name=="reinforcement_array_fields")s[floats].fields[3]=1;
    if(name=="reinforcement_null") {put<uint32_t>(s[inst].rows[0],25,0);put<uint32_t>(s[defs].rows[0],25,0);}
    const bool budget=name=="reinforcement_budget" || name=="reinforcement_budget_override";
    if(name=="reinforcement_shared" || budget)
    {
        if(budget)
        {
            for(uint32_t i=0;i<100;++i)addArray(2000+i,std::vector<double>(12,0),i==99?0:2001+i);
            put<uint32_t>(rebar,17,2000);s[inst].rows[0]=rebar;
        }
        const uint32_t copies=budget?149:1;
        for(uint32_t i=0;i<copies;++i)
        {
            const auto id=1000+i;auto r=rebar;put(r,1,id);s[inst].rows.push_back(r);
            auto idrow=identity;put(idrow,1,id);s[ident].rows.push_back(idrow);
            auto placement=p;put(placement,1,id);s[place].rows.push_back(placement);
            auto parent=link;put(parent,1,3000+i);put(parent,13,id);s[assoc].rows.push_back(parent);
        }
    }
    save(path,encode(s,older?"8.95":"9.60"));Model m;std::string error;
    ModelReadOptions options;if(budget)options.maxReinforcementArrayValues=name=="reinforcement_budget"?4096:1000000;
    const bool ok=library?parseComponentLibrary(path,m,error,options):parseModelFile(path,m,error,options);
    if(name=="reinforcement_budget_override") {check(ok,error.c_str());check(m.reinforcements.size()==150 && m.reinforcements.at(1148).storedShapeCoordinates.size()==400,"configured expansion budget lost shared arrays");return;}
    const bool valid=name=="reinforcement_main" || library || name=="reinforcement_null" || name=="reinforcement_shared";
    if(!valid){check(!ok && !error.empty() && m.reinforcements.empty() && m.reinforcementDefinitions.empty() && m.reinforcementIdsByFather.empty(),"invalid reinforcement accepted or failed parse retained state");if(name=="reinforcement_budget")check(error.find("expansion budget")!=std::string::npos,error.c_str());return;}
    check(ok,error.c_str());check(m.reinforcements.size()==(name=="reinforcement_shared"?2U:1U) && m.reinforcementDefinitions.size()==2,"reinforcement/shared unused definition lost");
    const auto& d=m.reinforcementDefinitions.at(600); const auto& r=m.reinforcements.at(650);
    check(d.name=="STIRRUP" && d.grade=="T460" && d.size=="10" && d.classNumber==4 && d.modeValues[0]==-99,"reinforcement definition mapping");
    check(d.rawPayload.size()==32 && d.rawPayload[28]==0x34 && r.rawPayload.size()==56 && r.rawPayload[45]==0,"opaque reinforcement payload lost");
    check(r.storedShapeCoordinates.size()==5 && r.storedShapeCoordinates.front()==r.storedShapeCoordinates.back() && r.storedShapeCoordinates[2]==Vec3{10,10,0},"chained coordinates changed");
    check(r.radiusValues==std::vector<double>{20} && r.fatherPartId==5 && r.origin==Vec3{100,0,0} && r.orientationId==3 && r.storedLength==1000,"reinforcement scalar/parent/placement mapping");
    check(m.reinforcementIdsByFather.at(5)==(name=="reinforcement_shared"?std::vector<uint32_t>{650,1000}:std::vector<uint32_t>{650}),"reinforcement father index");
    check(r.spacingValues==(name=="reinforcement_null"?std::vector<double>{}:std::vector<double>{3}),"null or bar count treated as missing/spacing conversion");
    save(path,{1,2});check(!parseModelFile(path,m,error) && m.reinforcements.empty() && m.reinforcementDefinitions.empty() && m.reinforcementIdsByFather.empty(),"failed reread retained reinforcement");
}
