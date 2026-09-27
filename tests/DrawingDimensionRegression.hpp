void drawingDimensionRegression(std::string mode,const std::filesystem::path& path)
{
    const bool older730=mode.find("dimensions730_")==0;
    if(older730)mode.replace(0,14,"dimensions_");
    DgFixture f; f.addView();
    Bytes set(144); put<unsigned>(set,0,257); put<unsigned>(set,4,66); put<unsigned>(set,8,45);
    Bytes row(1496); put<unsigned>(row,0,256); put<unsigned>(row,4,67); put<unsigned>(row,8,45);
    put<unsigned>(row,12,66); put<unsigned>(row,16,older730?0:1);
    for (unsigned i=0;i<3;++i) { put<double>(row,24+8*i,std::array<double,3>{1,2,3}[i]); put<double>(row,48+8*i,std::array<double,3>{4,6,3}[i]); }
    put<double>(row,176,1); put<double>(row,192,-2);
    f.rows[2]={row}; f.rows[3]={set};
    const auto encode=[&](DgFixture value) {
        if(!older730)return value.encode();
        Dg730Fixture old;
        old.rows[2]=value.rows[2];old.rows[3]=value.rows[3];
        for(auto& r:old.rows[2])r.resize(1400);
        for(auto& r:old.rows[3])r.resize(136);
        if(mode=="dimensions_ambiguous")
        {
            old.rows[5].push_back(old.rows[5][0]);put<unsigned>(old.rows[5][1],4,46);
        }
        if(mode=="dimensions_collapsed")
            for(auto offset:{40U,160U})for(auto point:{24U,48U})
                std::memcpy(old.rows[5][0].data()+offset+point,old.rows[5][0].data()+offset,24);
        return old.encode();
    };
    tekla::Drawing drawing; std::string error;
    if (mode=="dimensions_invalid")
    {
        for (unsigned mutation=0;mutation<13;++mutation)
        {
            auto bad=f;
            if(mutation==0)put<unsigned>(bad.rows[2][0],4,0);
            if(mutation==1)bad.rows[2].push_back(row);
            if(mutation==2)bad.rows[3].push_back(set);
            if(mutation==3)put<unsigned>(bad.rows[3][0],4,0);
            if(mutation==4)put<unsigned>(bad.rows[2][0],0,999);
            if(mutation==5)put<unsigned>(bad.rows[3][0],0,999);
            if(mutation==6)put<unsigned>(bad.rows[2][0],12,999);
            if(mutation==7)put<unsigned>(bad.rows[2][0],8,999);
            if(mutation==8)put<unsigned>(bad.rows[3][0],8,999);
            if(mutation>=9 && mutation<=11)put<double>(bad.rows[2][0],std::array<unsigned,3>{24,168,192}[mutation-9],std::numeric_limits<double>::infinity());
            if(mutation==12) { put<double>(bad.rows[2][0],24,-std::numeric_limits<double>::max()); put<double>(bad.rows[2][0],48,std::numeric_limits<double>::max()); }
            save(path,encode(f)); check(tekla::parseDrawing(path,drawing,error),error);
            save(path,encode(bad)); check(!tekla::parseDrawing(path,drawing,error),"bad straight dimension accepted");
            check(!error.empty() && drawing.raw.tables.empty() && drawing.straightDimensions.empty() && drawing.straightDimensionSets.empty(),"failed dimension output not cleared");
        }
        return;
    }
    if(mode=="dimensions_angled")
    {
        put<double>(f.rows[2][0],24,0);put<double>(f.rows[2][0],32,0);
        put<double>(f.rows[2][0],48,10);put<double>(f.rows[2][0],56,0);
        put<double>(f.rows[2][0],168,0.6);put<double>(f.rows[2][0],176,0.8);
    }
    if(mode=="dimensions_deferred")put<double>(f.rows[2][0],176,2);
    if(mode=="dimensions_nonplanar")put<double>(f.rows[2][0],64,4);
    if(mode=="dimensions_unknown")
    {
        put<unsigned>(f.rows[2][0],16,older730?1:77);
        put<double>(f.rows[2][0],24,std::numeric_limits<double>::quiet_NaN());
    }
    if(mode=="dimensions_legacy")
    {
        Dg782Fixture old; old.rows[2]=f.rows[2]; old.rows[3]=f.rows[3];
        old.rows[2][0].resize(1400);old.rows[3][0].resize(136);
        save(path,old.encode());check(tekla::parseDrawing(path,drawing,error),error);
        check(drawing.straightDimensions.empty() && drawing.straightDimensionSets.empty() && !drawing.raw.tables.empty(),"legacy dimensions guessed");return;
    }
    save(path,encode(f));error="stale";check(tekla::parseDrawing(path,drawing,error),error);
    check(error.empty() && drawing.straightDimensionSets.at(66).dimensionIds==std::vector<std::uint32_t>{67},"dimension ownership");
    if(mode=="dimensions_unknown")
    {
        check(drawing.straightDimensions.empty() && drawing.unhandledDimensionRecordIds==std::vector<std::uint32_t>{67},"unknown subtype guessed");return;
    }
    const auto& d=drawing.straightDimensions.at(67);
    check(d.subtypeCode==(older730?0U:1U),"dimension subtype rewritten");
    if(mode=="dimensions_ambiguous" || mode=="dimensions_collapsed")
        check(drawing.viewsByContext.empty() && !drawing.diagnostics.empty(),"legacy dimension invented a unique view");
    check(d.contextId==45 && d.dimensionSetId==66 && d.distance==-2,"dimension fields or signed offset");
    if(mode=="dimensions_deferred" || mode=="dimensions_nonplanar")
        check(!d.projectedLength && !drawing.diagnostics.empty(),"unverified projection guessed");
    else check(d.projectedLength && std::abs(*d.projectedLength-(mode=="dimensions_angled"?8.0:3.0))<1e-12,"projected dimension confused with endpoint distance");
    if(mode=="dimensions_deferred")check(d.upDirection[1]==2,"stored direction normalized silently");
}
