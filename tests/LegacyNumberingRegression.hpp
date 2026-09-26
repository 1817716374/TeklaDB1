// Included after the synthetic DB1 schema helpers.
void legacyNumberingRegression(const std::filesystem::path& path,const std::string& name)
{
    using namespace tekla::db1;
    const bool library=name=="legacy_number_library";
    auto s=onePart782(library); const auto first=library?185U:215U, links=library?181U:211U;
    auto part=row(s[first],100); put<std::uint32_t>(part,5,2000); put<std::uint32_t>(part,9,5); put<std::uint32_t>(part,13,77); str(part,17,"P");
    s[first].rows={part};
    auto assembly=row(s[first+1],200); put<std::uint32_t>(assembly,5,1000); put<std::uint32_t>(assembly,9,2); str(assembly,25,"A");
    put<std::uint32_t>(assembly,13,78); put<std::uint32_t>(assembly,17,79); put<std::uint32_t>(assembly,21,80); s[first+1].rows={assembly};
    auto link=row(s[links],5); put<std::uint32_t>(link,5,1703144); put<std::uint32_t>(link,9,100); s[links].rows={link};
    if (name=="legacy_number_fields") s[first].fields[1]=1;
    if (name=="legacy_number_width") {s[first].payload=68;s[first].rows.clear();}
    if (name=="legacy_number_link_fields") s[links].fields[1]=1;
    if (name=="legacy_number_link_width") {s[links].payload=16;s[links].rows.clear();}
    if (name=="legacy_number_duplicate") s[first].rows.push_back(s[first].rows[0]);
    if (name=="legacy_number_collision") put<std::uint32_t>(s[first+1].rows[0],1,100);
    if (name=="legacy_number_duplicate_link") s[links].rows.push_back(s[links].rows[0]);
    if (name=="legacy_number_missing_identity") put<std::uint32_t>(s[links].rows[0],1,999);
    if (name=="legacy_number_missing_target") put<std::uint32_t>(s[links].rows[0],9,999);
    if (name=="legacy_number_zero") put<std::uint32_t>(s[first].rows[0],9,0);
    if (name=="legacy_number_special") put<std::uint32_t>(s[first].rows[0],5,0x80000001U);
    auto bytes=encode(s,"7.82",library); save(path,bytes);
    Model model; std::string error; const bool ok=library?parseComponentLibrary(path,model,error):parseModelFile(path,model,error);
    const bool valid=name=="legacy_number_main" || library || name=="legacy_number_zero" || name=="legacy_number_special" || name.rfind("legacy_number_project_",0)==0;
    if (!valid)
    {
        check(!ok && !error.empty() && model.objectNumberingRecords.empty() && model.objectNumberingReferences.empty() && model.identities.empty(),"malformed legacy numbering accepted or partially retained");
        return;
    }
    check(ok,error.c_str()); check(model.objectNumberingRecords.size()==2,"legacy numbering records dropped or third layout fabricated");
    const auto& p=model.objectNumberingRecords.at(100); const auto& a=model.objectNumberingRecords.at(200);
    check(p.kind==ObjectNumberingKind::Part && p.prefix=="P" && p.rawPayload.size()==64 && p.rawPayload[12]==77,"legacy part numbering fields changed");
    if (name=="legacy_number_zero" || name=="legacy_number_special") check(!p.positionNumber,"legacy special range fabricated number");
    else check(p.startNumber==2000 && p.sequence==5 && p.positionNumber==2004,"legacy part number derivation");
    check(a.kind==ObjectNumberingKind::Assembly && a.startNumber==1000 && a.sequence==2 && a.prefix=="A" && a.positionNumber==1001 && a.rawPayload[12]==78 && a.rawPayload[16]==79 && a.rawPayload[20]==80,"legacy assembly numbering or unknown fields changed");
    check(model.objectNumberingReferences.at(5).rawContext==1703144 && model.objectNumberingReferences.at(5).numberingRecordId==100,"legacy number reference lost");
    if (name.rfind("legacy_number_project_",0)==0)
    {
        std::string header="Xsteel\x80 7.82";
        if (name=="legacy_number_project_modern_pair")
        {
            header="Xsteel\x80 9.60 11234567-89ab-cdef-0123-456789abcdef";
            // Matching GUIDs still do not verify different storage layouts.
            bytes[11]=' '; str(bytes,12,"11234567-89ab-cdef-0123-456789abcdef"); save(path,bytes);
        }
        if (name=="legacy_number_project_conflict")
        {
            bytes[11]=' '; str(bytes,12,"01234567-89ab-cdef-0123-456789abcdef"); save(path,bytes);
            header+=" 11234567-89ab-cdef-0123-456789abcdef";
        }
        Bytes db2(header.begin(),header.end()); u32(db2,22); u32(db2,1); u32(db2,name=="legacy_number_project_modern_pair"?80:76); db2.push_back(4);
        Bytes series(name=="legacy_number_project_modern_pair"?80:76); str(series,0,"P/2000"); db2.insert(db2.end(),series.begin(),series.end()); u32(db2,0x00bc614f); save(path.parent_path()/"model.db2",db2);
        tekla::ProjectOptions options; options.mainDatabase=path.filename(); options.readRawCompanions=false;
        options.trustLegacyNumberingBasenames=name!="legacy_number_project_default";
        tekla::Project project; check(tekla::readProject(path.parent_path(),project,error,options),error.c_str());
        if (name=="legacy_number_project_trusted")
        {
            check(project.objectNumberingSeriesAssociations.size()==1,"explicit legacy pair lost");
            const auto& association=project.objectNumberingSeriesAssociations[0];
            check(association.pairingEvidence==tekla::NumberingPairEvidence::ExplicitLegacyBasename && association.objectId==5 && association.numberingRecordId==100 && association.seriesIndex==0,"legacy pair evidence or scope wrong");
        }
        else check(project.objectNumberingSeriesAssociations.empty() && std::any_of(project.diagnostics.begin(),project.diagnostics.end(),[](const auto& d){return d.find("object numbering DB1/DB2")!=std::string::npos;}),"unscoped/conflicting numbering pair accepted without evidence");
    }
    save(path,{1,2,3}); check(!parseModelFile(path,model,error) && model.objectNumberingRecords.empty() && model.objectNumberingReferences.empty(),"failed reread kept legacy numbering");
}
