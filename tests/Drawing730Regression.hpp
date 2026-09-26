struct Dg730Fixture : Dg782Fixture
{
    Dg730Fixture()
    {
        widths[1]=584; widths[13]=704; widths[17]=16; widths[24]=240;
        Bytes property(110); put<unsigned>(property,0,3); put<unsigned>(property,4,2);
        text(property,8,"gr_cl_draw_prop"); text(property,29,"standard"); rows[22][0]=property;
    }
    Bytes encode()
    {
        const std::string header="Xsteel  7.30"; Bytes bytes(header.begin(),header.end());
        word(bytes,33); word(bytes,4);
        for(std::size_t i=34;i-- >0;)if(i!=6){bytes.push_back(4);word(bytes,types[i]);}
        for(std::size_t i=0;i<34;++i)if(i!=6)
        {
            word(bytes,static_cast<unsigned>(rows[i].size()));word(bytes,widths[i]);
            for(const auto& row:rows[i])
            {
                check(row.size()==widths[i],"7.30 fixture width");
                bytes.push_back(4);bytes.insert(bytes.end(),row.begin(),row.end());
            }
        }
        return bytes;
    }
};
void drawing730Regression(const std::string& mode,const std::filesystem::path& path)
{
    Dg730Fixture fixture;tekla::Drawing drawing;tekla::db1::RawDatabase raw;std::string error="stale";
    if(mode=="drawing730_signature")++fixture.widths[0]; // empty table still must match
    if(mode=="drawing730_cycle")put<unsigned>(fixture.rows[18][1],4,1);
    if(mode=="drawing730_missing")put<unsigned>(fixture.rows[18][1],4,999);
    if(mode=="drawing730_property")put<unsigned>(fixture.rows[21][0],4,999);
    if(mode=="drawing730_nonfinite")put<double>(fixture.rows[5][0],40,std::numeric_limits<double>::quiet_NaN());
    if(mode=="drawing730_volume")put<double>(fixture.rows[5][0],112,30);
    if(mode=="drawing730_duplicate")fixture.rows[5].push_back(fixture.rows[5][0]);
    if(mode=="drawing730_context")
    {
        fixture.rows[5].push_back(fixture.rows[5][0]);put<unsigned>(fixture.rows[5][1],4,46);
    }
    if(mode=="drawing730_collapsed")
        for(auto start:{40U,160U})for(auto point:{24U,48U})std::memcpy(fixture.rows[5][0].data()+start+point,fixture.rows[5][0].data()+start,24);
    if(mode=="drawing730_subject_unknown")put<unsigned>(fixture.rows[10][0],12,2);
    if(mode=="drawing730_general") { put<unsigned>(fixture.rows[10][0],12,3);put<unsigned>(fixture.rows[10][0],16,0); }
    const auto bytes=fixture.encode();save(path,bytes);
    if(mode=="drawing730_truncated")
    {
        for(auto at:{std::size_t(0),std::size_t(12),std::size_t(19),std::size_t(24),bytes.size()/2,bytes.size()-1})
        {
            save(path,Bytes(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(at)));
            check(!tekla::parseDrawing(path,drawing,error),"truncated 7.30 drawing accepted");
            check(drawing.raw.tables.empty() && drawing.strings.empty() && !error.empty(),"truncated output not cleared");
        }
        auto extra=bytes;extra.push_back(0);save(path,extra);
        check(!tekla::parseDrawing(path,drawing,error),"trailing drawing data accepted");return;
    }
    if(mode=="drawing730_limit")
    {
        tekla::db1::RawDatabaseOptions options;options.maxDecodedBytes=bytes.size()-1;
        check(!tekla::parseDrawing(path,drawing,error,options),"drawing decode budget ignored");return;
    }
    const bool valid=mode=="drawing730_valid" || mode=="drawing730_context" || mode=="drawing730_collapsed" || mode=="drawing730_subject_unknown" || mode=="drawing730_general";
    if(!valid)
    {
        check(!tekla::parseDrawing(path,drawing,error),"invalid 7.30 drawing accepted");
        check(drawing.raw.tables.empty() && drawing.strings.empty() && drawing.viewsByRecordId.empty() && !error.empty(),"failed drawing output not cleared");return;
    }
    check(tekla::parseDrawing(path,drawing,error),error);check(error.empty(),"stale drawing error");
    check(drawing.raw.storageVersion=="7.30" && drawing.raw.tables.size()==34,"drawing signature");
    check(drawing.projectGuid.empty() && drawing.modelReferences.empty(),"fabricated legacy GUID/model reference");
    check(std::any_of(drawing.diagnostics.begin(),drawing.diagnostics.end(),[](const auto& s){return s.find("7.30 drawing model references remain raw")!=std::string::npos;}),"unmapped reference diagnostic missing");
    check(drawing.strings.at(1).text=="<Mark><UserText>HELLO WORLD</UserText></Mark>" && drawing.strings.at(1).complete,"drawing string chain");
    check(drawing.sheets.size()==1 && drawing.sheets[0].width==594 && drawing.sheets[0].height==420,"drawing sheet");
    check(drawing.subject && drawing.subject->modelObjectId==(mode=="drawing730_general"?0U:1234U) && drawing.subject->modelGuid.empty(),"numeric subject lost");
    if(mode=="drawing730_subject_unknown")check(drawing.subject->kind==tekla::DrawingSubjectKind::Unknown,"unverified assembly kind guessed");
    else check(drawing.subject->kind==(mode=="drawing730_general"?tekla::DrawingSubjectKind::GeneralArrangement:tekla::DrawingSubjectKind::SinglePart),"subject classification");
    if(mode=="drawing730_context")check(drawing.viewsByRecordId.size()==2 && drawing.viewsByContext.empty(),"ambiguous views collapsed");
    else if(mode=="drawing730_collapsed")check(drawing.viewsByRecordId.empty() && drawing.unhandledViewRecordIds==std::vector<std::uint32_t>{44},"collapsed view lost");
    else
    {
        const auto& view=drawing.viewsByContext.at(45);
        check(view.viewCoordinates.origin==tekla::DrawingPoint3{{100,200,300}} && view.viewCoordinates.axisX==tekla::DrawingPoint3{{1,0,0}},"view coordinates");
        check(view.displayCoordinates.axisX==tekla::DrawingPoint3{{0,1,0}} && view.restriction.minX==-10 && view.restriction.maxX==20 && view.storedAttributeVolume.depthPositive==7,"display/range blocks");
        check(view.modelGuid.empty() && view.propertySetName=="saved-view","view context/property join");
    }
    check(tekla::db1::parseRawDatabase(path,raw,error,{true}),error);check(raw.decompressedFileImage==bytes,"drawing raw bytes lost");
}
