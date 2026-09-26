#include <tekla/db1/OcctGeometry.hpp>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <cmath>
#include <iostream>
#include <algorithm>
#include <set>
#include <limits>
#include "NominalSectionReference.hpp"
#include "EllipseGeometryRegression.hpp" // Includes oblique section transport across a bend.

int nominalSections()
{
    using namespace tekla::db1;
    Model model;Part p;p.internalType=2;p.length=100;p.end={100,0,0};
    p.axis={1,0,0};p.secondary={0,1,0};p.normal={0,0,1};
    std::uint32_t id=100;
    for(const auto& reference:nominalReferences)
    { p.id=id++;p.profile=reference.name;model.parts[p.id]=p;model.actualPartIds.push_back(p.id); }
    std::vector<std::uint32_t> unsupported;
    for(const char* name:{"UPE141","UPE140.5","IPE201","IPE750","IPEA200","IPE200x100","UPE140..0"})
    { p.id=id++;p.profile=name;model.parts[p.id]=p;model.actualPartIds.push_back(p.id);unsupported.push_back(p.id); }
    OcctGeometryModel result;std::string error;
    if(!buildOcctGeometry(model,result,error)){std::cerr<<error;return 10;}
    std::sort(result.unbuiltPartIds.begin(),result.unbuiltPartIds.end());
    if(result.partShapes.size()!=32 || result.unbuiltPartIds!=unsupported || result.nominalProfilePartIds.size()!=32)return 11;
    id=100;
    for(const auto& reference:nominalReferences)
    {
        if(result.nominalProfilePartIds.at(id-100)!=id)return 12;
        GProp_GProps props;BRepGProp::VolumeProperties(result.partShapes.at(id++),props);
        const bool channel=std::string(reference.name).find("UPE")==0;
        const double nominalArea=reference.areaCm2*100;
        const double fillets=(channel?2:4)*reference.radius*reference.radius*(1-std::acos(-1.0)/4);
        // Permit only the published rounding interval plus integration error.
        if(std::abs(props.Mass()/p.length-(nominalArea-fillets))>reference.roundingMm2+.02)
        {std::cerr<<reference.name<<" area="<<props.Mass()/p.length;return 13;}
    }
    if(std::none_of(result.diagnostics.begin(),result.diagnostics.end(),[](const auto& s){return s.find("sharp-corner approximation")!=std::string::npos;}))return 14;
    // A project catalog overrides nominal dimensions, including fillets.
    Model catalogModel;p.id=1;p.profile="UPE140";catalogModel.parts[1]=p;catalogModel.actualPartIds={1};
    Profile section;section.name=p.profile;section.type=4;
    section.parameters={{36,160},{37,70},{38,8},{39,12},{40,12}};catalogModel.profiles[p.profile]=section;
    if(!buildOcctGeometry(catalogModel,result,error) || result.partShapes.size()!=1 || !result.nominalProfilePartIds.empty())return 15;
    GProp_GProps props;BRepGProp::VolumeProperties(result.partShapes.at(1),props);
    const double expected=8*(160-24)+2*70*12+2*12*12*(1-std::acos(-1.0)/4);
    if(std::abs(props.Mass()/100-expected)>1e-6)return 16;
    // A present but uninterpreted catalog entry cannot be silently replaced.
    catalogModel.profiles[p.profile].type=999;
    if(!buildOcctGeometry(catalogModel,result,error) || !result.partShapes.empty() || result.unbuiltPartIds!=std::vector<std::uint32_t>{1} || !result.nominalProfilePartIds.empty())return 17;
    std::cout<<"32 nominal sections checked against published area; 7 unsupported names rejected; catalog precedence and fillets preserved\n";
    return 0;
}

int polybeamMiters()
{
    using namespace tekla::db1;
    for(const double angle:{0.0,std::acos(-1.0)/4,std::acos(-1.0)/2,-std::acos(-1.0)/2})
    {
        Model model;Part p;p.id=1;p.internalType=2;p.length=100;p.profile="10*10";p.contourIsPath=true;
        p.axis={1,0,0};p.secondary={0,1,0};p.normal={0,0,1};p.end={100+100*std::cos(angle),100*std::sin(angle),0};
        for(const auto& v:{Vec3{0,0,0},Vec3{100,0,0},p.end}){ContourPoint c;c.value=v;p.contour.push_back(c);}
        model.parts[1]=p;model.actualPartIds={1};OcctGeometryModel result;std::string error;
        if(!buildOcctGeometry(model,result,error) || result.partShapes.size()!=1 || !BRepCheck_Analyzer(result.partShapes.at(1)).IsValid())return 20;
        GProp_GProps mass;BRepGProp::VolumeProperties(result.partShapes.at(1),mass);
        if(std::abs(mass.Mass()-20000)>1e-5){std::cerr<<"miter mass="<<mass.Mass();return 21;}
        for(double y:{-5.0,5.0})for(double z:{-5.0,5.0})
        {
            const auto x=100-y*std::tan(angle/2);
            BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(gp_Pnt(x,y,z)).Vertex(),result.partShapes.at(1));
            if(!distance.IsDone() || distance.Value()>1e-6)return 22;
        }
    }
    // A complete reversal has no finite bisector; do not return a partial path.
    Model model;Part p;p.id=1;p.internalType=2;p.length=100;p.profile="10*10";p.contourIsPath=true;
    p.axis={1,0,0};p.secondary={0,1,0};p.normal={0,0,1};
    for(const auto& v:{Vec3{0,0,0},Vec3{100,0,0},Vec3{0,0,0}}){ContourPoint c;c.value=v;p.contour.push_back(c);}
    model.parts[1]=p;model.actualPartIds={1};OcctGeometryModel result;std::string error;
    if(!buildOcctGeometry(model,result,error) || !result.partShapes.empty() || result.unbuiltPartIds!=std::vector<std::uint32_t>{1})return 23;
    return 0;
}

int camberingGeometry()
{
    using namespace tekla::db1;
    const auto pi = std::acos(-1.0);
    for (double h : {0.0, 10.0, -10.0, 50.0, -50.0, 100.0, -100.0})
    for (bool tube : {false,true})
    for (bool rotated : {false,true})
    {
        Model m; Part p; p.id=1; p.profile=tube?"TUBE-20*2":"D20"; p.length=100;
        p.origin={23,41,-7}; p.axis=rotated?Vec3{0,1,0}:Vec3{1,0,0};
        p.secondary=rotated?Vec3{0,0,1}:Vec3{0,1,0};p.normal=rotated?Vec3{1,0,0}:Vec3{0,0,1};
        Property q;q.name="PartCambering";q.kind=Property::Kind::Double;q.doubleValue=h;p.properties.push_back(q);
        m.parts[1]=p;m.actualPartIds={1};OcctGeometryModel g;std::string error;
        if(!buildOcctGeometry(m,g,error)||g.partShapes.size()!=1||!BRepCheck_Analyzer(g.partShapes.at(1)).IsValid())return 30;
        if(g.camberedPartIds!=(h==0?std::vector<std::uint32_t>{}:std::vector<std::uint32_t>{1}))return 31;
        const double a=std::abs(h), radius=h==0?0:1250/a+a/2;
        const double center=h==0?0:h-std::copysign(radius,h);
        const double angle=h==0?0:2*std::atan2(50.0,std::abs(center));
        const double sag=h==0?0:(center==0?h:center-std::copysign(radius,center));
        GProp_GProps props;BRepGProp::VolumeProperties(g.partShapes.at(1),props);
        const double expected=pi*(tube?36:100)*(h==0?100:radius*angle);
        if(std::abs(props.Mass()-expected)>expected*1e-8){std::cerr<<"camber volume "<<h<<" "<<props.Mass()<<" "<<expected;return 32;}
        // Width points on both end caps and the mid-arc retain their transverse direction.
        for(double along : {0.0,50.0,100.0})
        for(double sectionRadius : (tube?std::vector<double>{8,10}:std::vector<double>{10}))
        for(double sign : {-1.0,1.0})
        {
            Vec3 v{};for(int k=0;k<3;++k)v[k]=p.origin[k]+along*p.axis[k]+(along==50?sag:0)*p.secondary[k]+sign*sectionRadius*p.normal[k];
            BRepExtrema_DistShapeShape d(BRepBuilderAPI_MakeVertex(gp_Pnt(v[0],v[1],v[2])).Vertex(),g.partShapes.at(1));
            if(!d.IsDone()||d.Value()>1e-6)return 33;
        }
    }
    Model m;Part p;p.id=1;p.profile="D20";p.length=100;p.axis={1,0,0};p.secondary={0,1,0};p.normal={0,0,1};m.actualPartIds={1};
    Property q;q.name="cambering";q.kind=Property::Kind::Double;q.doubleValue=20;p.properties={q};m.parts[1]=p;
    OcctGeometryModel g;std::string error;
    if(!buildOcctGeometry(m,g,error)||g.partShapes.size()!=1||!g.camberedPartIds.empty())return 34;
    for(int bad=0;bad<7;++bad)
    {
        m.booleans.clear();m.cutPlanes.clear();m.boltGroups.clear();p.contour.clear();p.length=100;p.properties.clear();
        q.name="PartCambering";q.kind=Property::Kind::Double;q.doubleValue=20;p.properties={q};
        if(bad==0)p.properties[0].doubleValue=std::numeric_limits<double>::quiet_NaN();
        if(bad==1)p.properties[0].kind=Property::Kind::String;
        if(bad==2){q.doubleValue=30;p.properties.push_back(q);}
        if(bad==3){p.length=10;p.properties[0].doubleValue=5;}
        if(bad==4){ContourPoint c;c.value={0,0,0};p.contour={c,c};p.contourIsPath=true;}
        if(bad==5){PlaneOperation cut;cut.fatherPartId=1;m.cutPlanes.push_back(cut);}
        if(bad==6){p.properties[0].doubleValue=std::numeric_limits<double>::infinity();}
        m.parts[1]=p;
        if(!buildOcctGeometry(m,g,error)||!g.partShapes.empty()||g.unbuiltPartIds!=std::vector<std::uint32_t>{1}||!g.camberedPartIds.empty())return 35+bad;
    }
    return 0;
}

int main(int argc,char** argv)
{
    if(argc==2 && std::string(argv[1])=="nominal")return nominalSections();
    if(argc==2 && std::string(argv[1])=="miter")return polybeamMiters();
    if(argc==2 && std::string(argv[1])=="camber")return camberingGeometry();
    if(argc==2 && std::string(argv[1])=="ellipse")return ellipseSections();
    if(argc!=1)return 2;
    tekla::db1::Model model;
    tekla::db1::Part beam;
    beam.id=1; beam.internalType=2; beam.profile="PL10*10"; beam.length=10;
    beam.end={10,0,0}; beam.axis={1,0,0}; beam.secondary={0,1,0}; beam.normal={0,0,1};
    model.parts[1]=beam;
    auto plate=beam; plate.id=2; plate.profile="PL2";
    for (const auto& p : {tekla::db1::Vec3{0,0,0},tekla::db1::Vec3{10,0,0},tekla::db1::Vec3{10,10,0},tekla::db1::Vec3{0,10,0}})
    { tekla::db1::ContourPoint point; point.value=p; plate.contour.push_back(point); }
    model.parts[2]=plate;
    auto unknown=plate; unknown.id=3; unknown.contourKindUnverified=true; model.parts[3]=unknown;
    model.actualPartIds={1,2,3};
    tekla::db1::BoltDefinition bolt;bolt.id=10;bolt.diameter=10;bolt.length=40;model.boltDefinitions[10]=bolt;
    tekla::db1::BoltGroup absent;absent.id=20;absent.definitionId=10;absent.positionArrayId=0;
    absent.axis={1,0,0};absent.secondary={0,1,0};absent.normal={0,0,1};
    model.boltGroups.push_back(absent);
    auto present=absent;present.id=21;present.positionArrayId=30;present.positions={{0,0,0}};model.boltGroups.push_back(present);
    auto missing=present;missing.id=22;missing.definitionId=999;model.boltGroups.push_back(missing);
    tekla::db1::OcctGeometryModel result; std::string error;
    if (!tekla::db1::buildOcctGeometry(model,result,error)) { std::cerr<<error; return 1; }
    if (result.partShapes.size()!=2 || result.unbuiltPartIds!=std::vector<std::uint32_t>{3}) return 2;
    if(result.unbuiltBoltGroupIds!=std::vector<std::uint32_t>{20,22} || result.boltShapes.size()!=1 || !result.boltShapes.count(21))return 7;
    GProp_GProps boltProperties;BRepGProp::VolumeProperties(result.boltShapes.at(21),boltProperties);
    if(std::abs(boltProperties.Mass()-1000*std::acos(-1.0))>1e-6)return 8;
    for (const auto& expected : {std::pair<std::uint32_t,double>{1,1000},std::pair<std::uint32_t,double>{2,200}})
    {
        GProp_GProps properties; BRepGProp::VolumeProperties(result.partShapes.at(expected.first),properties);
        if (std::abs(properties.Mass()-expected.second)>1e-6) { std::cerr<<properties.Mass(); return 3; }
        tekla::db1::TriangleMesh mesh;
        if (!tekla::db1::tessellateOcctShape(result.partShapes.at(expected.first),mesh,error)) { std::cerr<<error; return 4; }
        if (mesh.indices.empty() || mesh.indices.size()%3) return 5;
        for (auto i:mesh.indices) if (i>=mesh.vertices.size()) return 6;
    }
    return 0;
}
