// Included after Fingerprint inside validate.cpp's anonymous namespace.
void validateObjectNumbers(const tekla::db1::Model& model, const tekla::db1::RawDatabase& raw, bool library)
{
    using namespace tekla::db1;
    Fingerprint hash; std::size_t records=0, derived=0, linked=0, unknown=0, nulls=0;
    const auto word=[](const std::vector<std::uint8_t>& p,std::size_t at) { std::uint32_t n=0; std::memcpy(&n,p.data()+at,4); return n; };
    const bool legacy=model.storageVersion=="7.82";
    const auto first=legacy?(library?185U:215U):(library?286U:322U);
    for (std::size_t kind=0;kind<(legacy?2U:3U);++kind)
        for (const auto& source:raw.tables.at(first+kind).records)
        {
            const auto& p=source.payload; const auto& r=model.objectNumberingRecords.at(word(p,0));
            if (r.rawPayload!=p || r.id!=word(p,0)) throw std::runtime_error("numbering raw payload lost");
            const auto expectedKind=kind==0?ObjectNumberingKind::Part:kind==1?ObjectNumberingKind::Assembly:ObjectNumberingKind::Unverified;
            if (r.kind!=expectedKind) throw std::runtime_error("numbering kind changed");
            if (kind<2)
            {
                const auto start=(kind==0?20:28)-(legacy?4:0);
                const auto end=std::find(p.begin()+start,p.end(),0);
                if (r.startNumber!=word(p,legacy?4:8) || r.sequence!=word(p,legacy?8:12) || r.prefix!=std::string(p.begin()+start,end))
                    throw std::runtime_error("numbering field mapping changed");
                const auto number=std::uint64_t(r.startNumber)+r.sequence;
                const bool expected=r.startNumber>0 && r.startNumber<0x80000000U && r.sequence>0 && number<=0x80000000ULL;
                if (bool(r.positionNumber)!=expected || (expected && *r.positionNumber!=number-1)) throw std::runtime_error("numbering derived position changed");
            }
            else if (r.positionNumber || r.startNumber || r.sequence || !r.prefix.empty()) throw std::runtime_error("unverified numbering semantics fabricated");
            ++records; derived+=bool(r.positionNumber);
            hash.number(r.id); hash.number(static_cast<unsigned>(r.kind)); hash.number(r.startNumber); hash.number(r.sequence); hash.text(r.prefix);
            hash.number(bool(r.positionNumber)); if (r.positionNumber) hash.number(*r.positionNumber);
            for (auto v:r.rawPayload) hash.byte(v);
        }
    const auto& references=raw.tables.at(library?181:211).records;
    for (const auto& source:references)
    {
        const auto& p=source.payload; const auto& r=model.objectNumberingReferences.at(word(p,0));
        if (r.objectId!=word(p,0) || r.rawContext!=word(p,4) || r.numberingRecordId!=word(p,8) || !model.identities.count(r.objectId))
            throw std::runtime_error("numbering reference changed");
        if (!r.numberingRecordId) ++nulls;
        else if (model.objectNumberingRecords.at(r.numberingRecordId).kind==ObjectNumberingKind::Unverified) ++unknown;
        else ++linked;
        hash.number(r.objectId); hash.number(r.rawContext); hash.number(r.numberingRecordId);
    }
    if (model.objectNumberingRecords.size()!=records || model.objectNumberingReferences.size()!=references.size())
        throw std::runtime_error("numbering records or references dropped");
    std::cout<<"version="<<model.storageVersion<<" records="<<records<<" derived="<<derived<<" references="<<references.size()
             <<" linked="<<linked<<" unverified="<<unknown<<" null="<<nulls<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
}

void validateObjectNumberEvidence(const std::filesystem::path& path)
{
    using namespace tekla::db1;
    std::ifstream f(path/"numberinghistory.txt"); if (!f) throw std::runtime_error("numbering log missing");
    struct Evidence { std::string kind,series,prefix; std::uint32_t number; };
    std::map<std::string,Evidence> evidence;
    const std::regex pattern(R"(^(Part|Assembly)\s+guid:\s+([\w-]+)\s+series:(.*?)\s+.*? -> (.*)/(\d+)\s*$)");
    std::string line; std::smatch match;
    while (std::getline(f,line)) if (std::regex_match(line,match,pattern))
        evidence[match[2].str()]={match[1].str(),match[3].str(),match[4].str(),static_cast<std::uint32_t>(std::stoul(match[5].str()))};
    tekla::Project project; tekla::ProjectOptions options; options.readRawCompanions=false;
    options.readDrawings=false; options.readEnvironment=false; options.readOptions=false;
    std::string error; if (!tekla::readProject(path,project,error,options)) throw std::runtime_error(error);
    std::size_t parts=0,assemblies=0;
    for (const auto& identity:project.model.identities)
    {
        const auto found=evidence.find(identity.second.guid); if (found==evidence.end()) continue;
        const auto& e=found->second;
        const auto& reference=project.model.objectNumberingReferences.at(identity.first);
        const auto& record=project.model.objectNumberingRecords.at(reference.numberingRecordId);
        const auto kind=e.kind=="Part"?ObjectNumberingKind::Part:ObjectNumberingKind::Assembly;
        if (record.kind!=kind || !record.positionNumber || *record.positionNumber!=e.number || record.prefix!=e.prefix ||
            record.prefix+"/"+std::to_string(record.startNumber)!=e.series)
            throw std::runtime_error("numbering assignment disagrees with independent Tekla history");
        if (kind==ObjectNumberingKind::Part) ++parts; else ++assemblies;
    }
    if (evidence.size()!=230 || parts!=187 || assemblies!=43) throw std::runtime_error("numbering log evidence coverage changed");
    if (project.objectNumberingSeriesAssociations.size()!=249)
    {
        std::string details;
        for (const auto& d:project.diagnostics) if (d.find("object numbering")!=std::string::npos) details+="; "+d;
        throw std::runtime_error("DB1 to DB2 object series evidence changed: " + std::to_string(project.objectNumberingSeriesAssociations.size())+details);
    }
    for (const auto& link:project.objectNumberingSeriesAssociations)
    {
        if (link.database!=project.model.databasePath) throw std::runtime_error("numbering database namespace lost");
        if (link.pairingEvidence!=tekla::NumberingPairEvidence::DatabaseGuid) throw std::runtime_error("modern numbering evidence changed");
        const auto& record=project.model.objectNumberingRecords.at(link.numberingRecordId);
        const auto& series=project.numbering.at(link.numberingDatabase).series.at(link.seriesIndex);
        if (project.model.objectNumberingReferences.at(link.objectId).numberingRecordId!=record.id ||
            series.prefix!=record.prefix || series.startNumber!=record.startNumber)
            throw std::runtime_error("DB1 to DB2 exact series association changed");
    }
    for (const auto& identity:project.model.identities)
        if (evidence.count(identity.second.guid) && std::none_of(project.objectNumberingSeriesAssociations.begin(),project.objectNumberingSeriesAssociations.end(),[&](const auto& a){return a.objectId==identity.first && a.database==project.model.databasePath;}))
            throw std::runtime_error("log-verified object lost DB2 series association");
    std::size_t special=0;
    for (const auto& entry:project.model.objectNumberingReferences)
        if (entry.second.numberingRecordId)
        {
            const auto& record=project.model.objectNumberingRecords.at(entry.second.numberingRecordId);
            if (record.startNumber==0x80000001U && record.sequence==0 && !record.positionNumber) ++special;
        }
    if (special!=20) throw std::runtime_error("special unassigned-range evidence changed");
    std::cout<<"log_parts="<<parts<<" log_assemblies="<<assemblies<<" project_series_links="<<project.objectNumberingSeriesAssociations.size()<<'\n';
}
