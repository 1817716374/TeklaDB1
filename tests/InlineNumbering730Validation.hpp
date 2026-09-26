void validateInlineNumbers730(const tekla::db1::Model& model,const tekla::db1::RawDatabase& raw,bool emit=true)
{
    using namespace tekla::db1;
    if(model.storageVersion!="7.30" || !model.objectNumberingReferences.empty())throw std::runtime_error("inline numbering scope/references");
    const auto word=[](const auto& p,std::size_t at){std::uint32_t n=0;std::memcpy(&n,p.data()+at,4);return n;};
    Fingerprint hash;std::size_t count=0,derived=0;
    for(auto tid:{94U,95U})
    {
        const auto table=std::find_if(raw.tables.begin(),raw.tables.end(),[&](const auto&t){return t.ordinal==tid;});
        if(table==raw.tables.end() || table->payloadSize!=(tid==94?60:68))throw std::runtime_error("inline raw table layout");
        for(const auto& source:table->records)
        {
            const auto& p=source.payload;const auto& record=model.objectNumberingRecords.at(word(p,0));
            const auto begin=p.begin()+(tid==94?16:24),end=std::find(begin,p.end(),0);const std::string prefix(begin,end);
            const auto kind=tid==94?ObjectNumberingKind::Part:ObjectNumberingKind::Assembly;
            if(record.id!=word(p,0) || record.inlineObjectId!=record.id || record.kind!=kind || record.startNumber!=word(p,4) || record.storedNumber!=word(p,8) || record.sequence!=0 || record.prefix!=prefix || record.rawPayload!=p || !model.identities.count(record.id))throw std::runtime_error("inline fields/bytes/object source changed");
            const bool known=word(p,4)==1 && word(p,8)>0 && word(p,8)<0x80000000U;
            if(bool(record.positionNumber)!=known || (known && *record.positionNumber!=word(p,8)))throw std::runtime_error("inline position derivation");
            ++count;derived+=known;hash.number(record.id);hash.number(tid);hash.number(record.startNumber);hash.number(*record.storedNumber);hash.text(record.prefix);for(auto byte:p)hash.byte(byte);
        }
    }
    if(count!=model.objectNumberingRecords.size())throw std::runtime_error("inline numbering coverage");
    if(emit)std::cout<<"inline_records="<<count<<" verified_positions="<<derived<<" stored_references=0 fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
}

void validateInline730History(const std::filesystem::path& directory)
{
    using namespace tekla::db1;
    struct Evidence{std::string series,position;};std::map<std::pair<ObjectNumberingKind,unsigned>,Evidence> latest;
    const std::regex pattern(R"((Part|Assembly)\s+id:\s*(\d+)\s+series:(.*?)\s+.*?->\s+(.*?)\s*$)");
    for(const char* name:{"numbering.history","numberingresults"})
    {
        std::ifstream in(directory/name,std::ios::binary);if(!in)throw std::runtime_error("missing inline numbering history");std::string line;std::smatch match;
        while(std::getline(in,line))if(std::regex_match(line,match,pattern))latest[{match[1]=="Part"?ObjectNumberingKind::Part:ObjectNumberingKind::Assembly,static_cast<unsigned>(std::stoul(match[2]))}]={match[3],match[4]};
    }
    tekla::Project p;tekla::ProjectOptions options;options.readDrawings=false;options.readRawCompanions=false;std::string error;
    if(!tekla::readProject(directory,p,error,options))throw std::runtime_error(error);
    if(!p.objectNumberingSeriesAssociations.empty())throw std::runtime_error("unverified 7.30 automatic pairing");
    options.trustLegacyNumberingBasenames=true;if(!tekla::readProject(directory,p,error,options))throw std::runtime_error(error);
    std::size_t parts=0,assemblies=0;std::set<unsigned> differences;
    for(const auto& item:p.model.objectNumberingRecords)
    {
        const auto& n=item.second;const auto h=latest.find({n.kind,n.id});if(h==latest.end())continue;
        if(n.prefix+"/"+std::to_string(n.startNumber)!=h->second.series)throw std::runtime_error("inline history series mismatch");
        if(n.positionNumber && n.prefix+"/"+std::to_string(*n.positionNumber)==h->second.position){if(n.kind==ObjectNumberingKind::Part)++parts;else ++assemblies;}
        else{if(n.storedNumber!=0 || n.positionNumber)throw std::runtime_error("unexpected history disagreement");differences.insert(n.id);}
    }
    if(parts!=759 || assemblies!=708 || differences!=std::set<unsigned>{74351,74352})throw std::runtime_error("inline history coverage changed");
    const auto verify=[&](const Model& model){RawDatabase raw;if(!parseRawDatabase(model.databasePath,raw,error))throw std::runtime_error(error);validateInlineNumbers730(model,raw,false);};
    verify(p.model);if(!p.componentLibrary)throw std::runtime_error("missing inline component evidence");verify(*p.componentLibrary);
    std::size_t expected=0;
    for(const auto* model:{&p.model,&*p.componentLibrary})
    {
        const auto target=model->databasePath.parent_path()/(model->databasePath.stem().string()+".db2");const auto& db=p.numbering.at(target);
        for(const auto& item:model->objectNumberingRecords)
        {
            const auto& n=item.second;std::vector<std::size_t> indices;for(std::size_t i=0;i<db.series.size();++i)if(db.series[i].prefix==n.prefix && db.series[i].startNumber==n.startNumber)indices.push_back(i);
            const auto link=std::find_if(p.objectNumberingSeriesAssociations.begin(),p.objectNumberingSeriesAssociations.end(),[&](const auto&a){return a.database==model->databasePath && a.objectId==n.id;});
            if(indices.size()==1){++expected;if(link==p.objectNumberingSeriesAssociations.end() || link->numberingDatabase!=target || link->numberingRecordId!=n.id || link->seriesIndex!=indices[0] || link->pairingEvidence!=tekla::NumberingPairEvidence::ExplicitLegacyBasename)throw std::runtime_error("inline project series link changed");}
            else if(link!=p.objectNumberingSeriesAssociations.end())throw std::runtime_error("inline missing series crossed database namespaces");
        }
    }
    if(p.objectNumberingSeriesAssociations.size()!=expected || p.model.objectNumberingRecords.size()!=2908 || p.componentLibrary->objectNumberingRecords.size()!=537)throw std::runtime_error("inline project record/link count");
    std::cout<<"history_parts="<<parts<<" history_assemblies="<<assemblies<<" preserved_zero_differences="<<differences.size()<<" explicit_series_links="<<expected<<" component_records=537\n";
}

void validateInline730Backups(const std::filesystem::path& directory)
{
    using namespace tekla::db1;
    std::vector<std::filesystem::path> files;for(const auto& item:std::filesystem::directory_iterator(directory))if(item.path().filename().string().find(".db1.bak")!=std::string::npos)files.push_back(item.path());std::sort(files.begin(),files.end());
    std::size_t records=0,nonzero=0,linked=0,missing=0,used=0,equal=0;
    for(const auto& path:files)
    {
        Model model;RawDatabase raw;std::string error;const bool library=path.filename().string().rfind("xslib",0)==0;
        if(!(library?parseComponentLibrary(path,model,error):parseModelFile(path,model,error)) || !parseRawDatabase(path,raw,error))throw std::runtime_error(error);
        validateInlineNumbers730(model,raw,false);records+=model.objectNumberingRecords.size();
        auto name=path.filename().string();name.replace(name.size()-8,8,".db2.bak");tekla::NumberingDatabase db;
        if(!tekla::parseNumberingDatabase(directory/name,db,error) || db.raw.storageVersion!="7.30")throw std::runtime_error("backup DB2: "+error);
        std::map<std::pair<std::size_t,ObjectNumberingKind>,unsigned> maxima;
        for(const auto& item:model.objectNumberingRecords)
        {
            const auto& n=item.second;if(!n.storedNumber || !*n.storedNumber)continue;++nonzero;
            if(n.startNumber!=1 || !n.positionNumber)throw std::runtime_error("backup unsupported numbering range");
            const auto s=std::find_if(db.series.begin(),db.series.end(),[&](const auto& s){return s.prefix==n.prefix && s.startNumber==n.startNumber;});
            if(s==db.series.end()){++missing;if(!library || !db.series.empty() || n.kind!=ObjectNumberingKind::Assembly)throw std::runtime_error("unexpected missing backup series");continue;}
            ++linked;const auto counter=n.kind==ObjectNumberingKind::Part?s->partCounter:s->assemblyCounter;
            if(*n.positionNumber>counter)throw std::runtime_error("backup position exceeds series counter");
            auto& maximum=maxima[{static_cast<std::size_t>(s-db.series.begin()),n.kind}];maximum=(std::max)(maximum,*n.positionNumber);
        }
        used+=maxima.size();for(const auto& m:maxima){const auto& s=db.series[m.first.first];equal+=m.second==(m.first.second==ObjectNumberingKind::Part?s.partCounter:s.assemblyCounter);}
    }
    if(files.size()!=34 || records!=36285 || nonzero!=22640 || linked!=22581 || missing!=59 || used!=361 || equal!=303)throw std::runtime_error("backup inline numbering scope changed");
    std::cout<<"backup_pairs=34 inline_records=36285 nonzero=22640 linked=22581 missing_component_series=59 counters_above_current_max=58\n";
}
