// Included after Fingerprint inside validate.cpp's anonymous namespace.
void validatePositions(const tekla::db1::Model& model, const tekla::db1::RawDatabase& raw, bool library)
{
    Fingerprint hash;
    const auto word=[](const std::vector<std::uint8_t>& p,std::size_t offset) {
        std::uint32_t n=0; std::memcpy(&n,p.data()+offset,4); return n;
    };
    const auto real=[](const std::vector<std::uint8_t>& p,std::size_t offset) {
        float n=0; std::memcpy(&n,p.data()+offset,4); return n;
    };
    const bool legacy=model.storageVersion=="7.82";
    const std::size_t shift=legacy?16:0;
    const auto& positions=legacy?model.partDefinitionPositions:model.partPositions;
    if (legacy ? !model.partPositions.empty() : !model.partDefinitionPositions.empty())
        throw std::runtime_error("position ID namespaces mixed");
    const auto& records=raw.tables.at(legacy?(library?177:207):(library?215:245)).records;
    if (records.size()!=positions.size()) throw std::runtime_error("part position records dropped");
    for (const auto& record:records)
    {
        const auto& p=record.payload;
        if (p.size()!=(legacy?380:52)) throw std::runtime_error("unexpected position evidence width");
        const auto& position=positions.at(word(p,0));
        if (position.id!=word(p,0) || position.startAxialOffset!=real(p,4+shift) || position.endAxialOffset!=real(p,16+shift) ||
            position.depthCode!=word(p,28+shift) || position.depthOffset!=real(p,32+shift) ||
            position.planeCode!=word(p,44+shift) || position.planeOffset!=real(p,48+shift))
            throw std::runtime_error("part position field mapping changed");
        hash.number(position.id); hash.real(position.startAxialOffset); hash.real(position.endAxialOffset);
        hash.number(position.depthCode); hash.real(position.depthOffset); hash.number(position.planeCode); hash.real(position.planeOffset);
        const std::size_t offsets[]={8,12,20,24,36,40};
        for (std::size_t i=0;i<6;++i)
        {
            if (position.rawFields[i]!=word(p,offsets[i]+shift)) throw std::runtime_error("opaque position bytes changed");
            hash.number(position.rawFields[i]);
        }
    }
    std::size_t linked=0, checked=0, nonzero=0, residuals=0;
    std::vector<std::uint32_t> axialResidualIds, lengthResidualIds;
    for (const auto& part:model.parts)
    {
        const auto& p=part.second;
        if (legacy)
        {
            if (p.auxiliaryReferenceId) throw std::runtime_error("fabricated legacy position reference");
            const auto& position=positions.at(p.definitionId); ++linked;
            if (!p.geometryReferenceId)
            {
                double projected=0, squared=0;
                for (std::size_t i=0;i<3;++i)
                {
                    projected+=(p.origin[i]-p.start[i])*p.axis[i];
                    squared+=(p.end[i]-p.start[i])*(p.end[i]-p.start[i]);
                }
                // Geometry is double, but the setting is float (e.g. -304.8
                // is stored as -304.79998779296875). Allow one float rounding
                // interval plus double-coordinate subtraction noise.
                const double rounding=std::numeric_limits<float>::epsilon()*std::max(1.0,std::abs(double(position.startAxialOffset)))+1e-6;
                if (std::abs(projected-position.startAxialOffset)>rounding)
                    axialResidualIds.push_back(p.id);
                ++checked;
                if (position.startAxialOffset || position.endAxialOffset) ++nonzero;
                if (std::abs(p.length-(std::sqrt(squared)+position.endAxialOffset-position.startAxialOffset))>0.005)
                { ++residuals; lengthResidualIds.push_back(p.id); }
            }
        }
        else if (p.auxiliaryReferenceId) { positions.at(p.auxiliaryReferenceId); ++linked; }
    }
    std::cout<<"version="<<model.storageVersion<<" positions="<<records.size()<<" linked_parts="<<linked
             <<" fingerprint="<<std::hex<<hash.value<<std::dec;
    if (legacy)
    {
        const auto ids=[](std::vector<std::uint32_t>& values) {
            std::sort(values.begin(),values.end());
            if (values.empty()) std::cout<<'-';
            for (std::size_t i=0;i<values.size();++i) std::cout<<(i?",":"")<<values[i];
        };
        std::cout<<" axial_checked="<<checked<<" nonzero="<<nonzero<<" length_residuals="<<residuals;
        std::cout<<" axial_residual_ids="; ids(axialResidualIds);
        std::cout<<" length_residual_ids="; ids(lengthResidualIds);
    }
    std::cout<<'\n';
}

void validatePositionSourceEvidence(const std::filesystem::path& path)
{
    const auto text=[](const std::filesystem::path& file) {
        std::ifstream f(file,std::ios::binary);
        if (!f) throw std::runtime_error("position source evidence missing");
        return std::string((std::istreambuf_iterator<char>(f)),std::istreambuf_iterator<char>());
    };
    const auto source=text(path/"Model.cs"), caller=text(path/"AUTRATekla.cs");
    const auto begin=source.find("public static TSM.Beam CreateBeam(");
    if (begin==std::string::npos) throw std::runtime_error("beam creation evidence missing");
    const auto beamSource=source.substr(begin,source.find("beam.Insert();",begin)-begin);
    if (beamSource.find("Plane = TSM.Position.PlaneEnum.MIDDLE")==std::string::npos ||
        beamSource.find("Depth = depth")==std::string::npos ||
        caller.find("project.CreateMainBeams(TSM.Position.DepthEnum.BEHIND)")==std::string::npos ||
        caller.find("project.CreateSecondaryBeams(TSM.Position.DepthEnum.BEHIND)")==std::string::npos)
        throw std::runtime_error("beam position source changed");
    tekla::db1::Model model; std::string error;
    const auto modelPath=path.parent_path()/"MohamedHasan94__AUTRA"/"AUTRA"/"wwwroot"/"Outputs"/"Tekla"/"ITIFinal02_35"/"ITIFinal02_35.db1";
    if (!tekla::db1::parseModelFile(modelPath,model,error)) throw std::runtime_error(error);
    std::size_t beams=0,centered=0;
    for (const auto& entry:model.parts)
    {
        const auto& p=entry.second;
        const bool beam=p.name=="Main Beam" || p.name=="Secondary Beam";
        const bool center=p.name=="Column" || p.name=="RC Footing" || p.name=="PC Footing";
        if (!beam && !center) continue;
        const auto& position=model.partPositions.at(p.auxiliaryReferenceId);
        if (position.depthCode!=(beam?2U:0U) || position.planeCode!=0 || position.depthOffset!=0 || position.planeOffset!=0)
            throw std::runtime_error("position differs from paired construction source");
        if (beam) ++beams; else ++centered;
    }
    if (beams!=30 || centered!=36) throw std::runtime_error("position source model population changed");
    // Stored geometry consistency, distinct from independent source evidence.
    const auto& base=model.parts.at(21371); const auto& position=model.partPositions.at(base.auxiliaryReferenceId);
    if (position.startAxialOffset!=-25 || position.endAxialOffset!=25 || base.length!=350 || base.origin[0]!=20175)
        throw std::runtime_error("axial position lost or applied twice");
    const auto& plate=model.parts.at(41223); const auto& platePosition=model.partPositions.at(plate.auxiliaryReferenceId);
    if (platePosition.depthCode!=2 || platePosition.planeCode!=1 || std::abs(platePosition.planeOffset-4.3f)>1e-6f ||
        std::abs(plate.origin[1]-4009.3000001905)>1e-7)
        throw std::runtime_error("plane position lost or applied twice");
    std::cout<<"source_beams="<<beams<<" source_centered="<<centered<<" stored_geometry_checks=2\n";
}
