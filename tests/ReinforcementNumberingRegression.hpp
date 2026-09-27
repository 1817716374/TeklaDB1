void reinforcementNumberingRegression(const std::filesystem::path& path,const std::string& name)
{
    using namespace tekla::db1;
    const bool older=name=="rebar_number_895" || name=="rebar_number_wrong_kind895";
    const bool library=older || name=="rebar_number_library" || name=="rebar_number_952";
    auto s=library?componentLibrary(older):onePart();
    const auto numbers=library?288U:324U,refs=library?181U:211U;
    const auto defs=library?153U:183U,inst=library?154U:184U,ident=library?(older?260U:318U):355U;
    const auto place=library?43U:64U,assoc=library?161U:191U;
    s[defs].rows={row(s[defs],600)};
    if(older){auto c=row(s[279],647);put<uint32_t>(c,5,47);s[279].rows.push_back(c);}
    const auto addRebar=[&](uint32_t id){
        auto identity=row(s[ident],id);if(older){put<uint32_t>(identity,5,647);put<uint32_t>(identity,9,50);}
        else {put<uint32_t>(identity,29,47);if(library)put<uint32_t>(identity,5,50);}s[ident].rows.push_back(identity);
        auto r=row(s[inst],id);put<uint32_t>(r,5,600);s[inst].rows.push_back(r);
        auto p=row(s[place],id);put<uint32_t>(p,5,3);s[place].rows.push_back(p);
        auto parent=row(s[assoc],id+1000);put<uint32_t>(parent,5,47);put<uint32_t>(parent,9,5);put(parent,13,id);s[assoc].rows.push_back(parent);
        auto link=row(s[refs],id);put<uint32_t>(link,5,77);put<uint32_t>(link,9,700);s[refs].rows.push_back(link);
    };
    addRebar(650);
    auto n=row(s[numbers],700);put<uint32_t>(n,5,99);put<uint32_t>(n,9,47);put<uint32_t>(n,13,100);str(n,17,"R\xe9");s[numbers].rows={n};
    if(name=="rebar_number_zero")put<uint32_t>(s[numbers].rows[0],13,0);
    if(name=="rebar_number_special")put<uint32_t>(s[numbers].rows[0],13,0x80000001U);
    if(name=="rebar_number_raw_zero")put<uint32_t>(s[numbers].rows[0],9,0);
    if(name=="rebar_number_raw_high")put<uint32_t>(s[numbers].rows[0],9,0xffffffffU);
    if(name=="rebar_number_full_prefix")std::fill(s[numbers].rows[0].begin()+17,s[numbers].rows[0].begin()+45,uint8_t('X'));
    if(name=="rebar_number_unreferenced")s[refs].rows.clear();
    if(name=="rebar_number_null")put<uint32_t>(s[refs].rows[0],9,0);
    if(name=="rebar_number_shared")addRebar(651);
    if(name=="rebar_number_wrong_kind")put<uint32_t>(s[refs].rows[0],1,5);
    if(name=="rebar_number_wrong_kind895")put<uint32_t>(s[279].rows.back(),5,2);
    if(name=="rebar_number_missing")put<uint32_t>(s[refs].rows[0],9,999);
    if(name=="rebar_number_fields")s[numbers].fields[3]=1;
    if(name=="rebar_number_width"){s[numbers].payload=48;s[numbers].rows[0].resize(57);}
    if(name=="rebar_number_duplicate")s[numbers].rows.push_back(s[numbers].rows[0]);
    if(name=="rebar_number_zero_id"){put<uint32_t>(s[numbers].rows[0],1,0);put<uint32_t>(s[refs].rows[0],9,0);}
    const auto version=older?"8.95":name=="rebar_number_952"?"9.52":"9.60";
    const std::string guid="01234567-89ab-cdef-0123-456789abcdef";
    auto bytes=encode(s,version);bytes[11]=' ';str(bytes,12,guid);save(path,bytes);
    Model m;std::string error;const bool ok=library?parseComponentLibrary(path,m,error):parseModelFile(path,m,error);
    const bool invalid=name=="rebar_number_wrong_kind" || name=="rebar_number_wrong_kind895" || name=="rebar_number_missing" || name=="rebar_number_fields" || name=="rebar_number_width" || name=="rebar_number_duplicate" || name=="rebar_number_zero_id";
    if(invalid){check(!ok && !error.empty() && m.objectNumberingRecords.empty() && m.objectNumberingReferences.empty(),"invalid rebar numbering accepted or partial state retained");return;}
    check(ok,error.c_str());const auto& value=m.objectNumberingRecords.at(700);
    check(value.kind==ObjectNumberingKind::Reinforcement && value.startNumber==(name=="rebar_number_zero"?0U:name=="rebar_number_special"?0x80000001U:100U),"rebar series kind/start");
    check(value.prefix==(name=="rebar_number_full_prefix"?std::string(28,'X'):std::string("R\xe9")),"rebar prefix bytes or width");
    check(value.rawPayload==Bytes(s[numbers].rows[0].begin()+1,s[numbers].rows[0].begin()+45) && value.sequence==0 && !value.positionNumber && !value.storedNumber && !value.inlineObjectId,"unverified rebar assignment interpreted or bytes lost");
    const auto count=name=="rebar_number_unreferenced"?0U:name=="rebar_number_shared"?2U:1U;
    check(m.objectNumberingReferences.size()==count,"shared/null/unreferenced rebar numbering changed");
    if(count)check(m.objectNumberingReferences.at(650).rawContext==77 && m.objectNumberingReferences.at(650).numberingRecordId==(name=="rebar_number_null"?0U:700U),"rebar reference context lost");
    if(name=="rebar_number_project")
    {
        // Identical DB2 series text is not evidence of a steel-bar counter/link.
        const std::string h="Xsteel\x80 9.60 "+guid;Bytes db(h.begin(),h.end());u32(db,22);u32(db,1);u32(db,80);db.push_back(4);Bytes series(80);str(series,0,"R\xe9/100");db.insert(db.end(),series.begin(),series.end());u32(db,0x00bc614f);save(path.parent_path()/"model.db2",db);
        tekla::Project p;tekla::ProjectOptions options;options.mainDatabase=path.filename();options.readRawCompanions=false;
        check(tekla::readProject(path.parent_path(),p,error,options),error.c_str());
        check(p.objectNumberingSeriesAssociations.empty() && p.model.objectNumberingRecords.at(700).prefix=="R\xe9","unverified rebar DB2 matching fabricated or series lost");
    }
    save(path,{1,2});check(!parseModelFile(path,m,error) && m.objectNumberingRecords.empty() && m.objectNumberingReferences.empty(),"failed reread retained rebar numbering");
}
