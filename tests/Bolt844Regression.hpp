void bolt844Regression(const std::filesystem::path& path,const std::string& name)
{
    using namespace tekla::db1;
    const bool library=name.find("library")!=std::string::npos;
    auto s=empty844(library);
    const auto def=library?222U:252U,group=library?221U:251U,point=library?40U:61U,
        frame=library?44U:65U,identity=library?179U:209U,contour=library?94U:121U;
    const bool multi=name.find("multi")!=std::string::npos;
    const bool empty=name=="bolt844_empty",null=name=="bolt844_null";
    const auto count=empty?0U:multi?12U:2U;
    s[point].rows={row(s[point],1),row(s[point],2)};
    auto f=row(s[frame],0);put<double>(f,1,1);put<double>(f,33,1);put<std::uint32_t>(f,49,3);s[frame].rows={f};
    auto d=row(s[def],20);put<std::uint32_t>(d,21,count);str(d,157,"8.8");put<float>(d,261,16);put<float>(d,273,2);put<float>(d,297,30);put<std::uint32_t>(d,293,100000);s[def].rows={d};
    auto g=row(s[group],30);put<std::uint32_t>(g,5,20);put<std::uint32_t>(g,13,1);put<std::uint32_t>(g,17,2);
    put<std::uint32_t>(g,21,null?0:40);put<std::uint32_t>(g,25,3);put<double>(g,33,100);put<double>(g,41,200);put<double>(g,49,300);put<double>(g,57,50);s[group].rows={g};
    auto ident=row(s[identity],30);str(ident,25,"ID7998C610-11CA-4333-8D41-1FC4375B943E");s[identity].rows={ident};
    std::vector<Vec3> expected;
    for (unsigned chunk=0;chunk<(multi?2U:1U);++chunk)
    {
        auto c=row(s[contour],40);put<std::uint32_t>(c,5,chunk);
        const auto n=std::min(10U,count-chunk*10);
        for(unsigned i=0;i<n;++i)
        {
            const float x=static_cast<float>(chunk*10+i+10),y=-675.0f,z=3.0f;
            put<float>(c,13+i*4,x);put<float>(c,53+i*4,y);put<float>(c,93+i*4,z);
            put<float>(c,293+i*4,5.0f);expected.push_back({x,y,z});
        }
        if(n<10)put<std::uint32_t>(c,213+n*4,0x7fffffff);
        s[contour].rows.push_back(c);
    }
    if(name=="bolt844_missing_definition")s[def].rows.clear();
    if(name=="bolt844_missing_point")put<std::uint32_t>(s[group].rows[0],13,999);
    if(name=="bolt844_missing_frame")put<std::uint32_t>(s[group].rows[0],25,999);
    if(name=="bolt844_missing_array")put<std::uint32_t>(s[group].rows[0],21,999);
    if(name=="bolt844_missing_identity")s[identity].rows.clear();
    if(name=="bolt844_duplicate_group")s[group].rows.push_back(g);
    if(name=="bolt844_duplicate_definition")s[def].rows.push_back(d);
    if(name=="bolt844_origin_nan")put<double>(s[group].rows[0],33,std::numeric_limits<double>::quiet_NaN());
    if(name=="bolt844_position_nan")put<float>(s[contour].rows[0],13,std::numeric_limits<float>::quiet_NaN());
    if(name=="bolt844_diameter_nan")put<float>(s[def].rows[0],261,std::numeric_limits<float>::quiet_NaN());
    if(name=="bolt844_count")put<std::uint32_t>(s[def].rows[0],21,3);
    if(name=="bolt844_sequence")put<std::uint32_t>(s[contour].rows[0],5,1);
    if(name=="bolt844_group_fields")s[group].fields[2]=0;
    if(name=="bolt844_definition_fields")s[def].fields[2]=1;
    const bool good=name=="bolt844_valid" || name=="bolt844_library" || multi || empty || null;
    save(path,encode(s,"8.44"));Model m;std::string error;
    const bool ok=library?parseComponentLibrary(path,m,error):parseModelFile(path,m,error);
    if(!good){check(!ok && !error.empty() && m.boltGroups.empty(),"invalid 8.44 bolt accepted");return;}
    check(ok,error.c_str());check(m.boltGroups.size()==1,"missing 8.44 bolt group");const auto& b=m.boltGroups[0];const auto& definition=m.boltDefinitions.at(20);
    check(b.positions==(null?std::vector<Vec3>{}:expected),"8.44 first/full-chunk position lost");
    check(b.positionArrayId==(null?0U:40U) && b.origin==Vec3{100,200,300} && b.axis==Vec3{1,0,0} && b.secondary==Vec3{0,1,0} && b.normal==Vec3{0,0,1},"8.44 inline placement");
    check(definition.count==count && definition.diameter==16 && definition.tolerance==2 && definition.length==30 && definition.standard=="8.8" && definition.legacy844Parameters.has_value(),"8.44 definition fields");
    check(definition.extraLength==0 && definition.boltType==0 && b.connectedPartIds.empty() && b.layers.empty(),"unverified 8.44 settings invented");
    check(std::equal(definition.legacy844Parameters->begin(),definition.legacy844Parameters->end(),d.begin()+253),"8.44 original numeric settings lost");
}
