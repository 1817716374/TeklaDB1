struct Dg844Fixture : Dg782Fixture
{
    std::array<std::vector<unsigned>,47> references;
    Dg844Fixture()
    {
        widths[0]=128; fields[0]=20; references[0]={0};
        widths[1]=596; fields[1]=63; references[1]={0,61,62};
        widths[2]=1400; fields[2]=185; references[2]={0,65};
        widths[3]=136; fields[3]=22; references[3]={0,6};
        widths[4]=624; fields[4]=97; references[4]={0,5};
        widths[5]=4568; fields[5]=632; references[5]={0,7,117,326,432};
        widths[7]=536; fields[7]=76; references[7]={0,59};
        widths[8]=52; fields[8]=11; references[8]={0};
        widths[9]=32; fields[9]=6; references[9]={0};
        widths[10]=5968; fields[10]=793; references[10]={0,5};
        widths[11]=948; fields[11]=162; references[11]={0,32};
        widths[12]=232; fields[12]=37; references[12]={0,24};
        widths[13]=600; fields[13]=92; references[13]={0,4};
        widths[14]=296; fields[14]=48; references[14]={0};
        widths[15]=496; fields[15]=21; references[15]={0};
        widths[16]=144; fields[16]=21; references[16]={0};
        widths[17]=20; fields[17]=6; references[17]={0,4};
        widths[18]=45; fields[18]=6; references[18]={0};
        widths[19]=12; fields[19]=4; references[19]={0};
        widths[20]=37; fields[20]=5; references[20]={0};
        widths[21]=12; fields[21]=4; references[21]={0};
        widths[22]=110; fields[22]=5; references[22]={0};
        widths[23]=3438; fields[23]=50; references[23]={0};
        widths[24]=288; fields[24]=47; references[24]={0,31};
        widths[25]=860; fields[25]=143; references[25]={0,20};
        widths[26]=24; fields[26]=7; references[26]={0};
        widths[27]=64; fields[27]=12; references[27]={0};
        widths[28]=16; fields[28]=5; references[28]={0,4};
        widths[29]=36; fields[29]=10; references[29]={0,4,6,8};
        widths[30]=40; fields[30]=9; references[30]={0,4};
        widths[31]=20; fields[31]=6; references[31]={0,4};
        widths[32]=40; fields[32]=9; references[32]={0,4,6};
        widths[33]=120; fields[33]=21; references[33]={0};
        widths[34]=12; fields[34]=4; references[34]={0,2};
        widths[35]=64; fields[35]=10; references[35]={0};
        widths[36]=32; fields[36]=7; references[36]={0,3};
        widths[37]=56; fields[37]=9; references[37]={0};
        widths[38]=156; fields[38]=24; references[38]={0,4};
        widths[39]=24; fields[39]=7; references[39]={0};
        widths[40]=60; fields[40]=16; references[40]={0,6,7,8,9,10,11,12,13,14,15};
        widths[41]=112; fields[41]=17; references[41]={0};
        widths[42]=20; fields[42]=6; references[42]={0};
        widths[43]=60; fields[43]=16; references[43]={0};
        widths[44]=112; fields[44]=20; references[44]={0,3,8};
        widths[45]=144; fields[45]=23; references[45]={0,3};
        widths[46]=60; fields[46]=11; references[46]={0};
        rows[5][0].resize(widths[5]); rows[10][0].resize(widths[10]);
        auto& p=rows[22][0]; std::fill(p.begin()+29,p.end(),0); text(p,29,"ID01234567-89ab-cdef-0123-456789abcdef");
        Bytes dimension(1400); put<unsigned>(dimension,0,256);put<unsigned>(dimension,4,100); rows[2]={dimension};
    }
    Bytes encode()
    {
        Bytes b(76);text(b,0,"Xsteel# 8.44");put<unsigned>(b,12,1);put<unsigned>(b,16,0xdbcec0bc);
        const auto table=[&](unsigned width,unsigned count,const std::vector<unsigned>& refs,const std::vector<Bytes>& records) {
            word(b,0xdbcec066);word(b,width);word(b,count);
            for(unsigned f=0;f<count;++f)word(b,std::find(refs.begin(),refs.end(),f)!=refs.end()?1:0);
            for(const auto& row:records)
            {
                check(row.size()==width,"8.44 fixture width");b.push_back(4);b.insert(b.end(),row.begin(),row.end());
                const auto start=b.size();b.resize(start+24+16*refs.size(),0xa5);
                put<unsigned>(b,start,0xdbcec066); // magic inside opaque tail
            }
            b.push_back(0);
        };
        std::vector<Bytes> directory;
        for(std::size_t i=types.size();i-- >0;)if(i!=6){Bytes v(4);put(v,0,types[i]);directory.push_back(v);}
        table(4,2,{0},directory);
        for(std::size_t i=0;i<types.size();++i)if(i!=6)table(widths[i],fields[i],references[i],rows[i]);
        return b;
    }
};
void drawing844Regression(const std::string& mode,const std::filesystem::path& path)
{
    Dg844Fixture f;tekla::Drawing drawing;tekla::db1::RawDatabase raw;std::string error="stale";
    if(mode=="drawing844_signature")++f.widths[0];
    if(mode=="drawing844_reference")f.references[5].push_back(8);
    if(mode=="drawing844_missing")put<unsigned>(f.rows[18][1],4,999);
    if(mode=="drawing844_cycle")put<unsigned>(f.rows[18][1],4,1);
    if(mode=="drawing844_guid")f.rows[22][0][29]='X';
    if(mode=="drawing844_conflict") { auto p=f.rows[22][0];put<unsigned>(p,0,99);p[31]='f';f.rows[22].push_back(p); }
    if(mode=="drawing844_property")put<unsigned>(f.rows[21][0],4,999);
    if(mode=="drawing844_nonfinite")put<double>(f.rows[8][0],16,std::numeric_limits<double>::infinity());
    auto bytes=f.encode();
    if(mode=="drawing844_header")bytes[11]='5';
    if(mode=="drawing844_descriptor")put<unsigned>(bytes,88,2);
    if(mode=="drawing844_zero_descriptor")put<unsigned>(bytes,88,0);
    save(path,bytes);
    if(mode=="drawing844_truncated")
    {
        // Includes cuts inside the variable tail and the final terminator.
        for(auto at:{std::size_t(0),std::size_t(75),std::size_t(80),std::size_t(100),bytes.size()/2,bytes.size()-1})
        {
            save(path,Bytes(bytes.begin(),bytes.begin()+static_cast<std::ptrdiff_t>(at)));
            check(!tekla::db1::parseRawDatabase(path,raw,error),"truncated 8.44 raw accepted");
            check(raw.tables.empty() && !error.empty(),"failed raw result not cleared");
        }
        bytes.push_back(0xff);save(path,bytes);check(!tekla::parseDrawing(path,drawing,error),"trailing 8.44 bytes accepted");return;
    }
    if(mode=="drawing844_limit")
    {
        tekla::db1::RawDatabaseOptions options;options.maxDecodedBytes=bytes.size()-1;
        check(!tekla::parseDrawing(path,drawing,error,options),"8.44 decode limit ignored");return;
    }
    if(mode=="drawing844_header" || mode=="drawing844_descriptor" || mode=="drawing844_zero_descriptor")
        check(!tekla::db1::parseRawDatabase(path,raw,error),"unknown 8.44 framing accepted");
    const bool valid=mode=="drawing844_valid";
    if(!valid)
    {
        check(!tekla::parseDrawing(path,drawing,error),"invalid 8.44 drawing accepted");
        check(drawing.raw.tables.empty() && drawing.strings.empty() && !error.empty(),"failed drawing not cleared");return;
    }
    tekla::db1::RawDatabaseOptions options;options.retainDecompressedFileImage=true;
    check(tekla::parseDrawing(path,drawing,error,options),error);
    check(error.empty() && drawing.raw.storageVersion=="8.44" && drawing.raw.tables.size()==47,"8.44 signature");
    check(drawing.strings.at(1).text=="<Mark><UserText>HELLO WORLD</UserText></Mark>","8.44 chain lost");
    check(drawing.projectGuid=="01234567-89ab-cdef-0123-456789abcdef" && drawing.properties.at(3).stringValue=="ID01234567-89ab-cdef-0123-456789abcdef","normalized/raw GUID lost");
    check(drawing.sheets.size()==1 && drawing.sheets[0].width==594 && drawing.sheets[0].height==420,"8.44 sheet");
    check(drawing.modelReferences.empty() && !drawing.subject && drawing.viewsByContext.empty() && drawing.straightDimensions.empty(),"unverified 8.44 geometry decoded");
    check(drawing.unhandledViewRecordIds==std::vector<std::uint32_t>{44} && drawing.unhandledDimensionRecordIds==std::vector<std::uint32_t>{100} && !drawing.diagnostics.empty(),"deferred records not reported");
    check(tekla::db1::parseRawDatabase(path,raw,error,options),error);
    Bytes rebuilt=raw.preamble;
    for(const auto& t:raw.tables)
    {
        word(rebuilt,0xdbcec066);word(rebuilt,t.payloadSize);word(rebuilt,static_cast<unsigned>(t.fieldDescriptors.size()));
        for(auto v:t.fieldDescriptors)word(rebuilt,v);
        for(const auto& row:t.records){rebuilt.push_back(row.allocationTag);rebuilt.insert(rebuilt.end(),row.payload.begin(),row.payload.end());rebuilt.insert(rebuilt.end(),row.allocatorMetadata.begin(),row.allocatorMetadata.end());}
        rebuilt.insert(rebuilt.end(),t.trailer.begin(),t.trailer.end());
    }
    check(rebuilt==bytes && raw.decompressedFileImage==bytes && drawing.raw.decompressedFileImage==bytes,"8.44 byte preservation failed");
    check(raw.tables[6].records[0].allocatorMetadata.size()==104 && raw.tables[10].records.at(0).allocatorMetadata.size()==56,"8.44 variable tail lost");
}
