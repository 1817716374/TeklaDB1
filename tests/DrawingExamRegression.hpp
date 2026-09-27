struct DgExamFixture : Dg844Fixture
{
    bool sectioned;
    explicit DgExamFixture(bool section):sectioned(section)
    {
        widths[2]=1480;fields[2]=205;widths[4]=656;fields[4]=101;
        widths[5]=4676;fields[5]=646;widths[6]=580;fields[6]=16;references[6]={0};
        widths[7]=568;fields[7]=84;widths[10]=6132;fields[10]=804;
        for(std::size_t i=0;i<rows.size();++i)for(auto& r:rows[i])r.resize(widths[i]);
        auto& p=rows[22][0];std::fill(p.begin()+29,p.end(),0);text(p,29,"01234567-89ab-cdef-0123-456789abcdef");
        put<unsigned>(rows[10][0],16,1234);
    }
    Bytes encode()
    {
        const std::string header=sectioned?"Xsteel# 9.08":"Xsteel  8.95";
        Bytes b(header.begin(),header.end());
        if(sectioned){b.resize(76);put<unsigned>(b,12,1);put<unsigned>(b,16,0xdbcec0bc);}
        const auto append=[&](unsigned width,unsigned fields,const std::vector<unsigned>& refs,const std::vector<Bytes>& records){
            if(sectioned){word(b,0xdbcec066);word(b,width);word(b,fields);for(unsigned f=0;f<fields;++f)word(b,std::find(refs.begin(),refs.end(),f)!=refs.end()?1:0);}
            else {word(b,static_cast<unsigned>(records.size()));word(b,width);}
            for(const auto& r:records)
            {
                check(r.size()==width,"exam fixture row width");b.push_back(4);b.insert(b.end(),r.begin(),r.end());
                if(sectioned){const auto at=b.size();b.resize(at+24+16*refs.size(),0xa5);put<unsigned>(b,at,0xdbcec066);}
            }
            if(sectioned)b.push_back(0);
        };
        std::vector<Bytes> directory;
        for(std::size_t i=types.size();i-- >0;)if(sectioned || i!=6){Bytes r(4);put(r,0,types[i]);directory.push_back(r);}
        append(4,2,{0},directory);
        for(std::size_t i=0;i<types.size();++i)if(sectioned || i!=6)append(widths[i],fields[i],references[i],rows[i]);
        return b;
    }
};

void drawingExamRegression(const std::string& mode,const std::filesystem::path& path)
{
    const bool sectioned=mode.find("drawing908_")==0;const auto name=mode.substr(11);
    DgExamFixture f(sectioned);std::string error;tekla::Drawing drawing;tekla::db1::RawDatabase raw;
    if(name=="signature")++f.widths[0];
    if(name=="reference")f.references[5].push_back(8);
    if(name=="missing")put<unsigned>(f.rows[18][1],4,999);
    if(name=="cycle")put<unsigned>(f.rows[18][1],4,1);
    if(name=="sheet_nan")put<double>(f.rows[8][0],16,std::numeric_limits<double>::quiet_NaN());
    if(name=="subject_duplicate")f.rows[10].push_back(f.rows[10][0]);
    if(name=="subject_zero")put<unsigned>(f.rows[10][0],4,0);
    if(name=="subject_type")put<unsigned>(f.rows[10][0],0,99);
    if(name=="reference_duplicate")f.rows[44].push_back(f.rows[44][0]);
    if(name=="guid")f.rows[22][0][29]='x';
    if(name=="property")put<unsigned>(f.rows[21][0],4,999);
    auto bytes=f.encode();if(name=="version")bytes[11]='7';
    if(name=="truncated")
    {
        for(auto n:{std::size_t(0),std::size_t(11),std::size_t(13),bytes.size()/2,bytes.size()-1})
        {save(path,Bytes(bytes.begin(),bytes.begin()+n));check(!tekla::parseDrawing(path,drawing,error) && drawing.raw.tables.empty() && !error.empty(),"truncated exam DG accepted");}
        return;
    }
    save(path,bytes);const bool ok=tekla::parseDrawing(path,drawing,error,{true});
    if(name!="valid"){check(!ok && !error.empty() && drawing.raw.tables.empty(),"invalid exam drawing accepted");return;}
    check(ok,error);check(drawing.raw.decompressedFileImage==bytes,"exam DG raw image lost");
    check(drawing.subject && drawing.subject->modelObjectId==1234 && drawing.subject->modelGuid.empty(),"numeric subject became GUID");
    check(drawing.modelReferences.size()==1 && drawing.modelReferences[0].modelObjectId==1234 && drawing.modelReferences[0].modelGuid.empty(),"numeric model reference became GUID");
    check(drawing.viewsByRecordId.empty() && drawing.straightDimensions.empty() && drawing.unhandledViewRecordIds.size()==1 && drawing.unhandledDimensionRecordIds.size()==1,"unverified geometry fabricated");
    check(drawing.strings.at(1).text=="<Mark><UserText>HELLO WORLD</UserText></Mark>" && drawing.sheets[0].width==594,"exam DG semantic fields");
}

void drawingHeaderBoundary(const std::filesystem::path& path)
{
    for(unsigned count:{46,48,49,50,51,52,53,54,55,56,57})
    {
        const std::string header="Xsteel  8.95";Bytes b(header.begin(),header.end());word(b,count);word(b,4);
        for(unsigned i=0;i<count;++i){b.push_back(4);word(b,1000+i);}
        for(unsigned i=0;i<count;++i){word(b,0);word(b,4);}
        save(path,b);tekla::db1::RawDatabase raw;std::string error;
        check(tekla::db1::parseRawDatabase(path,raw,error,{true}),error);
        check(raw.storageVersion=="8.95" && raw.preamble.size()==12 && raw.tables.size()==count+1 && raw.decompressedFileImage==b,"binary directory count consumed as version");
    }
}
