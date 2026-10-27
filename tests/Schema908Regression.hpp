std::vector<Table> schema908(bool library)
{
    auto main=schema();main.resize(339);
    const auto define=[&](std::size_t n,std::uint32_t width,std::size_t count,std::initializer_list<std::size_t> refs) {
        auto& t=main[n];t.payload=width;t.fields.assign(count,0);t.rows.clear();for(auto ref:refs)t.fields[ref]=1;
    };
    define(293,55,6,{0,1,2,3,4});define(329,44,9,{0,1,7,8});
    define(299,332,19,{0,1,12});define(116,116,6,{0,1,5});define(327,312,29,{0,1});
    if(!library)return main;
    std::vector<Table> out(303);
    for(const auto& p:std::vector<std::pair<std::size_t,std::size_t>>{
        {61,40},{64,43},{65,44},{75,53},{122,95},{154,126},{160,132},{161,133},{190,160},{191,161},{192,162},
        {293,260},{329,293},{299,264},{328,292},{116,90},{274,242},{270,238},{228,198},{300,265},{327,291},
        {310,274},{332,296},{294,261},{245,215},{211,181},{322,286},{323,287},{324,288},
        {183,153},{184,154},{205,175},{206,176}})out[p.second]=main[p.first];
    const auto modern=componentLibrary(false);
    for(auto n:{68,147,156,226}){out[n]=modern[n];out[n].rows.clear();}
    return out;
}

void schema908Regression(const std::filesystem::path& path,const std::string& name)
{
    using namespace tekla::db1;
    const bool library=name.find("library")!=std::string::npos;
    auto t=schema908(library);
    const auto point=library?40U:61U,frame=library?44U:65U,strings=library?53U:75U;
    const auto identity=library?260U:293U,cls=library?293U:329U,definition=library?264U:299U;
    const auto part=library?242U:274U,contour=library?292U:328U,link=library?238U:270U;
    auto a=row(t[point],1),b=row(t[point],2);put<double>(b,9,20);t[point].rows={a,b};
    auto f=row(t[frame],0);put<double>(f,1,1);put<double>(f,33,1);put<std::uint32_t>(f,49,3);t[frame].rows={f};
    auto c=row(t[cls],6);put<std::uint32_t>(c,5,name=="schema908_unknown_kind"?39:2);put<std::uint32_t>(c,41,0xabc123);t[cls].rows={c};
    auto i=row(t[identity],5);put<std::uint32_t>(i,5,6);str(i,17,"01234567-0000-0000-0000-000000000001");t[identity].rows={i};
    auto d=row(t[definition],4);put<std::uint32_t>(d,9,2);str(d,55,"PLATE");str(d,77,"IPE");str(d,145,"WRONG_PREFIX");
    str(d,229,"S355");str(d,261,"FINISH");put<std::uint32_t>(d,141,10);t[definition].rows={d};
    auto s=row(t[strings],10);str(s,17,"200");t[strings].rows={s};
    auto p=row(t[part],5);put<std::uint32_t>(p,5,4);put<std::uint32_t>(p,13,1);put<std::uint32_t>(p,17,2);put<std::uint32_t>(p,25,3);
    put<std::uint32_t>(p,21,7);t[part].rows={p};
    auto l=row(t[link],7);put<std::uint32_t>(l,17,8);t[link].rows={l};
    auto block=row(t[contour],8);put<double>(block,17,10000000000.125);put<double>(block,97,-2.25);put<double>(block,177,3.5);
    put<std::uint32_t>(block,341,0x7fffffff);t[contour].rows={block};
    const bool bolt=name.find("schema908_bolt")==0 || name=="schema908_library_bolt";
    if(bolt)
    {
        const auto bd=library?291U:327U,bg=library?274U:310U,place=library?43U:64U;
        auto def=row(t[bd],12);put<std::uint32_t>(def,21,1);str(def,51,"BOLT");str(def,157,"A325N");
        std::fill(def.begin()+253,def.begin()+313,0x7f);t[bd].rows={def};
        auto group=row(t[bg],11);put<std::uint32_t>(group,5,12);put<std::uint32_t>(group,13,1);put<std::uint32_t>(group,17,2);put<std::uint32_t>(group,21,8);t[bg].rows={group};
        auto position=row(t[place],11);put<std::uint32_t>(position,5,3);t[place].rows={position};
        auto identityRow=row(t[identity],11);put<std::uint32_t>(identityRow,5,13);t[identity].rows.push_back(identityRow);
        auto classRow=row(t[cls],13);put<std::uint32_t>(classRow,5,10);t[cls].rows.push_back(classRow);
    }
    bool bad=false;
    if(name=="schema908_bolt_missing_identity"){t[identity].rows.pop_back();bad=true;}
    if(name=="schema908_bolt_duplicate"){auto& rows=t[library?274:310].rows;rows.push_back(rows[0]);bad=true;}
    if(name=="schema908_bolt_wrong_kind"){put<std::uint32_t>(t[cls].rows.back(),5,2);bad=true;}
    if(name=="schema908_bolt_missing_placement"){t[library?43:64].rows.clear();bad=true;}
    if(name=="schema908_bolt_origin_nan"){put<double>(t[library?43:64].rows[0],9,std::numeric_limits<double>::quiet_NaN());bad=true;}
    if(name.find("bad_class")!=std::string::npos){put<std::uint32_t>(t[identity].rows[0],5,999);bad=true;}
    if(name=="schema908_bad_owner"){put<std::uint32_t>(t[identity].rows[0],9,999);bad=true;}
    if(name=="schema908_bad_signature"){t[cls].fields[7]=0;bad=true;}
    if(name=="schema908_bad_bolt_signature"){t[library?291:327].fields[28]=1;bad=true;}
    if(name=="schema908_bad_contour_signature"){t[contour].payload=332;t[contour].rows.clear();bad=true;}
    if(name=="schema908_missing_string"){t[strings].rows.clear();bad=true;}
    if(name=="schema908_string_cycle"){put<std::uint32_t>(t[strings].rows[0],5,10);bad=true;}
    if(name=="schema908_bad_point"){put<std::uint32_t>(t[part].rows[0],13,999);bad=true;}
    if(name=="schema908_nonfinite"){put<double>(t[contour].rows[0],17,std::numeric_limits<double>::infinity());bad=true;}
    if(name=="schema908_wrong_count"){t.push_back(Table{});bad=true;}
    save(path,encode(t,"9.08"));Model model;std::string error;
    const bool ok=library?parseComponentLibrary(path,model,error):parseModelFile(path,model,error);
    if(bad){check(!ok && !error.empty() && model.parts.empty(),"9.08 invalid input accepted or result retained");return;}
    check(ok,error.c_str());check(model.storageVersion=="9.08" && model.parts.size()==1,"9.08 model missing");
    check(model.definitions.at(4).profile=="IPE200" && model.definitions.at(4).material=="S355","9.08 field crossed into prefix or finish");
    check(model.identityClasses.at(6).rawPayload.size()==44 && model.identityClasses.at(6).rawPayload[40]==0x23,"9.08 class tail lost");
    check(model.parts.at(5).contour.size()==1 && model.parts.at(5).contour[0].value==Vec3{10000000000.125,-2.25,3.5},"9.08 double contour misread");
    if(name=="schema908_unknown_kind")check(model.actualPartIds.empty() && model.unhandledPartIds==std::vector<std::uint32_t>{5},"9.08 unknown kind reclassified as part");
    else check(model.actualPartIds==std::vector<std::uint32_t>{5},"9.08 physical part classification");
    if(bolt)
    {
        const auto& bd=model.boltDefinitions.at(12);
        check(bd.legacy908Parameters && bd.legacy908Parameters->back()==0x7f && !bd.legacy844Parameters && bd.diameter==0 && bd.tolerance==0 && bd.length==0 && bd.extraLength==0 && bd.boltType==0,"9.08 unknown bolt parameter interpreted");
        check(model.boltGroups.size()==1 && model.boltGroups[0].positions.size()==1 && model.boltGroups[0].positions[0]==model.parts.at(5).contour[0].value,"9.08 bolt group position join");
    }
}
