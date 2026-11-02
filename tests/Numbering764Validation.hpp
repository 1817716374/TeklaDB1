int numbering764Validation(const std::filesystem::path& path,const std::string& mode)
{
    using namespace tekla::db1;
    const auto require=[](bool ok,const std::string& why){if(!ok)throw std::runtime_error("7.64 numbering: "+why);};
    std::string error;
    if(mode=="numbering764_fields")
    {
        tekla::NumberingDatabase database;require(tekla::parseNumberingDatabase(path,database,error),error);
        require(database.raw.storageVersion=="7.64" && database.raw.databaseGuid.empty(),"DB2 version/scope");
        for(const auto& s:database.series)
        {
            std::cout<<s.key<<'\t'<<s.startNumber<<'\t'<<s.partCounter<<'\t'<<s.assemblyCounter;
            for(auto word:s.additionalFields)std::cout<<'\t'<<word;
            std::cout<<'\n';
        }
        return 0;
    }
    tekla::Project project;tekla::ProjectOptions options;options.strictCompanions=true;
    if(mode=="numbering764_raw_off")options.readRawCompanions=false;
    require(tekla::readProject(path,project,error,options),error);
    require(project.model.storageVersion=="7.64" && project.model.objectNumberingRecords.size()==75 &&
        project.componentLibrary && project.componentLibrary->objectNumberingRecords.size()==5,"DB1 scope/records");
    require(project.objectNumberingSeriesAssociations.empty(),"unscoped automatic numbering pair");
    options.trustLegacyNumberingBasenames=true;
    require(tekla::readProject(path,project,error,options),error);
    if(mode=="numbering764_unpaired")
    {require(project.objectNumberingSeriesAssociations.empty(),"wrong-version/missing DB2 paired");std::cout<<"unpaired=0 records=75 component_records=5\n";return 0;}
    require(project.numbering.size()==2,"DB2 inventory");
    const auto dbPath=path/"u52_2007vierge.db2",libPath=path/"xslib.db2";
    const auto& db=project.numbering.at(dbPath);const auto& lib=project.numbering.at(libPath);
    require(db.raw.storageVersion=="7.64" && db.raw.tables.size()==9 && db.series.size()==4 &&
        lib.raw.storageVersion=="7.64" && lib.raw.tables.empty() && lib.series.empty(),"native DB2 count baseline");
    struct Entry{std::string series,position;};std::map<std::pair<ObjectNumberingKind,unsigned>,Entry> history;
    std::map<std::string,std::pair<unsigned,unsigned>> maxima;
    const std::regex assignment(R"((Part|Assembly)\s+id:\s*(\d+)\s+series:(.*?)\s+.*?->\s+(.*?)\s*$)");
    const std::regex maximum(R"(\s*(\S+/\d+|/\d+)\s+Max Part\s+(\d+)\s+Max Rebar\s+\d+\s+Max Assembly\s+(\d+)\s+Max Family\s+\d+\s+Max Qualifier\s+\d+\s*)");
    std::ifstream input(path/"numbering.history",std::ios::binary);require(bool(input),"missing native numbering history");
    std::string line;std::smatch match;
    while(std::getline(input,line))
    {
        if(std::regex_match(line,match,assignment))history[{match[1]=="Part"?ObjectNumberingKind::Part:ObjectNumberingKind::Assembly,static_cast<unsigned>(std::stoul(match[2]))}]={match[3],match[4]};
        if(std::regex_match(line,match,maximum))maxima[match[1]]={static_cast<unsigned>(std::stoul(match[2])),static_cast<unsigned>(std::stoul(match[3]))};
    }
    require(input.eof() && history.size()==105,"history read/coverage");
    for(const auto& series:db.series)
    {
        const auto m=maxima.find(series.key);require(m!=maxima.end(),"series missing from history");
        require(m->second.first==series.partCounter && m->second.second==series.assemblyCounter,"DB2 counters differ from history");
    }
    std::size_t parts=0,assemblies=0;std::set<unsigned> missing;
    for(const auto& h:history)
    {
        const auto ref=project.model.objectNumberingReferences.find(h.first.second);
        if(ref==project.model.objectNumberingReferences.end() || !ref->second.numberingRecordId){missing.insert(h.first.second);continue;}
        const auto& n=project.model.objectNumberingRecords.at(ref->second.numberingRecordId);
        require(n.kind==h.first.first && n.prefix+"/"+std::to_string(n.startNumber)==h.second.series &&
            n.positionNumber && n.prefix+"/"+std::to_string(*n.positionNumber)==h.second.position,"DB1 stored number differs from history");
        if(n.kind==ObjectNumberingKind::Part)++parts;else ++assemblies;
    }
    require(parts==65 && assemblies==38 && missing==std::set<unsigned>{3957,3961},"object history coverage changed");
    std::set<unsigned> linked;
    for(const auto& a:project.objectNumberingSeriesAssociations)
    {
        require(a.database==project.model.databasePath && a.numberingDatabase==dbPath &&
            a.pairingEvidence==tekla::NumberingPairEvidence::ExplicitLegacyBasename && linked.insert(a.objectId).second,"association scope/provenance");
        const auto& n=project.model.objectNumberingRecords.at(a.numberingRecordId);const auto& s=db.series.at(a.seriesIndex);
        require(n.prefix==s.prefix && n.startNumber==s.startNumber,"association series fields");
    }
    require(linked.size()==105 && linked.count(4527) && linked.count(5069),"series link coverage");
    for(auto id:{4527U,5069U})
    {
        const auto& n=project.model.objectNumberingRecords.at(project.model.objectNumberingReferences.at(id).numberingRecordId);
        require(n.sequence==0 && !n.positionNumber,"unassigned object fabricated a position");
    }
    std::cout<<"series=4 history_counters=8 history_parts=65 history_assemblies=38 missing_history_objects=2 explicit_series_links=105 unassigned_positions=2 component_links=0\n";
    return 0;
}
