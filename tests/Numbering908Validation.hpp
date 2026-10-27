int numbering908Validation(const std::filesystem::path& directory,bool unpaired=false)
{
    using namespace tekla;
    const auto require=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    const bool september=directory.filename()=="api-exam908-september";
    require(september || directory.filename()=="api-exam908-july","unknown 9.08 evidence snapshot");
    const auto history=directory.parent_path()/"api-exam908-july/numberinghistory.txt";
    std::ifstream input(history);require(bool(input),"9.08 official numbering history missing");
    struct Evidence {std::string kind,series,prefix;std::uint32_t number;};
    std::map<std::string,Evidence> evidence;
    std::map<std::pair<std::string,std::string>,std::uint32_t> maxima;
    const std::regex pattern(R"(^(Part|Assembly)\s+guid:\s+([\w-]+)\s+series:(.*?)\s+.*? -> (.*)/(\d+)\s*$)");
    std::string line;std::smatch match;
    while(std::getline(input,line))if(std::regex_match(line,match,pattern))
    {
        Evidence item{match[1].str(),match[3].str(),match[4].str(),static_cast<std::uint32_t>(std::stoul(match[5].str()))};
        require(evidence.emplace(match[2].str(),item).second,"duplicate historical GUID in pinned 9.08 evidence");
        auto& maximum=maxima[{item.kind,item.series}];maximum=std::max(maximum,item.number);
    }
    require(evidence.size()==4936 && maxima.size()==3,"9.08 numbering history coverage changed");
    Project project;ProjectOptions options;options.strictCompanions=true;std::string error;
    require(readProject(directory,project,error,options),error);
    require(project.model.storageVersion=="9.08" && project.numbering.size()==2 && project.componentLibrary.has_value(),"9.08 numbering project incomplete");
    if(unpaired)
    {
        require(project.objectNumberingSeriesAssociations.empty() && std::any_of(project.diagnostics.begin(),project.diagnostics.end(),[](const auto& s){
            return s.find("object numbering DB1/DB2")!=std::string::npos;
        }),"mismatched DB2 version/GUID did not suppress object pairing");
        std::cout<<"unpaired_db2=1 objects_preserved="<<project.model.actualPartIds.size()<<'\n';return 0;
    }
    const auto db2Path=directory/"API_Developer_Exam_01.db2";
    const auto& database=project.numbering.at(db2Path);
    require(database.series.size()==3 && database.raw.tables.size()==10,"9.08 main DB2 schema changed");
    const auto& library=project.numbering.at(directory/"xslib.db2");
    require(library.series.empty() && library.raw.tables.empty() && library.raw.preamble.size()==49,"9.08 header-only xslib DB2 changed");
    std::size_t counters=0,numberingFiles=0;
    for(const auto& file:project.files)if(file.role==FileRole::Numbering)
    {require(file.level==ReadLevel::PartialSemantic,"DB2 overstates semantics");++numberingFiles;}
    require(numberingFiles==2,"9.08 DB2 project file scope");
    for(const auto& series:database.series)for(bool assembly:{false,true})
    {
        const auto count=assembly?series.assemblyCounter:series.partCounter;
        const auto found=maxima.find({assembly?"Assembly":"Part",series.key});
        if(found==maxima.end()){require(count==0,"unexpected nonzero DB2 counter");continue;}
        require(count && std::uint64_t(series.startNumber)+count-1==found->second,"DB2 counter disagrees with historical maximum");++counters;
    }
    require(counters==3,"9.08 independent counter coverage changed");
    std::map<std::string,std::uint32_t> identities;
    for(const auto& item:project.model.identities)if(!item.second.guid.empty())
        require(identities.emplace(item.second.guid,item.first).second,"ambiguous 9.08 GUID");
    std::set<std::uint32_t> linked;
    for(const auto& link:project.objectNumberingSeriesAssociations)
    {
        require(link.database==project.model.databasePath && link.numberingDatabase==db2Path &&
            link.pairingEvidence==NumberingPairEvidence::DatabaseGuid,"9.08 numbering association scope changed");
        const auto& record=project.model.objectNumberingRecords.at(link.numberingRecordId);
        const auto& series=database.series.at(link.seriesIndex);
        require(series.prefix==record.prefix && series.startNumber==record.startNumber &&
            project.model.objectNumberingReferences.at(link.objectId).numberingRecordId==record.id &&
            linked.insert(link.objectId).second,"9.08 numbering association broken or duplicated");
    }
    std::size_t parts=0,assemblies=0;std::set<std::string> missing;
    for(const auto& entry:evidence)
    {
        const auto identity=identities.find(entry.first);
        if(identity==identities.end()){missing.insert(entry.first);continue;}
        const auto& expected=entry.second;
        const auto& reference=project.model.objectNumberingReferences.at(identity->second);
        const auto& record=project.model.objectNumberingRecords.at(reference.numberingRecordId);
        const auto kind=expected.kind=="Part"?db1::ObjectNumberingKind::Part:db1::ObjectNumberingKind::Assembly;
        require(record.kind==kind && record.positionNumber && *record.positionNumber==expected.number &&
            record.prefix==expected.prefix && record.prefix+"/"+std::to_string(record.startNumber)==expected.series,
            "9.08 DB1 assignment disagrees with independent history");
        require(linked.count(identity->second)==1,"historically verified 9.08 object lost DB2 association");
        if(kind==db1::ObjectNumberingKind::Part)++parts;else ++assemblies;
    }
    const std::set<std::string> expectedMissing=september?std::set<std::string>{
        "68a5241b-0cba-40b4-a7f8-2dc6b1975189","3bd0c7f1-0c2a-4854-b1e9-41ebeba14f37"}:std::set<std::string>{};
    require(parts==4677 && assemblies==(september?257U:259U) && missing==expectedMissing,"9.08 historical identity coverage changed");
    std::cout<<"version=9.08 log_parts="<<parts<<" log_assemblies="<<assemblies<<" historical_missing="<<missing.size()
        <<" counters="<<counters<<" project_series_links="<<linked.size()<<'\n';
    return 0;
}
