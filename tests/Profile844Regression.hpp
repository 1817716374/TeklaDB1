std::vector<Table> empty844(bool library)
{
    std::vector<Table> tables(library ? 254 : 286);
    const std::array<std::array<std::size_t,3>,21> roles{{
        {{61,40,32}}, {{64,43,40}}, {{65,44,52}}, {{75,53,45}},
        {{122,95,44}}, {{154,126,104}}, {{160,132,24}}, {{161,133,24}},
        {{190,160,24}}, {{191,161,60}}, {{192,162,60}}, {{209,179,63}},
        {{245,215,322}}, {{121,94,332}}, {{116,90,116}}, {{273,241,64}},
        {{227,197,100}}, {{181,151,88}}, {{269,237,24}}, {{251,221,64}}, {{252,222,308}}}};
    for (const auto& r:roles) tables[r[library ? 1 : 0]].payload=static_cast<std::uint32_t>(r[2]);
    auto& group=tables[library?221:251];group.fields.assign(12,0);std::fill(group.fields.begin(),group.fields.begin()+8,1);
    auto& definition=tables[library?222:252];definition.fields.assign(28,0);definition.fields[0]=definition.fields[1]=1;
    return tables;
}

void profile844Regression(const std::filesystem::path& path, const std::string& name)
{
    using namespace tekla::db1;
    const bool library = name.find("library") != std::string::npos;
    const bool boundary = name.find("boundary") != std::string::npos;
    auto tables=empty844(library);
    const std::array<std::array<std::size_t,3>,19> roles{{
        {{61,40,32}}, {{64,43,40}}, {{65,44,52}}, {{75,53,45}},
        {{122,95,44}}, {{154,126,104}}, {{160,132,24}}, {{161,133,24}},
        {{190,160,24}}, {{191,161,60}}, {{192,162,60}}, {{209,179,63}},
        {{245,215,322}}, {{121,94,332}}, {{116,90,116}}, {{273,241,64}},
        {{227,197,100}}, {{181,151,88}}, {{269,237,24}}}};
    for (const auto& role : roles) tables[role[library ? 1 : 0]].payload=static_cast<std::uint32_t>(role[2]);
    const auto at = [&](std::size_t ordinal) -> Table& {
        for (const auto& role : roles) if (role[0]==ordinal) return tables[role[library ? 1 : 0]];
        throw std::runtime_error("unknown 8.44 fixture role");
    };
    auto a=row(at(61),1), b=row(at(61),2); put<double>(b,9,10); at(61).rows={a,b};
    auto frame=row(at(65),0); put<double>(frame,1,1); put<double>(frame,33,1); put<std::uint32_t>(frame,49,3); at(65).rows={frame};
    auto def=row(at(245),4); str(def,55,"POTEAU"); str(def,77,"IPE"); str(def,145,"PT"); str(def,229,"S275J0"); str(def,261,"GALVA"); put<std::uint32_t>(def,141,90);
    auto chunk=row(at(75),90); str(chunk,17,"160"); at(75).rows={chunk};
    if (boundary)
    {
        std::fill(def.begin()+55,def.begin()+77,'N');
        std::fill(def.begin()+77,def.begin()+141,'P');
        std::fill(def.begin()+229,def.begin()+261,'M');
    }
    if (name.find("missing")!=std::string::npos) at(75).rows.clear();
    if (name.find("next")!=std::string::npos) put<std::uint32_t>(at(75).rows[0],5,91);
    if (name.find("cycle")!=std::string::npos) put<std::uint32_t>(at(75).rows[0],5,90);
    at(245).rows={def};
    at(209).rows={row(at(209),5)};
    auto part=row(at(273),5); put<std::uint32_t>(part,5,4); put<std::uint32_t>(part,13,1); put<std::uint32_t>(part,17,2); put<std::uint32_t>(part,25,3); at(273).rows={part};
    if (name=="profile844_contour")
    {
        put<std::uint32_t>(at(273).rows[0],21,60);
        auto link=row(at(269),60);put<std::uint32_t>(link,17,70);at(269).rows={link};
        auto c=row(at(121),70);
        for(unsigned i=0;i<10;++i){put<float>(c,13+i*4,12.0f+i);put<float>(c,53+i*4,-675.0f);put<float>(c,93+i*4,4.0f);put<float>(c,293+i*4,7.0f);}
        at(121).rows={c};
    }
    save(path,encode(tables,"8.44")); Model model; std::string error;
    const bool ok=library ? parseComponentLibrary(path,model,error) : parseModelFile(path,model,error);
    const bool bad=name.find("missing")!=std::string::npos || name.find("next")!=std::string::npos || name.find("cycle")!=std::string::npos;
    if (bad) { check(!ok && !error.empty() && model.parts.empty(),"invalid 8.44 dimension chain accepted"); return; }
    check(ok,error.c_str()); const auto& p=model.parts.at(5);
    check(p.name==(boundary?std::string(22,'N'):"POTEAU"),"8.44 name crosses profile boundary");
    check(p.profile==(boundary?std::string(64,'P')+"160":"IPE160"),"8.44 numbering prefix used as profile family");
    check(p.material==(boundary?std::string(32,'M'):"S275J0"),"8.44 material crosses finish boundary");
    if (name=="profile844_contour") check(p.contour.size()==10 && p.contour.front().value==Vec3{12,-675,4} && p.contour.back().value==Vec3{21,-675,4} && p.contour.back().chamferDz2==7,"8.44 full coordinate/chamfer arrays");
}
