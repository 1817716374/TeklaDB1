void boltPositionRegression(const std::filesystem::path& path,const std::string& name)
{
    using namespace tekla::db1;
    const bool library=name=="bolt_position_library",older=library;
    auto s=library?componentLibrary(true):onePart();
    const auto def=library?223U:351U,group=library?274U:310U,place=library?43U:64U,ident=library?260U:355U,contours=library?94U:328U;
    auto d=row(s[def],20);str(d,51,"SCREW");s[def].rows={d};
    auto r=row(s[group],30);put<uint32_t>(r,5,20);put<uint32_t>(r,13,1);put<uint32_t>(r,17,2);s[group].rows={r};
    auto identity=row(s[ident],30);
    if(older){auto cls=row(s[279],110);put<uint32_t>(cls,5,10);s[279].rows.push_back(cls);put<uint32_t>(identity,5,110);put<uint32_t>(identity,9,50);}
    else put<uint32_t>(identity,29,10);
    s[ident].rows.push_back(identity);
    auto placement=row(s[place],30);put<uint32_t>(placement,5,3);s[place].rows.push_back(placement);
    if(name=="bolt_position_missing")put<uint32_t>(s[group].rows[0],21,999);
    if(name=="bolt_position_point")put<uint32_t>(s[group].rows[0],13,999);
    if(name=="bolt_position_definition")put<uint32_t>(s[group].rows[0],5,999);
    if(name=="bolt_position_placement")s[place].rows.clear();
    if(name=="bolt_position_present" || name=="bolt_position_empty_array")
    {
        put<uint32_t>(s[group].rows[0],21,40);auto c=row(s[contours],40);
        put<double>(c,17,12);put<double>(c,97,34);put<double>(c,177,56);
        put<uint32_t>(c,341,0x7fffffff);s[contours].rows={c};
        if(name=="bolt_position_empty_array")put<uint32_t>(s[contours].rows[0],337,0x7fffffff);
    }
    save(path,encode(s,older?"8.95":"9.60"));Model m;std::string error;
    const bool ok=library?parseComponentLibrary(path,m,error):parseModelFile(path,m,error);
    const bool good=name=="bolt_position_null" || library || name=="bolt_position_present" || name=="bolt_position_empty_array";
    if(!good){check(!ok && !error.empty() && m.boltGroups.empty() && m.parts.empty(),"broken non-null bolt dependency accepted");return;}
    check(ok,error.c_str());check(m.boltGroups.size()==1,"empty bolt group discarded");
    const auto& b=m.boltGroups.front();check(b.id==30 && b.definitionId==20 && b.first==m.points.at(1).value && b.second==m.points.at(2).value,"empty bolt references lost");
    if(name=="bolt_position_present")check(b.positionArrayId==40 && b.positions==std::vector<Vec3>{{12,34,56}},"stored bolt position changed");
    else {check(b.positionArrayId==(name=="bolt_position_empty_array"?40U:0U) && b.positions.empty(),"empty positions fabricated or reference lost");check(std::any_of(m.diagnostics.begin(),m.diagnostics.end(),[](const auto& d){return d.find("bolt group has no stored positions")!=std::string::npos;}),"empty bolt diagnostic missing");}
}
