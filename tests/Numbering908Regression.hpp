void numbering908Regression(const std::string& mode,const std::filesystem::path& path)
{
    auto bytes=numbering("/1001");bytes[10]='0';bytes[11]='8';
    if(mode=="numbering908_empty")bytes.resize(49);
    if(mode=="numbering908_width")
    {put<unsigned>(bytes,73,76);bytes.erase(bytes.begin()+154,bytes.begin()+158);}
    if(mode=="numbering908_guid")std::fill(bytes.begin()+13,bytes.begin()+49,' ');
    if(mode=="numbering908_duplicate")
    {
        Bytes duplicate(bytes.begin()+77,bytes.begin()+158);
        bytes.insert(bytes.begin()+158,duplicate.begin(),duplicate.end());put<unsigned>(bytes,69,2);
    }
    if(mode=="numbering908_key")bytes[79]='x';
    if(mode=="numbering908_tail")bytes.back()=1;
    if(mode=="numbering908_unknown_table")
    {
        word(bytes,999);word(bytes,1);word(bytes,4);bytes.push_back(4);word(bytes,123);word(bytes,0x00bc614f);
    }
    tekla::NumberingDatabase result;std::string error;
    if(mode=="numbering908_truncated")
    {
        for(auto size:{0U,12U,48U,50U,66U,79U,159U,161U})
        {
            save(path,Bytes(bytes.begin(),bytes.begin()+size));
            check(!tekla::parseNumberingDatabase(path,result,error),"truncated 9.08 DB2 accepted");
            check(result.series.empty() && result.raw.tables.empty() && !error.empty(),"failed 9.08 DB2 retained state");
        }
        return;
    }
    save(path,bytes);
    const bool valid=mode=="numbering908_valid" || mode=="numbering908_empty" || mode=="numbering908_unknown_table";
    const bool ok=tekla::parseNumberingDatabase(path,result,error,{true});
    check(ok==valid,"9.08 DB2 validation: "+error);
    if(!valid){check(result.series.empty() && result.raw.tables.empty() && !error.empty(),"9.08 DB2 failure state");return;}
    check(result.raw.storageVersion=="9.08" && result.raw.decompressedFileImage==bytes,"9.08 DB2 raw preservation");
    if(mode=="numbering908_empty")check(result.series.empty() && result.raw.tables.empty(),"9.08 empty library");
    else
    {
        check(result.series.size()==1,"9.08 series missing");const auto& s=result.series[0];
        check(s.prefix.empty() && s.startNumber==1001 && s.partCounter==13 && s.assemblyCounter==2 && s.additionalFields.size()==5,"9.08 series fields");
    }
    if(mode=="numbering908_unknown_table")check(result.raw.tables.back().records[0].payload==Bytes({123,0,0,0}) && result.diagnostics.size()>=2,"unknown 9.08 table dropped");
}
