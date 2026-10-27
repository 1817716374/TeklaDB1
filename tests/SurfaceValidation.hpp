// Included after Fingerprint inside validate.cpp's anonymous namespace.
void validateSurfaces(const tekla::db1::Model& model, const tekla::db1::RawDatabase& raw, bool library)
{
    const auto word=[](const std::vector<std::uint8_t>& p,std::size_t offset) {
        std::uint32_t n=0; std::memcpy(&n,p.data()+offset,4); return n;
    };
    const auto& definitions=raw.tables.at(library?118:146).records;
    const auto& records=raw.tables.at(library?117:145).records;
    if (model.surfaceTreatmentDefinitions.size()!=definitions.size() || model.surfaceTreatments.size()!=records.size())
        throw std::runtime_error("surface records lost");
    Fingerprint hash;
    for (const auto& r:definitions)
    {
        if (r.payload.size()!=292) throw std::runtime_error("surface definition evidence width");
        const auto& d=model.surfaceTreatmentDefinitions.at(word(r.payload,0));
        hash.number(d.id); hash.text(d.classNumber); hash.text(d.name); hash.text(d.profile); hash.text(d.material); hash.text(d.typeName); hash.number(d.typeCode);
        if (d.typeCode!=word(r.payload,268)) throw std::runtime_error("surface type code lost");
        hash.number(d.thicknessFromProfile.has_value()); if (d.thicknessFromProfile) hash.real(*d.thicknessFromProfile);
        for (std::size_t i=0;i<16;++i)
        {
            if (d.rawHeader[i]!=word(r.payload,4+i*4)) throw std::runtime_error("surface definition header lost");
            hash.number(d.rawHeader[i]);
        }
        for (std::size_t i=0;i<5;++i)
        {
            if (d.rawTail[i]!=word(r.payload,272+i*4)) throw std::runtime_error("surface definition tail lost");
            hash.number(d.rawTail[i]);
        }
    }
    std::map<std::uint32_t,std::uint32_t> fathers;
    for (const auto& r:raw.tables.at(library?161:191).records)
        if (word(r.payload,4)==73 && !fathers.emplace(word(r.payload,12),word(r.payload,8)).second)
            throw std::runtime_error("duplicate surface father evidence");
    std::size_t points=0,distances=0,formulas=0;
    for (const auto& r:records)
    {
        if (r.payload.size()!=78) throw std::runtime_error("surface instance evidence width");
        const auto& s=model.surfaceTreatments.at(word(r.payload,0));
        if (s.fatherPartId!=fathers.at(s.id) || s.definitionId!=word(r.payload,4) || s.startPointId!=word(r.payload,8) ||
            s.endPointId!=word(r.payload,12) || s.contourId!=word(r.payload,16) || s.orientationId!=word(r.payload,20) ||
            s.ownerId!=model.identities.at(s.id).ownerId || model.parts.count(s.id))
            throw std::runtime_error("surface reference/classification mismatch");
        model.parts.at(s.fatherPartId); model.surfaceTreatmentDefinitions.at(s.definitionId);
        const auto& reverse=model.surfaceTreatmentIdsByFather.at(s.fatherPartId);
        if (std::count(reverse.begin(),reverse.end(),s.id)!=1) throw std::runtime_error("surface father reverse index mismatch");
        hash.number(s.id); hash.number(s.fatherPartId); hash.number(s.ownerId); hash.number(s.definitionId); hash.text(s.guid);
        hash.number(s.startPointId); hash.number(s.endPointId); hash.number(s.contourId); hash.number(s.orientationId); hash.real(s.storedLength);
        for (const auto* v:{&s.start,&s.end,&s.origin,&s.axis,&s.secondary,&s.normal}) for (double x:*v) hash.real(x);
        for (const auto& point:s.contour)
        {
            for (double x:point.value) hash.real(x);
            hash.number(point.chamferType);
            for (float f:{point.chamferX,point.chamferY,point.chamferDz1,point.chamferDz2})
            { std::uint32_t bits=0; std::memcpy(&bits,&f,4); hash.number(bits); }
            ++points;
        }
        for (std::size_t i=0;i<22;++i)
        {
            if (s.rawTail[i]!=r.payload[56+i]) throw std::runtime_error("surface instance tail lost");
            hash.byte(s.rawTail[i]);
        }
        for (auto id:s.distanceParameterIds)
        {
            const auto& bound=model.distanceParameters.at(id).boundObjectIds;
            if (std::find(bound.begin(),bound.end(),s.id)==bound.end()) throw std::runtime_error("surface distance link lost");
            hash.number(id); ++distances;
        }
        for (auto id:s.formulaBindingIds)
        {
            if (model.formulaBindings.at(id).targetObjectId!=s.id) throw std::runtime_error("surface formula target lost");
            hash.number(id); ++formulas;
        }
    }
    if (fathers.size()!=model.surfaceTreatments.size()) throw std::runtime_error("surface father edge lost");
    std::cout<<"version="<<model.storageVersion<<" definitions="<<definitions.size()<<" surfaces="<<records.size()
             <<" fathers="<<fathers.size()<<" points="<<points<<" distances="<<distances<<" formulas="<<formulas
             <<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
}

void validateSurfaceEvidence(const std::filesystem::path& path)
{
    tekla::db1::Model model; tekla::db1::MaterialCatalog materials; std::string error;
    if (!tekla::db1::parseComponentLibrary(path/"xslib.db1",model,error) ||
        !tekla::db1::parseMaterialCatalog(path/"matdb.bin",materials,error)) throw std::runtime_error(error);
    std::size_t materialMatches=0,points=0,distances=0,formulas=0;
    for (const auto& entry:model.surfaceTreatmentDefinitions)
    {
        const auto& d=entry.second;
        // Official CODE and SurfaceTypeEnum documentation corroborate this pair.
        if (d.typeCode!=3 || d.typeName!="TS1 - Tile surface 1" || !d.thicknessFromProfile)
            throw std::runtime_error("surface type/thickness evidence mismatch");
        if (std::none_of(materials.materials.begin(),materials.materials.end(),[&](const auto& m){return m.name==d.material;}))
            throw std::runtime_error("surface material not found in paired catalog: "+d.material);
        ++materialMatches;
    }
    for (const auto& entry:model.surfaceTreatments)
    {
        const auto& s=entry.second;
        if (model.parts.at(s.fatherPartId).internalType!=2) throw std::runtime_error("surface father evidence changed");
        points+=s.contour.size(); distances+=s.distanceParameterIds.size(); formulas+=s.formulaBindingIds.size();
    }
    const auto& step=model.surfaceTreatments.at(20871);
    const auto& stepDefinition=model.surfaceTreatmentDefinitions.at(step.definitionId);
    if (step.fatherPartId!=20859 || stepDefinition.name!="STEP" || *stepDefinition.thicknessFromProfile!=1.587 ||
        std::abs(step.origin[2]+19999.2065)>1e-8 || model.formulaBindings.at(21385).propertyName!="proPOSITION_AT_DEPTH" ||
        model.formulaBindings.at(21385).expression!="2" || model.formulaBindings.at(21385).targetObjectId!=step.id)
        throw std::runtime_error("surface STEP geometry/formula evidence mismatch");
    if (materialMatches!=6 || model.surfaceTreatments.size()!=7 || points!=32 || distances!=132 || formulas!=34)
        throw std::runtime_error("surface evidence counts changed");
    tekla::Project project; tekla::ProjectOptions options;
    options.readRawCompanions=false; options.readNumbering=false; options.readDrawings=false;
    options.readEnvironment=false; options.readOptions=false;
    if (!tekla::readProject(path,project,error,options)) throw std::runtime_error(error);
    if (!project.materials || !project.componentLibrary || project.surfaceMaterialAssociations.size()!=7)
        throw std::runtime_error("project surface material associations lost");
    for (const auto& link:project.surfaceMaterialAssociations)
    {
        if (link.database!=project.componentLibrary->databasePath) throw std::runtime_error("surface material DB1 scope lost");
        const auto& surface=project.componentLibrary->surfaceTreatments.at(link.surfaceTreatmentId);
        if (project.materials->materials.at(link.materialIndex).name!=project.componentLibrary->surfaceTreatmentDefinitions.at(surface.definitionId).material)
            throw std::runtime_error("project surface material index mismatch");
    }
    std::cout<<"definitions=6 surfaces=7 material_matches="<<materialMatches<<" points="<<points
             <<" distance_links="<<distances<<" formula_targets="<<formulas<<" project_material_links="<<project.surfaceMaterialAssociations.size()<<'\n';
}
