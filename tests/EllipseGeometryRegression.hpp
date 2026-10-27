#pragma once
int ellipseSections()
{
    using namespace tekla::db1;
    Model model; Part p; p.internalType=2; p.length=100; p.axis={1,0,0}; p.secondary={0,1,0}; p.normal={0,0,1};
    p.profile="ELD50*25*50*25"; p.id=1; p.end={100,0,0}; model.parts[1]=p;
    p.id=2; p.profile="ELD25*50*25*50"; model.parts[2]=p;
    p.id=3; p.profile="ELD30*30*30*30"; model.parts[3]=p;
    p.id=4; p.profile="ELD50*25*50*25"; p.origin={100,200,300}; p.axis={0,1,0};p.secondary={0,0,1};p.normal={1,0,0};model.parts[4]=p;
    p=model.parts[1];p.id=5;p.contourIsPath=true;p.end={100,100,0};
    for(const auto& v:{Vec3{0,0,0},Vec3{100,0,0},Vec3{100,100,0}}){ContourPoint c;c.value=v;p.contour.push_back(c);}model.parts[5]=p;
    p.id=6;p.secondary={0,std::sqrt(.5),std::sqrt(.5)};p.normal={0,-std::sqrt(.5),std::sqrt(.5)};model.parts[6]=p;
    std::vector<std::uint32_t> rejected;
    p=model.parts[1];std::uint32_t id=10;
    for(const char* name:{"ELD0*25*0*25","ELD50*0*50*0","ELD50*25*40*25","ELD50*25*50*20","ELD50..0*25*50*25","ELD50*nan*50*nan","ELD-50*25*-50*25"})
    {p.id=id++;p.profile=name;model.parts[p.id]=p;rejected.push_back(p.id);}
    p.id=id++;p.profile="ELD"+std::string(400,'9')+"*25*50*25";model.parts[p.id]=p;rejected.push_back(p.id);
    for(const auto& kv:model.parts)model.actualPartIds.push_back(kv.first);
    OcctGeometryModel result;std::string error;
    if(!buildOcctGeometry(model,result,error)){std::cerr<<error;return 81;}
    std::sort(result.unbuiltPartIds.begin(),result.unbuiltPartIds.end());
    if(result.partShapes.size()!=6 || result.unbuiltPartIds!=rejected)return 82;
    const double pi=std::acos(-1.0);
    for(unsigned i=1;i<=6;++i)
    {
        const auto& shape=result.partShapes.at(i);if(!BRepCheck_Analyzer(shape).IsValid())return 83;
        GProp_GProps props;BRepGProp::VolumeProperties(shape,props,1e-10);
        const double area=pi*(i==3?225:312.5),length=i>=5?200:100;
        if(std::abs(props.Mass()-area*length)>area*length*1e-7){std::cerr<<"ellipse "<<i<<" volume="<<props.Mass();return 84;}
        const auto& part=model.parts.at(i);
        for(double theta:{.123,.789,1.913,3.057,4.81})
        {
            const double a=i==2?12.5:i==3?15:25,b=i==2?25:i==3?15:12.5;
            Vec3 v=part.origin;for(unsigned k=0;k<3;++k)v[k]+=part.axis[k]*50+part.secondary[k]*a*std::cos(theta)+part.normal[k]*b*std::sin(theta);
            BRepExtrema_DistShapeShape dist(BRepBuilderAPI_MakeVertex(gp_Pnt(v[0],v[1],v[2])).Vertex(),shape);
            if(!dist.IsDone() || dist.Value()>1e-6)return 85;
            if(i==6)
            {
                // A 90-degree rotation around Z preserves the initial 45-degree
                // section orientation; fixed-axis projection would erase it.
                const double s=std::sqrt(.5);
                const gp_Pnt after(100-s*a*std::cos(theta)+s*b*std::sin(theta),50,s*a*std::cos(theta)+s*b*std::sin(theta));
                BRepExtrema_DistShapeShape moved(BRepBuilderAPI_MakeVertex(after).Vertex(),shape);
                if(!moved.IsDone() || moved.Value()>1e-6)return 88;
            }
        }
        TriangleMesh mesh;if(!tessellateOcctShape(shape,mesh,error) || mesh.indices.empty())return 86;
    }
    // A catalog entry remains authoritative, including an unsupported entry.
    Model catalog;p=model.parts[1];catalog.parts[1]=p;catalog.actualPartIds={1};Profile q;q.name=p.profile;q.type=999;catalog.profiles[q.name]=q;
    if(!buildOcctGeometry(catalog,result,error)||!result.partShapes.empty()||result.unbuiltPartIds!=std::vector<unsigned>{1})return 87;
    std::cout<<"analytic ellipses, rotated axes, circle limit, miter path, invalid/tapered dimensions and catalog precedence checked\n";
    return 0;
}
