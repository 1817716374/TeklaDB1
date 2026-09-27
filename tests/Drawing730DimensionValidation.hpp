int drawing730DimensionEvidence(const std::filesystem::path& directory)
{
    // Independently exported DWG LINE coordinates, extracted with LibreDWG
    // 0.14. The corpus pins the original DWG; this is not a DWG reader.
    std::ifstream dwg(directory/"ensemble passerelle.dwg",std::ios::binary);
    char signature[6]{};dwg.read(signature,6);
    if(!dwg || std::string(signature,6)!="AC1015")throw std::runtime_error("independent DWG source missing");
    const auto root=directory.parent_path()/"btscm-dg730";
    tekla::Drawing drawing;tekla::db1::RawDatabase model;std::string error;
    if(!tekla::parseDrawing(root/"D0000516925.dg",drawing,error) ||
       !tekla::db1::parseRawDatabase(root/"pechelirejulV1.db1",model,error))throw std::runtime_error(error);
    if(drawing.raw.storageVersion!="7.30" || drawing.straightDimensions.size()!=5 ||
       drawing.straightDimensionSets.size()!=5 || !drawing.unhandledDimensionRecordIds.empty())
        throw std::runtime_error("independent drawing dimension coverage changed");
    const auto rawTable=[](const tekla::db1::RawDatabase& raw,unsigned ordinal)->const tekla::db1::RawTable& {
        const auto found=std::find_if(raw.tables.begin(),raw.tables.end(),[&](const auto& t){return t.ordinal==ordinal;});
        if(found==raw.tables.end())throw std::runtime_error("evidence table missing");return *found;
    };
    const auto stringAt=[](const std::vector<std::uint8_t>& row,std::size_t offset,std::size_t width) {
        if(offset+width>row.size())throw std::runtime_error("evidence string truncated");
        const auto begin=row.begin()+static_cast<std::ptrdiff_t>(offset);
        return std::string(begin,std::find(begin,begin+static_cast<std::ptrdiff_t>(width),0));
    };
    std::size_t titles=0;
    for(const auto& row:rawTable(model,27).records)
        if(stringAt(row.payload,92,16)=="D0000516925.dg" && stringAt(row.payload,108,80)=="Ensemble passerelle")++titles;
    if(titles!=1)throw std::runtime_error("DWG title does not match the DB1 drawing catalog");
    const auto word=[](const std::vector<std::uint8_t>& row,std::size_t offset) {
        if(offset+4>row.size())throw std::runtime_error("evidence integer truncated");
        return std::uint32_t(row[offset])|(std::uint32_t(row[offset+1])<<8)|
               (std::uint32_t(row[offset+2])<<16)|(std::uint32_t(row[offset+3])<<24);
    };
    const auto real=[](const std::vector<std::uint8_t>& row,std::size_t offset) {
        if(offset+8>row.size())throw std::runtime_error("evidence real truncated");
        double value;std::memcpy(&value,row.data()+offset,8);
        if(!std::isfinite(value))throw std::runtime_error("nonfinite evidence value");return value;
    };
    std::map<unsigned,const std::vector<std::uint8_t>*> views;
    for(const auto& row:rawTable(drawing.raw,260).records)
        if(!views.emplace(word(row.payload,8),&row.payload).second)throw std::runtime_error("ambiguous evidence view");
    struct Line {unsigned dimension,context;double scale;std::array<double,2> start,end;};
    const std::array<Line,5> lines{{
        {284777,11,100,{-90.5797809719143,3.95934921796604},{-30.91908290738988,3.95934921796604}},
        {293979,14,30,{-132.91523814010503,-54.56079527393188},{-132.91523814010503,-17.36807727346694}},
        {293970,14,30,{-120.92163603876264,-15.97688199608597},{-73.79618298737913,-15.97688199608597}},
        {293957,14,30,{-122.18258300399779,-44.40448712198505},{-72.53030174045212,-44.40448712198505}},
        {293808,15,100,{-146.40618012945828,71.68190325941679},{-30.37618243289736,71.68190325941679}}
    }};
    for(const auto& line:lines)
    {
        const auto& d=drawing.straightDimensions.at(line.dimension);
        if(d.contextId!=line.context || d.subtypeCode!=0 || !d.projectedLength)
            throw std::runtime_error("independent dimension identity differs");
        const auto& set=drawing.straightDimensionSets.at(d.dimensionSetId);
        if(set.contextId!=line.context || set.dimensionIds!=std::vector<std::uint32_t>{d.recordId})
            throw std::runtime_error("independent dimension ownership differs");
        const double length=std::hypot(line.end[0]-line.start[0],line.end[1]-line.start[1]);
        if(std::abs(length*line.scale-*d.projectedLength)>1e-7)
            throw std::runtime_error("DG projection differs from independent DWG line length");
        // This fixed export places views relative to view 11. These raw
        // positioning fields are evidence-only, not a general paper transform.
        const auto& view=*views.at(line.context);const auto& base=*views.at(11);
        if(real(view,376)!=line.scale)throw std::runtime_error("evidence scale differs");
        std::array<double,2> start{},end{},delta{};
        double alongUp=0;
        for(unsigned i=0;i<2;++i){delta[i]=(d.endPoint[i]-d.startPoint[i])/line.scale;alongUp+=delta[i]*d.upDirection[i];}
        for(unsigned i=0;i<2;++i)
        {
            start[i]=d.startPoint[i]/line.scale+d.upDirection[i]*d.distance+real(view,344+8*i)-real(base,344+8*i);
            end[i]=start[i]+delta[i]-d.upDirection[i]*alongUp;
        }
        const auto distance=[](const auto& a,const auto& b){return std::hypot(a[0]-b[0],a[1]-b[1]);};
        const auto forward=(std::max)(distance(start,line.start),distance(end,line.end));
        const auto reverse=(std::max)(distance(start,line.end),distance(end,line.start));
        if((std::min)(forward,reverse)>1e-7)throw std::runtime_error("DG anchors/direction/offset differ from independent DWG endpoints");
    }
    std::cout<<"independent_dwg_dimensions=5 endpoints=10 contexts=3 catalog_titles=1\n";
    return 0;
}
