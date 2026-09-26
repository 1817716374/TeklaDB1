// Included after the synthetic schema helpers.
void objectNumberingRegression(const std::filesystem::path& path, const std::string& name)
{
    using namespace tekla::db1;
    const bool library=name=="object_number_library" || name=="object_number_895";
    const bool older=name=="object_number_895";
    auto s=library?componentLibrary(older):onePart();
    const auto first=library?286U:322U, links=library?181U:211U;
    auto part=row(s[first],100); put<std::uint32_t>(part,9,100); put<std::uint32_t>(part,13,8); str(part,21,"P");
    put<std::uint32_t>(part,5,42); put<std::uint32_t>(part,17,43); s[first].rows={part};
    auto assembly=row(s[first+1],200); put<std::uint32_t>(assembly,9,1); put<std::uint32_t>(assembly,13,2); str(assembly,29,"A"); s[first+1].rows={assembly};
    auto unknown=row(s[first+2],300); put<std::uint32_t>(unknown,9,999); put<std::uint32_t>(unknown,13,123); unknown[44]=0xab; s[first+2].rows={unknown};
    auto link=row(s[links],5); put<std::uint32_t>(link,5,585288); put<std::uint32_t>(link,9,100); s[links].rows={link};
    if (name=="object_number_zero") put<std::uint32_t>(s[first].rows[0],13,0);
    if (name=="object_number_special") put<std::uint32_t>(s[first].rows[0],9,0x80000001U);
    if (name=="object_number_overflow") put<std::uint32_t>(s[first].rows[0],13,0xffffffffU);
    if (name=="object_number_unknown") put<std::uint32_t>(s[links].rows[0],9,300);
    if (name=="object_number_null") put<std::uint32_t>(s[links].rows[0],9,0);
    if (name=="object_number_missing") put<std::uint32_t>(s[links].rows[0],9,999);
    if (name=="object_number_identity") put<std::uint32_t>(s[links].rows[0],1,999);
    if (name=="object_number_duplicate") s[first].rows.push_back(s[first].rows[0]);
    if (name=="object_number_collision") put<std::uint32_t>(s[first+1].rows[0],1,100);
    if (name=="object_number_duplicate_link") s[links].rows.push_back(s[links].rows[0]);
    if (name=="object_number_fields") s[first].fields[3]=1;
    if (name=="object_number_link_fields") s[links].fields[1]=0;
    if (name=="object_number_width") {s[first].payload=72;s[first].rows.clear();}
    if (name=="object_number_link_width") {s[links].payload=16;s[links].rows.clear();}
    const std::string databaseGuid="01234567-89ab-cdef-0123-456789abcdef";
    auto bytes=encode(s,older?"8.95":"9.60");
    bytes[11]=' '; str(bytes,12,databaseGuid); save(path,bytes);
    Model model; std::string error;
    const bool ok=library?parseComponentLibrary(path,model,error):parseModelFile(path,model,error);
    const bool invalid=name=="object_number_missing" || name=="object_number_identity" || name=="object_number_duplicate" || name=="object_number_collision" || name=="object_number_duplicate_link" || name=="object_number_fields" || name=="object_number_link_fields" || name=="object_number_width" || name=="object_number_link_width";
    if (invalid)
    {
        check(!ok && !error.empty() && model.objectNumberingRecords.empty() && model.objectNumberingReferences.empty() && model.identities.empty(),"bad object numbering accepted or partial model retained");
        return;
    }
    check(ok,error.c_str()); check(model.objectNumberingRecords.size()==3,"unused numbering records discarded");
    const auto& record=model.objectNumberingRecords.at(100);
    check(record.kind==ObjectNumberingKind::Part && record.prefix=="P" && record.rawPayload[4]==42 && record.rawPayload[16]==43,"number fields or opaque flags lost");
    if (name=="object_number_zero" || name=="object_number_special" || name=="object_number_overflow") check(!record.positionNumber,"special/zero/overflow range fabricated an assigned number");
    else check(record.positionNumber && *record.positionNumber==107,"start+sequence-1 derivation wrong");
    const auto& a=model.objectNumberingRecords.at(200); check(a.kind==ObjectNumberingKind::Assembly && a.prefix=="A" && a.positionNumber==2,"assembly numbering mapping wrong");
    const auto& raw=model.objectNumberingRecords.at(300); check(raw.kind==ObjectNumberingKind::Unverified && raw.rawPayload.size()==44 && raw.rawPayload.back()==0xab && !raw.positionNumber && raw.prefix.empty(),"unverified numbering record guessed or lost");
    check(model.objectNumberingReferences.at(5).rawContext==585288,"numbering context lost");
    if (name.rfind("object_number_project",0)==0)
    {
        const auto db2=[&](const std::string& guid,const std::vector<std::string>& keys) {
            const std::string h="Xsteel\x80 9.60 "+guid; Bytes b(h.begin(),h.end()); u32(b,22); u32(b,static_cast<std::uint32_t>(keys.size())); u32(b,80);
            for (const auto& key:keys) { b.push_back(4); Bytes r(80); str(r,0,key); b.insert(b.end(),r.begin(),r.end()); }
            u32(b,0x00bc614f); return b;
        };
        std::vector<std::string> keys{"P/100"};
        if (name=="object_number_project_series") keys={"OTHER/100"};
        if (name=="object_number_project_ambiguous") keys.push_back("P/0100");
        if (name!="object_number_project_absent") save(path.parent_path()/"model.db2",db2(name=="object_number_project_guid"?"11234567-89ab-cdef-0123-456789abcdef":databaseGuid,keys));
        if (name=="object_number_project_no_guid") {std::fill(bytes.begin()+12,bytes.begin()+48,0);save(path,bytes);}
        if (name=="object_number_project_library_scope")
        {
            auto lib=componentLibrary(false); lib[286].rows={part}; lib[181].rows={link};
            auto content=encode(lib,"9.60"); content[11]=' '; str(content,12,"11234567-89ab-cdef-0123-456789abcdef");save(path.parent_path()/"xslib.db1",content);
            save(path.parent_path()/"xslib.db2",db2("11234567-89ab-cdef-0123-456789abcdef",keys));
        }
        tekla::Project project; tekla::ProjectOptions options; options.mainDatabase=path.filename(); options.readRawCompanions=false;
        // The legacy opt-in cannot weaken a modern GUID conflict.
        if (name=="object_number_project_guid") options.trustLegacyNumberingBasenames=true;
        check(tekla::readProject(path.parent_path(),project,error,options),error.c_str());
        const auto expected=name=="object_number_project"?1U:name=="object_number_project_library_scope"?2U:0U;
        check(project.objectNumberingSeriesAssociations.size()==expected,"project numbering scope/missing/ambiguous series mishandled");
        for (const auto& association:project.objectNumberingSeriesAssociations)
        {
            check(association.objectId==5 && association.numberingRecordId==100 && association.seriesIndex==0,"project number association wrong");
            check(association.database.stem()==association.numberingDatabase.stem(),"number association crossed database namespaces");
            check(association.pairingEvidence==tekla::NumberingPairEvidence::DatabaseGuid,"modern pair evidence mislabeled");
        }
        if (!expected) check(std::any_of(project.diagnostics.begin(),project.diagnostics.end(),[](const auto& d){return d.find("object numbering")!=std::string::npos && (d.find("DB2")!=std::string::npos);}),"missing project numbering diagnostic");
    }
    save(path,{1,2,3});
    check(!parseModelFile(path,model,error) && model.objectNumberingRecords.empty() && model.objectNumberingReferences.empty(),"failed reread kept numbering state");
}
