// Included after Fingerprint inside validate.cpp's anonymous namespace.
void validateOwnership(const tekla::db1::Model& model, const tekla::db1::RawDatabase& raw, bool library)
{
    const bool old=model.storageVersion=="7.82", intermediate=model.storageVersion=="8.95";
    const auto word=[](const std::vector<std::uint8_t>& p,std::size_t at) {
        if (at+4>p.size()) throw std::runtime_error("ownership evidence record too short");
        return std::uint32_t(p[at]) | std::uint32_t(p[at+1])<<8 | std::uint32_t(p[at+2])<<16 | std::uint32_t(p[at+3])<<24;
    };
    Fingerprint hash;
    const auto identityTable=old?(library?179:209):intermediate?(library?260:293):(library?318:355);
    for (const auto& r : raw.tables.at(identityTable).records)
    {
        const auto id=word(r.payload,0), owner=word(r.payload,old?16:intermediate?8:4);
        const auto& identity=model.identities.at(id);
        if (identity.ownerId!=owner) throw std::runtime_error("identity owner differs from version-specific evidence");
        hash.number(id); hash.number(owner); hash.text(identity.guid);
        if (intermediate)
        {
            if (identity.classReferenceId!=word(r.payload,4)) throw std::runtime_error("identity class reference lost");
            hash.number(identity.classReferenceId); hash.number(model.identityClasses.at(identity.classReferenceId).recordKind);
        }
    }
    if (intermediate)
        for (const auto& r : raw.tables.at(library?279:315).records)
        {
            const auto& c=model.identityClasses.at(word(r.payload,0));
            if (c.recordKind!=word(r.payload,4)) throw std::runtime_error("identity class kind changed");
            for (std::size_t i=0;i<c.rawFields.size();++i)
                if (c.rawFields[i]!=word(r.payload,8+i*4)) throw std::runtime_error("identity class metadata lost");
        }
    for (const auto& r : raw.tables.at(old?(library?163:193):(library?242:274)).records)
    {
        const auto id=word(r.payload,0); const auto& p=model.parts.at(id);
        if (p.ownerId!=model.identities.at(id).ownerId || p.auxiliaryReferenceId!=(old?0:word(r.payload,8)))
            throw std::runtime_error("part ownership and auxiliary reference conflated");
        hash.number(id); hash.number(p.ownerId); hash.number(p.auxiliaryReferenceId);
    }
    std::size_t ownedParameters=0,ownedDistances=0,ownedFormulas=0,children=0;
    const auto owned=[&](const auto& obj,const auto& childIds) {
        for (auto id : childIds)
        {
            if (id==obj.id || model.identities.at(id).ownerId!=obj.id) throw std::runtime_error("component child owner mismatch");
            hash.number(id); ++children;
        }
        for (auto id : obj.parameterIds)
        {
            const auto& p=model.parameterDefinitions.at(id);
            if (p.ownerId!=obj.id) throw std::runtime_error("parameter owner mismatch");
            hash.number(id); hash.text(p.name); hash.text(p.label); hash.text(p.expression); ++ownedParameters;
        }
        for (auto id : obj.distanceParameterIds)
        {
            if (model.distanceParameters.at(id).ownerId!=obj.id) throw std::runtime_error("distance owner mismatch");
            hash.number(id); ++ownedDistances;
        }
        for (auto id : obj.formulaBindingIds)
        {
            if (model.formulaBindings.at(id).ownerId!=obj.id) throw std::runtime_error("formula owner mismatch");
            hash.number(id); ++ownedFormulas;
        }
    };
    for (const auto& c : model.components) { hash.number(c.id); hash.text(c.name); owned(c,c.childIds); }
    for (const auto& c : model.customComponentDefinitions) { hash.number(c.id); hash.text(c.name); owned(c,c.childObjectIds); }
    std::size_t customReferences=0;
    std::map<std::uint32_t,std::vector<std::uint32_t>> expectedReferences;
    for (const auto& r : raw.tables.at(library?162:192).records)
        if (word(r.payload,4)==4) expectedReferences[word(r.payload,8)].push_back(word(r.payload,12));
    if (!model.controlLines.empty() || expectedReferences.size()!=model.customComponentReferences.size())
        throw std::runtime_error("custom references misclassified as control objects");
    for (auto& entry : expectedReferences)
    {
        std::sort(entry.second.begin(),entry.second.end());
        if (model.customComponentReferences.at(entry.first)!=entry.second) throw std::runtime_error("custom reference targets changed");
        hash.number(entry.first);
        for (auto target : entry.second) { model.identities.at(target); hash.number(target); ++customReferences; }
    }
    for (const auto& c : model.customComponentDefinitions)
    {
        const auto found=expectedReferences.find(c.id);
        if (found!=expectedReferences.end() && c.referenceObjectIds!=found->second) throw std::runtime_error("custom definition reference index lost");
    }
    std::size_t bound=0,references=0;
    if (library)
    {
        if (model.distanceParameters.size()!=raw.tables.at(147).records.size() || model.formulaBindings.size()!=raw.tables.at(156).records.size())
            throw std::runtime_error("component variable rows omitted");
        for (const auto& r : raw.tables.at(147).records)
        {
            const auto& d=model.distanceParameters.at(word(r.payload,0));
            hash.number(d.id); hash.number(d.ownerId); hash.text(d.name); hash.text(d.label); hash.text(d.guid);
            hash.real(d.storedDistance); hash.real(d.secondaryStoredValue); hash.text(d.propertyToken); hash.text(d.planeToken);
            for (auto v : d.rawFields) hash.number(v);
            for (auto v : d.boundObjectIds) { model.identities.at(v); hash.number(v); ++bound; }
            for (auto v : d.formulaBindingIds)
                if (model.formulaBindings.at(v).targetObjectId!=d.id) throw std::runtime_error("distance target index changed");
        }
        for (const auto& r : raw.tables.at(156).records)
        {
            const auto& f=model.formulaBindings.at(word(r.payload,0));
            if (f.targetObjectId!=word(r.payload,4) || f.storedIndex!=word(r.payload,8)) throw std::runtime_error("formula target/index lost");
            hash.number(f.id); hash.number(f.ownerId); hash.number(f.targetObjectId); hash.number(f.storedIndex);
            hash.text(f.guid); hash.text(f.propertyName); hash.text(f.expression);
            for (auto v : f.referencedObjectIds) { model.identities.at(v); hash.number(v); ++references; }
            for (auto v : f.inputObjectIds) hash.number(v);
            const auto& index=model.formulaBindingIdsByTarget.at(f.targetObjectId);
            if (std::count(index.begin(),index.end(),f.id)!=1) throw std::runtime_error("formula index lost");
        }
        std::size_t parameters=0,distances=0,formulas=0;
        std::vector<std::uint32_t> owners;
        for (const auto& entry : model.variablesByOwner) owners.push_back(entry.first);
        std::sort(owners.begin(),owners.end());
        for (auto id : owners)
        {
            if (id) model.identities.at(id);
            hash.number(id); const auto& group=model.variablesByOwner.at(id);
            for (auto v : group.parameterIds)
            {
                const auto& p=model.parameterDefinitions.at(v);
                if (p.ownerId!=id) throw std::runtime_error("generic parameter scope mismatch");
                hash.number(v); hash.text(p.name); hash.text(p.label); hash.text(p.expression); ++parameters;
            }
            for (auto v : group.distanceParameterIds)
            {
                if (model.distanceParameters.at(v).ownerId!=id) throw std::runtime_error("generic distance scope mismatch");
                hash.number(v); ++distances;
            }
            for (auto v : group.formulaBindingIds)
            {
                if (model.formulaBindings.at(v).ownerId!=id) throw std::runtime_error("generic formula scope mismatch");
                hash.number(v); ++formulas;
            }
        }
        if (parameters!=model.parameterDefinitions.size() || distances!=model.distanceParameters.size() || formulas!=model.formulaBindings.size())
            throw std::runtime_error("generic ownership lists omit variables");
    }
    std::cout<<"version="<<model.storageVersion<<" identities="<<model.identities.size()<<" classes="<<model.identityClasses.size()
             <<" parts="<<model.parts.size()<<" children="<<children<<" owned_parameters="<<ownedParameters
             <<" distances="<<model.distanceParameters.size()<<" formulas="<<model.formulaBindings.size()
             <<" bound="<<bound<<" references="<<references<<" custom_references="<<customReferences<<" scopes="<<model.variablesByOwner.size()
             <<" other_parameters="<<(model.parameterDefinitions.size()-ownedParameters)
             <<" other_formulas="<<(model.formulaBindings.size()-ownedFormulas)<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
}
