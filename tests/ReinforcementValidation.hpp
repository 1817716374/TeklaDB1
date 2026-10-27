void validateReinforcement(const tekla::db1::Model& m,const tekla::db1::RawDatabase& raw,bool library)
{
    using namespace tekla::db1;
    const auto word=[](const std::vector<uint8_t>& p,std::size_t at){uint32_t n;std::memcpy(&n,p.data()+at,4);return n;};
    std::map<uint32_t,const std::vector<uint8_t>*> doubles,ints;
    for(const auto& r:raw.tables.at(library?175:205).records)doubles.emplace(word(r.payload,0),&r.payload);
    for(const auto& r:raw.tables.at(library?176:206).records)ints.emplace(word(r.payload,0),&r.payload);
    const auto array=[&](uint32_t key,const auto& source,auto type){
        using T=decltype(type);std::vector<T> values;std::size_t seen=0;
        while(key){if(++seen>source.size())throw std::runtime_error("reinforcement source cycle");const auto& p=*source.at(key);
            const auto count=word(p,8);if(count>(sizeof(T)==8?12U:10U))throw std::runtime_error("reinforcement source count");
            for(uint32_t i=0;i<count;++i){T v;std::memcpy(&v,p.data()+(sizeof(T)==8?24:20)+i*sizeof(T),sizeof(T));values.push_back(v);}key=word(p,4);}
        return values;
    };
    Fingerprint h;std::size_t coordinates=0,radii=0,spacings=0,formulas=0,distances=0;
    for(const auto& source:raw.tables.at(library?153:183).records)
    {
        const auto& p=source.payload;const auto& d=m.reinforcementDefinitions.at(word(p,0));
        if(d.rawPayload!=p || d.classNumber!=word(p,8) || d.modeArrayId!=word(p,4) || d.hookArrayId!=word(p,24) ||
           d.modeValues!=array(d.modeArrayId,ints,int32_t{}) || d.hookValues!=array(d.hookArrayId,doubles,double{}))throw std::runtime_error("reinforcement definition data lost");
        h.number(d.id);h.number(d.classNumber);h.text(d.name);h.text(d.grade);h.text(d.size);
        for(auto v:d.modeValues)h.number(static_cast<uint32_t>(v));for(auto v:d.hookValues)h.real(v);for(auto v:d.rawPayload)h.byte(v);
    }
    std::size_t fatherLinks=0;
    for(const auto& source:raw.tables.at(library?161:191).records)
    {
        const auto& p=source.payload;if(word(p,4)!=47)continue;
        const auto& r=m.reinforcements.at(word(p,12));if(r.fatherPartId!=word(p,8) || !m.parts.count(r.fatherPartId))throw std::runtime_error("reinforcement father direction changed");++fatherLinks;
    }
    for(const auto& source:raw.tables.at(library?154:184).records)
    {
        const auto& p=source.payload;const auto& r=m.reinforcements.at(word(p,0));
        const auto& recordIdentity=m.identities.at(r.id);
        const auto recordKind=m.storageVersion=="8.95"?m.identityClasses.at(recordIdentity.classReferenceId).recordKind:recordIdentity.type;
        if(r.rawPayload!=p || r.definitionId!=word(p,4) || recordKind!=47 || !m.reinforcementDefinitions.count(r.definitionId))throw std::runtime_error("reinforcement instance data lost");
        const auto shape=array(word(p,16),doubles,double{});
        std::vector<double> flat;for(const auto& v:r.storedShapeCoordinates)flat.insert(flat.end(),v.begin(),v.end());
        if(flat!=shape || r.radiusValues!=array(word(p,20),doubles,double{}) || r.spacingValues!=array(word(p,24),doubles,double{}) || r.storedDistributionValues!=array(word(p,28),doubles,double{}))throw std::runtime_error("reinforcement chained values changed");
        const auto& reverse=m.reinforcementIdsByFather.at(r.fatherPartId);
        if(std::count(reverse.begin(),reverse.end(),r.id)!=1)throw std::runtime_error("reinforcement reverse parent missing");
        const auto& identity=m.identities.at(r.id);
        if(r.ownerId!=identity.ownerId || r.contextId!=identity.contextId || r.guid!=identity.guid || !m.frames.count(r.orientationId))throw std::runtime_error("reinforcement identity scope changed");
        h.number(r.id);h.number(r.definitionId);h.number(r.fatherPartId);h.number(r.ownerId);h.number(r.contextId);h.text(r.guid);
        h.number(r.orientationId);for(auto v:r.origin)h.real(v);h.real(r.storedLength);
        for(std::size_t i=0;i<4;++i){if(r.arrayIds[i]!=word(p,16+i*4))throw std::runtime_error("reinforcement array id changed");h.number(r.arrayIds[i]);}
        for(auto v:flat)h.real(v);for(auto v:r.radiusValues)h.real(v);for(auto v:r.spacingValues)h.real(v);for(auto v:r.storedDistributionValues)h.real(v);for(auto v:p)h.byte(v);
        for(auto id:r.formulaBindingIds){if(m.formulaBindings.at(id).targetObjectId!=r.id)throw std::runtime_error("reinforcement formula target changed");h.number(id);}
        for(auto id:r.distanceParameterIds){const auto& bound=m.distanceParameters.at(id).boundObjectIds;if(std::find(bound.begin(),bound.end(),r.id)==bound.end())throw std::runtime_error("reinforcement distance target changed");h.number(id);}
        coordinates+=r.storedShapeCoordinates.size();radii+=r.radiusValues.size();spacings+=r.spacingValues.size();formulas+=r.formulaBindingIds.size();distances+=r.distanceParameterIds.size();
    }
    if(m.reinforcementDefinitions.size()!=raw.tables.at(library?153:183).records.size() || m.reinforcements.size()!=raw.tables.at(library?154:184).records.size() || fatherLinks!=m.reinforcements.size())throw std::runtime_error("reinforcement records omitted");
    std::cout<<"version="<<m.storageVersion<<" definitions="<<m.reinforcementDefinitions.size()<<" instances="<<m.reinforcements.size()<<" coordinates="<<coordinates<<" radii="<<radii<<" spacing_values="<<spacings<<" formulas="<<formulas<<" distances="<<distances<<" fingerprint="<<std::hex<<h.value<<std::dec<<'\n';
}

void validateReinforcementEvidence(const std::filesystem::path& path)
{
    using namespace tekla::db1;Model m;std::string error;if(!parseComponentLibrary(path,m,error))throw std::runtime_error(error);
    std::map<std::pair<uint32_t,std::string>,std::string> parameters;
    for(const auto& p:m.parameterDefinitions)parameters.emplace(std::make_pair(p.second.ownerId,p.second.name),p.second.expression);
    std::map<std::string,std::size_t> counts;
    for(const auto& entry:m.formulaBindings)
    {
        const auto& f=entry.second;const auto found=m.reinforcements.find(f.targetObjectId);if(found==m.reinforcements.end())continue;
        const auto literal=parameters.find({f.ownerId,f.expression});if(literal==parameters.end())continue;
        const auto& r=found->second;const auto& d=m.reinforcementDefinitions.at(r.definitionId);const auto& value=literal->second;
        std::string expected;bool checked=true;
        if(f.propertyName=="proNAME")expected=d.name;
        else if(f.propertyName=="proSIZE")expected=d.size;
        else if(f.propertyName=="proMATERIAL")expected=d.grade;
        else if(f.propertyName=="proCLASS")expected=std::to_string(d.classNumber);
        else checked=false;
        if(checked){if(expected!=value)throw std::runtime_error("reinforcement named parameter evidence mismatch");++counts[f.propertyName];}
        else if(f.propertyName=="proBENDING_RADIUS" || f.propertyName=="proNUMBER_OF_BARS" || f.propertyName=="proTARGET_SPACING")
        {
            std::size_t end=0;const auto number=std::stod(value,&end);if(end!=value.size())throw std::runtime_error("nonliteral evidence parameter");
            const auto& actual=f.propertyName=="proBENDING_RADIUS"?r.radiusValues:r.spacingValues;
            if(actual!=std::vector<double>{number})throw std::runtime_error("reinforcement numeric parameter evidence mismatch");++counts[f.propertyName];
        }
    }
    const std::map<std::string,std::size_t> expected{{"proNAME",16},{"proSIZE",16},{"proMATERIAL",16},{"proCLASS",16},{"proBENDING_RADIUS",16},{"proNUMBER_OF_BARS",13},{"proTARGET_SPACING",3}};
    if(counts!=expected)throw std::runtime_error("reinforcement evidence coverage changed");
    std::cout<<"named_fields=64 bending_radii=16 bar_count_parameters=13 target_spacing_parameters=3\n";
}
