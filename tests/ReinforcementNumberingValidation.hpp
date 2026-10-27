void validateReinforcementNumberingEvidence(const std::filesystem::path& path)
{
    using namespace tekla::db1;Model m;std::string error;
    if(!parseComponentLibrary(path,m,error))throw std::runtime_error(error);
    std::map<std::pair<uint32_t,std::string>,std::vector<std::string>> parameters;
    for(const auto& entry:m.parameterDefinitions)
        parameters[{entry.second.ownerId,entry.second.name}].push_back(entry.second.expression);
    std::set<std::pair<uint32_t,std::string>> checked;
    for(const auto& entry:m.formulaBindings)
    {
        const auto& f=entry.second;
        if(f.propertyName!="proSERIE" && f.propertyName!="proSTARTNUMBER")continue;
        if(!m.reinforcements.count(f.targetObjectId))continue;
        const auto literal=parameters.find({f.ownerId,f.expression});if(literal==parameters.end())continue;
        const auto& ref=m.objectNumberingReferences.at(f.targetObjectId);
        const auto& n=m.objectNumberingRecords.at(ref.numberingRecordId);
        if(literal->second.size()!=1)throw std::runtime_error("ambiguous referenced reinforcement parameter evidence");
        std::string expected=literal->second.front();
        if(f.propertyName=="proSERIE" && expected.size()>=2 && expected.front()=='"' && expected.back()=='"')expected=expected.substr(1,expected.size()-2);
        const auto actual=f.propertyName=="proSERIE"?n.prefix:std::to_string(n.startNumber);
        if(n.kind!=ObjectNumberingKind::Reinforcement || actual!=expected || n.positionNumber || n.sequence || n.inlineObjectId || n.storedNumber)
            throw std::runtime_error("reinforcement numbering parameter disagrees with stored series");
        if(!checked.emplace(f.targetObjectId,f.propertyName).second)throw std::runtime_error("duplicate reinforcement numbering evidence");
    }
    std::set<std::pair<uint32_t,std::string>> expected;
    for(auto id:{1127972U,1136014U,1136281U})for(const auto* name:{"proSERIE","proSTARTNUMBER"})expected.emplace(id,name);
    if(checked!=expected)throw std::runtime_error("reinforcement numbering evidence coverage changed");
    std::cout<<"reinforcement_objects=3 prefix_parameters=3 start_parameters=3 assigned_positions=unverified\n";
}
