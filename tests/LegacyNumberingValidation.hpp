// The argument is the pinned legacy-dg782 folder; its sibling is the paired model.
void validateLegacyNumberEvidence(const std::filesystem::path& drawings)
{
    using namespace tekla::db1;
    const auto modelDirectory=drawings.parent_path()/"PSDBIM__EXCEL-1246A-BLDG-B";
    tekla::Project project; tekla::ProjectOptions options; options.readComponentLibrary=false;
    options.readRawCompanions=false; options.readEnvironment=false; options.readOptions=false; options.readDrawings=false;
    std::string error;
    if (!tekla::readProject(modelDirectory,project,error,options)) throw std::runtime_error(error);
    if (!project.objectNumberingSeriesAssociations.empty()) throw std::runtime_error("unscoped legacy project auto-associated numbering");
    options.trustLegacyNumberingBasenames=true;
    if (!tekla::readProject(modelDirectory,project,error,options)) throw std::runtime_error(error);
    RawDatabase raw;
    if (!parseRawDatabase(project.model.databasePath,raw,error)) throw std::runtime_error(error);
    const auto word=[](const std::vector<std::uint8_t>& p,std::size_t offset) { std::uint32_t value=0; std::memcpy(&value,p.data()+offset,4); return value; };
    struct Sample { const char* file; std::uint32_t id; bool assembly; const char* prefix; std::uint32_t start,sequence; const char* mark; };
    const Sample samples[]={
        {"DID66AA1927-0000-342A-3137-323234323431.dg",1547355,false,"ps",1,5,"ps5"},
        {"DID66B9AFB8-0000-0B10-3137-323334363234.dg",1547325,false,"ps",1,4,"ps4"},
        {"DID66B9AFB8-0000-00CF-3137-323334343838.dg",1545718,true,"AR",2000,1,"AR2000"},
        {"DID66B9AFB8-0000-0B0C-3137-323334363234.dg",1541434,true,"AR",2000,1,"AR2000"}};
    for (const auto& sample:samples)
    {
        tekla::Drawing drawing;
        if (!tekla::parseDrawing(drawings/sample.file,drawing,error)) throw std::runtime_error(error);
        if (!drawing.subject || drawing.subject->modelObjectId!=sample.id || drawing.subject->kind!=(sample.assembly?tekla::DrawingSubjectKind::Assembly:tekla::DrawingSubjectKind::SinglePart))
            throw std::runtime_error("legacy drawing subject evidence changed");
        const auto& reference=project.model.objectNumberingReferences.at(sample.id);
        const auto& record=project.model.objectNumberingRecords.at(reference.numberingRecordId);
        if (record.kind!=(sample.assembly?ObjectNumberingKind::Assembly:ObjectNumberingKind::Part) || record.prefix!=sample.prefix || record.startNumber!=sample.start || record.sequence!=sample.sequence || !record.positionNumber || record.prefix+std::to_string(*record.positionNumber)!=sample.mark)
            throw std::runtime_error("legacy DB1 numbering differs from drawing mark evidence");
        const std::string element="name=\"PART_POS\" value=\""+std::string(sample.mark)+"\"";
        if (std::none_of(drawing.strings.begin(),drawing.strings.end(),[&](const auto& s){return s.second.complete && s.second.text.find(element)!=std::string::npos;}))
            throw std::runtime_error("independent stored drawing mark missing");
        if (sample.assembly)
        {
            const auto assembly=std::find_if(project.model.assemblies.begin(),project.model.assemblies.end(),[&](const auto& a){return a.id==sample.id;});
            if (assembly==project.model.assemblies.end()) throw std::runtime_error("legacy assembly missing");
            const auto& records=raw.tables.at(181).records;
            const auto stored=std::find_if(records.begin(),records.end(),[&](const auto& r){return word(r.payload,0)==sample.id;});
            if (stored==records.end()) throw std::runtime_error("stored assembly evidence missing");
            const auto primary=word(stored->payload,12);
            if (std::find(assembly->memberIds.begin(),assembly->memberIds.end(),primary)==assembly->memberIds.end()) throw std::runtime_error("assembly primary/member evidence differs");
            const auto& part=project.model.objectNumberingRecords.at(project.model.objectNumberingReferences.at(primary).numberingRecordId);
            if (!part.positionNumber || part.prefix+std::to_string(*part.positionNumber)!="rod1") throw std::runtime_error("assembly numbering conflated with main-part numbering");
        }
        const auto association=std::find_if(project.objectNumberingSeriesAssociations.begin(),project.objectNumberingSeriesAssociations.end(),[&](const auto& a){return a.database==project.model.databasePath && a.objectId==sample.id;});
        if (association==project.objectNumberingSeriesAssociations.end() || association->pairingEvidence!=tekla::NumberingPairEvidence::ExplicitLegacyBasename)
            throw std::runtime_error("explicit legacy numbering pair evidence lost");
        const auto& series=project.numbering.at(association->numberingDatabase).series.at(association->seriesIndex);
        if (series.prefix!=sample.prefix || series.startNumber!=sample.start) throw std::runtime_error("legacy DB2 series evidence changed");
    }
    for (const auto& link:project.objectNumberingSeriesAssociations)
        if (link.database!=project.model.databasePath || link.pairingEvidence!=tekla::NumberingPairEvidence::ExplicitLegacyBasename)
            throw std::runtime_error("legacy pair scope/evidence fabricated");
    std::cout<<"drawing_objects=4 parts=2 assemblies=2 explicit_series_links="<<project.objectNumberingSeriesAssociations.size()<<'\n';
}
