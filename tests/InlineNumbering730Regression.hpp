// Minimal sequential 7.30 model, independent of the later linked-record schema.
void inlineNumbering730Regression(const std::filesystem::path& path,const std::string& name)
{
    using namespace tekla::db1;
    std::map<unsigned,Table> t;
    for(const auto& spec:std::vector<std::pair<unsigned,unsigned>>{{1,67},{2,88},{3,40},{6,104},{10,56},{13,32},{16,60},{20,24},{32,60},{36,332},{38,24},{44,372},{47,52},{48,44},{94,60},{95,68},{98,108}})
        t[spec.first].payload=spec.second;
    const auto make=[&](unsigned table,unsigned id){Bytes r(t.at(table).payload+1);r[0]=4;put(r,1,id);return r;};
    auto identity=make(1,100);put<unsigned>(identity,21,200);t[1].rows={identity,make(1,200)};
    auto a=make(13,1),b=make(13,2);put<double>(b,9,10);t[13].rows={a,b};
    auto frame=make(47,0);put<double>(frame,1,1);put<double>(frame,33,1);put<unsigned>(frame,49,3);t[47].rows={frame};
    auto def=make(44,4);str(def,81,"1");str(def,103,"TEST");str(def,125,"PL10*10");str(def,271,"S235");t[44].rows={def};
    auto part=make(10,100);put<unsigned>(part,5,4);put<unsigned>(part,9,1);put<unsigned>(part,13,2);put<unsigned>(part,21,3);put<double>(part,49,10);t[10].rows={part};
    auto assembly=make(2,200);put<unsigned>(assembly,5,15);put<unsigned>(assembly,13,100);str(assembly,21,"ASM");t[2].rows={assembly};
    const std::string prefix="P\xe9";
    auto number=make(94,100);put<unsigned>(number,5,1);put<unsigned>(number,9,8);put<unsigned>(number,13,77);str(number,17,prefix);t[94].rows={number};
    auto an=make(95,200);put<unsigned>(an,5,1);put<unsigned>(an,9,2);put<unsigned>(an,13,78);put<unsigned>(an,17,79);put<unsigned>(an,21,80);str(an,25,"A");t[95].rows={an};
    if(name=="inline_number_730_zero")put<unsigned>(t[94].rows[0],9,0);
    if(name=="inline_number_730_non1")put<unsigned>(t[94].rows[0],5,100);
    if(name=="inline_number_730_zero_start")put<unsigned>(t[94].rows[0],5,0);
    if(name=="inline_number_730_special")put<unsigned>(t[94].rows[0],9,0x80000001U);
    if(name=="inline_number_730_tag")t[94].rows[0][0]=5;
    if(name=="inline_number_730_tag12")t[94].rows[0][0]=12;
    if(name=="inline_number_730_duplicate")t[94].rows.push_back(t[94].rows[0]);
    if(name=="inline_number_730_missing_record")t[94].rows.clear();
    if(name=="inline_number_730_identity")t[1].rows.erase(t[1].rows.begin());
    if(name=="inline_number_730_wrong_kind")put<unsigned>(t[95].rows[0],1,100);
    if(name=="inline_number_730_zero_id")put<unsigned>(t[94].rows[0],1,0);
    if(name=="inline_number_730_width"){t[95].payload=72;t[95].rows[0].resize(73);}
    if(name=="inline_number_730_empty")for(auto table:{2,10,94,95})t[table].rows.clear();
    const auto encode730=[&](){const std::string header="Xsteel  7.30";Bytes v(header.begin(),header.end());for(const auto& item:t){u32(v,0x00bc614f);u32(v,item.first);u32(v,static_cast<unsigned>(item.second.rows.size()));u32(v,item.second.payload);for(const auto& row:item.second.rows)v.insert(v.end(),row.begin(),row.end());}return v;};
    auto bytes=encode730();save(path,bytes);Model model;std::string error;
    const bool ok=name=="inline_number_730_library"?parseComponentLibrary(path,model,error):parseModelFile(path,model,error);
    const bool invalid=name=="inline_number_730_tag"||name=="inline_number_730_duplicate"||name=="inline_number_730_missing_record"||name=="inline_number_730_identity"||name=="inline_number_730_wrong_kind"||name=="inline_number_730_zero_id"||name=="inline_number_730_width";
    if(invalid){check(!ok && !error.empty() && model.objectNumberingRecords.empty() && model.identities.empty(),"invalid inline numbering accepted/partial state kept");return;}
    check(ok,error.c_str());check(model.objectNumberingReferences.empty(),"fabricated stored references for inline numbering");
    if(name=="inline_number_730_empty"){check(model.objectNumberingRecords.empty(),"fabricated empty inline records");return;}
    check(model.objectNumberingRecords.size()==2,"inline record count");const auto& p=model.objectNumberingRecords.at(100);const auto& ar=model.objectNumberingRecords.at(200);
    check(p.inlineObjectId==100 && p.storedNumber && p.sequence==0 && p.prefix==prefix && p.rawPayload.size()==60 && p.rawPayload[12]==77,"inline part fields/source/bytes lost");
    check(ar.inlineObjectId==200 && ar.storedNumber==2 && ar.positionNumber==2 && ar.sequence==0 && ar.rawPayload.size()==68 && ar.rawPayload[12]==78 && ar.rawPayload[16]==79 && ar.rawPayload[20]==80,"inline assembly fields/source lost");
    if(name=="inline_number_730_zero"||name=="inline_number_730_non1"||name=="inline_number_730_zero_start"||name=="inline_number_730_special")check(!p.positionNumber,"unverified inline position fabricated");
    else check(p.positionNumber==8,"verified start-one position missing");
    if(name=="inline_number_730_non1")check(p.startNumber==100 && p.storedNumber==8,"non-one raw number changed");
    check(std::any_of(model.parts.at(100).properties.begin(),model.parts.at(100).properties.end(),[](const auto& q){return q.name=="ModelGroup" && q.stringValue=="P\xc3\xa9";}),"compatibility prefix alias changed");
    if(name.find("inline_number_730_project_")==0)
    {
        const bool mismatch=name=="inline_number_730_project_version";
        const auto db2=[&](std::vector<std::string> keys){const std::string h=mismatch?"Xsteel\x80 7.82":"Xsteel  7.30";Bytes v(h.begin(),h.end());u32(v,22);u32(v,static_cast<unsigned>(keys.size()));u32(v,76);for(const auto& key:keys){v.push_back(4);Bytes row(76);str(row,0,key);v.insert(v.end(),row.begin(),row.end());}u32(v,0x00bc614f);return v;};
        std::vector<std::string> keys{prefix+"/1","A/1"};if(name=="inline_number_730_project_missing_series")keys={"A/1"};if(name=="inline_number_730_project_ambiguous")keys.push_back(prefix+"/01");
        if(name!="inline_number_730_project_absent")save(path.parent_path()/"model.db2",db2(keys));
        if(name=="inline_number_730_project_library_scope"){save(path.parent_path()/"xslib.db1",bytes);const std::string h="Xsteel  7.30";save(path.parent_path()/"xslib.db2",Bytes(h.begin(),h.end()));}
        tekla::Project project;tekla::ProjectOptions options;options.mainDatabase=path.filename();options.readRawCompanions=false;options.trustLegacyNumberingBasenames=name!="inline_number_730_project_default";
        check(tekla::readProject(path.parent_path(),project,error,options),error.c_str());
        const unsigned expected=(name=="inline_number_730_project_default"||name=="inline_number_730_project_absent"||mismatch)?0:(name=="inline_number_730_project_missing_series"||name=="inline_number_730_project_ambiguous")?1:2;
        check(project.objectNumberingSeriesAssociations.size()==expected,"inline DB2 pair scope/missing/ambiguous behavior");
        for(const auto& link:project.objectNumberingSeriesAssociations)check(link.database==project.model.databasePath && link.objectId==link.numberingRecordId && link.pairingEvidence==tekla::NumberingPairEvidence::ExplicitLegacyBasename,"inline numbering cross-file source/evidence");
        if(name=="inline_number_730_project_library_scope")check(project.componentLibrary && project.componentLibrary->objectNumberingRecords.size()==2,"missing series discarded inline component assignments");
    }
    save(path,{1,2,3});check(!parseModelFile(path,model,error) && model.objectNumberingRecords.empty() && model.objectNumberingReferences.empty(),"failed reread retained inline numbering");
}
