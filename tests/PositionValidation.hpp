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
    const auto& records=raw.tables.at(library?215:245).records;
    if (records.size()!=model.partPositions.size()) throw std::runtime_error("part position records dropped");
    for (const auto& record:records)
    {
        const auto& p=record.payload;
        if (p.size()!=52) throw std::runtime_error("unexpected position evidence width");
        const auto& position=model.partPositions.at(word(p,0));
        if (position.id!=word(p,0) || position.startAxialOffset!=real(p,4) || position.endAxialOffset!=real(p,16) ||
            position.depthCode!=word(p,28) || position.depthOffset!=real(p,32) ||
            position.planeCode!=word(p,44) || position.planeOffset!=real(p,48))
            throw std::runtime_error("part position field mapping changed");
        hash.number(position.id); hash.real(position.startAxialOffset); hash.real(position.endAxialOffset);
        hash.number(position.depthCode); hash.real(position.depthOffset); hash.number(position.planeCode); hash.real(position.planeOffset);
        const std::size_t offsets[]={8,12,20,24,36,40};
        for (std::size_t i=0;i<6;++i)
        {
            if (position.rawFields[i]!=word(p,offsets[i])) throw std::runtime_error("opaque position bytes changed");
            hash.number(position.rawFields[i]);
        }
    }
    std::size_t linked=0;
    for (const auto& part:model.parts)
        if (part.second.auxiliaryReferenceId) { model.partPositions.at(part.second.auxiliaryReferenceId); ++linked; }
    std::cout<<"version="<<model.storageVersion<<" positions="<<records.size()<<" linked_parts="<<linked
             <<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
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
