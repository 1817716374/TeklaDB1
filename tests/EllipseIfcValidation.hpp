#pragma once
double ellipseIfcMeshVolume(const ifc730::Entities& all,unsigned id)
{
    const auto& e=all.at(id);
    if(e.type=="IFCPOLYLOOP")
    {
        const auto ids=ifc730::refs(e.args.at(0));ifc730::require(ids.size()>=3,"ellipse face loop");
        const auto o=ifc730::vector(all,ids[0],"IFCCARTESIANPOINT");double volume=0;
        for(std::size_t i=1;i+1<ids.size();++i)
        {
            const auto a=ifc730::vector(all,ids[i],"IFCCARTESIANPOINT"),b=ifc730::vector(all,ids[i+1],"IFCCARTESIANPOINT");
            volume+=(o[0]*(a[1]*b[2]-a[2]*b[1])+o[1]*(a[2]*b[0]-a[0]*b[2])+o[2]*(a[0]*b[1]-a[1]*b[0]))/6;
        }
        return volume;
    }
    if(e.type=="IFCFACEOUTERBOUND"||e.type=="IFCFACEBOUND")
    {ifc730::require(e.args.at(1)==".T."||e.args[1]==".F.","ellipse bound orientation");return ellipseIfcMeshVolume(all,ifc730::entityRef(e.args[0]))*(e.args[1]==".T."?1:-1);}
    ifc730::require(e.type=="IFCPRODUCTDEFINITIONSHAPE"||e.type=="IFCSHAPEREPRESENTATION"||e.type=="IFCFACETEDBREP"||e.type=="IFCCLOSEDSHELL"||e.type=="IFCFACE","ellipse mesh entity");
    const unsigned slot=e.type=="IFCPRODUCTDEFINITIONSHAPE"?2:e.type=="IFCSHAPEREPRESENTATION"?3:0;
    double sum=0;for(auto child:ifc730::refs(e.args.at(slot)))sum+=ellipseIfcMeshVolume(all,child);return sum;
}

// Infer the analytic/polygon area ratio from an independently exported end
// ring. No DB1 dimensions or production section construction are used here.
double ellipseIfcAreaCorrection(const ifc730::Entities& all,unsigned shape)
{
    std::set<unsigned> vertices,active;ifc730::vertices(all,shape,vertices,active);
    ifc730::require(vertices.size()==32,"straight ellipse IFC rings");
    std::vector<tekla::db1::Vec3> ps;double minX=1e100;
    for(auto id:vertices){const auto p=ifc730::vector(all,id,"IFCCARTESIANPOINT");ps.push_back(p);minX=(std::min)(minX,p[0]);}
    std::vector<std::array<double,2>> ring;
    for(const auto& p:ps)if(std::abs(p[0]-minX)<.001)ring.push_back({p[1],p[2]});
    ifc730::require(ring.size()==16,"ellipse end ring point count");
    double ymin=1e100,ymax=-1e100,zmin=1e100,zmax=-1e100;
    for(const auto& p:ring){ymin=(std::min)(ymin,p[0]);ymax=(std::max)(ymax,p[0]);zmin=(std::min)(zmin,p[1]);zmax=(std::max)(zmax,p[1]);}
    const double a=(ymax-ymin)/2,b=(zmax-zmin)/2,cy=(ymax+ymin)/2,cz=(zmax+zmin)/2;
    ifc730::require(std::abs((std::min)(a,b)-12.5)<.001 && std::abs((std::max)(a,b)-25)<.001,"ellipse independently inferred radii");
    for(auto& p:ring){p[0]-=cy;p[1]-=cz;ifc730::require(std::abs(p[0]*p[0]/(a*a)+p[1]*p[1]/(b*b)-1)<1e-5,"IFC points on ellipse");}
    std::sort(ring.begin(),ring.end(),[&](const auto& p,const auto& q){return std::atan2(p[1]/b,p[0]/a)<std::atan2(q[1]/b,q[0]/a);});
    const double pi=std::acos(-1.0);double area=0;
    for(unsigned i=0;i<16;++i)
    {
        const auto& p=ring[i];const auto& q=ring[(i+1)%16];
        double delta=std::atan2(q[1]/b,q[0]/a)-std::atan2(p[1]/b,p[0]/a);if(delta<0)delta+=2*pi;
        ifc730::require(std::abs(delta-2*pi/16)<1e-5,"IFC equal-angle ellipse samples");
        area+=(p[0]*q[1]-p[1]*q[0])/2;
    }
    ifc730::require(area>0,"IFC ellipse polygon area");return pi*a*b/area;
}

int checkEllipseIfc(const std::filesystem::path& directory)
{
    using namespace tekla::db1;
    try
    {
        Model model;std::string error;
        ifc730::require(parseModelFile(directory.parent_path()/"btscm-pechelire"/"pechelirejulV1.db1",model,error),error);
        const auto all=ifc730::read(directory/"export.ifc");
        OcctGeometryModel geometry;ifc730::require(buildOcctGeometry(model,geometry,error),error);
        const std::set<unsigned> straight{347332,347391,350029,350049};
        double correction=0;unsigned rings=0;
        for(const auto& entry:all)
        {
            const auto& e=entry.second;if(e.type!="IFCBEAM")continue;
            const auto tag=ifc730::string(e.args.at(7));if(tag.substr(0,3)!="TS_"||!straight.count(ifc730::number(tag.substr(3))))continue;
            const auto factor=ellipseIfcAreaCorrection(all,ifc730::entityRef(e.args[6]));
            if(correction)ifc730::require(std::abs(factor-correction)<1e-7,"consistent independent ellipse sampling");
            correction=factor;++rings;
        }
        ifc730::require(rings==4,"independent ellipse cross-section coverage");
        std::set<unsigned> checked,controls;std::size_t count=0,controlVertices=0;double worst=0,volumeWorst=0;
        for(const auto& entry:all)
        {
            const auto& e=entry.second;if(e.type!="IFCBEAM" && e.type!="IFCCOLUMN" && e.type!="IFCBUILDINGELEMENTPROXY")continue;
            const auto tag=ifc730::string(e.args.at(7));if(tag.substr(0,3)!="TS_")continue;
            const auto id=ifc730::number(tag.substr(3));auto found=model.parts.find(id);
            const bool control=id==107718;
            if(found==model.parts.end() || (!control && found->second.profile!="ELD50*25*50*25"))continue;
            const auto& part=found->second;
            ifc730::require(!control || part.profile=="2*2300","transport control profile");
            ifc730::require(ifc730::guid(ifc730::string(e.args[0]))==part.guid && (control?controls:checked).insert(id).second,"ellipse/control identity");
            const auto& shape=geometry.partShapes.at(id);ifc730::require(BRepCheck_Analyzer(shape).IsValid(),"invalid ellipse solid");
            std::set<unsigned> vertices,active;ifc730::vertices(all,ifc730::entityRef(e.args[6]),vertices,active);
            const auto frame=ifc730::placement(all,ifc730::entityRef(e.args[5]));double maximum=0;
            for(auto vertex:vertices)
            {
                const auto local=ifc730::vector(all,vertex,"IFCCARTESIANPOINT");Vec3 world{};
                for(unsigned i=0;i<3;++i)world[i]=frame.origin[i]+local[0]*frame.x[i]+local[1]*frame.y[i]+local[2]*frame.z[i];
                BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(gp_Pnt(world[0],world[1],world[2])).Vertex(),shape);
                ifc730::require(distance.IsDone(),"ellipse vertex distance failed");maximum=(std::max)(maximum,distance.Value());++(control?controlVertices:count);
            }
            GProp_GProps props;BRepGProp::VolumeProperties(shape,props,1e-10);
            const auto expected=std::abs(ellipseIfcMeshVolume(all,ifc730::entityRef(e.args[6])))*(control?1:correction);
            ifc730::require(expected>0,"positive ellipse IFC volume");
            const auto residual=std::abs(props.Mass()-expected)/expected;volumeWorst=(std::max)(volumeWorst,residual);
            ifc730::require(residual<=1e-5,"ellipse IFC analytic volume mismatch: "+std::to_string(id));
            worst=(std::max)(worst,maximum);
        }
        ifc730::require(checked==std::set<unsigned>{347332,347391,347535,347550,349854,349861,350029,350049,350068,350089,350107,350129} && count==7872,"ellipse coverage");
        ifc730::require(controls==std::set<unsigned>{107718} && controlVertices==16,"transport control coverage");
        ifc730::require(worst<=.02,"ellipse IFC vertex mismatch: "+std::to_string(worst));
        std::cerr<<"maximumRelativeVolumeError="<<volumeWorst<<" maximumVertexDistanceMm="<<worst<<'\n';
        std::cout<<"ellipse_parts="<<checked.size()<<" ifc_vertices="<<count<<" transport_controls="<<controls.size()<<'\n';return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
