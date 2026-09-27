#include <tekla/db1/Parser.hpp>
#include <tekla/db1/OcctGeometry.hpp>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include "Ifc730Validation.hpp"
#include "IfcFacetedVolume.hpp"
#include "IfcFacetedVolumeRegression.hpp"
#include "EllipseIfcValidation.hpp" // Independent rings/volume/vertices and non-ellipse transport control.

// Independent analytic volume inferred only from the fixed IFC BREP rings.
// Do not use its undeformed quantity field or the DB1 camber formula here.
double camberedIfcVolume(const std::map<unsigned,ifc730::Entity>& all, const std::set<unsigned>& vertices)
{
    std::vector<tekla::db1::Vec3> points;
    double sectionRadius=0;
    for(auto vertex:vertices){auto p=ifc730::vector(all,vertex,"IFCCARTESIANPOINT");points.push_back(p);sectionRadius=(std::max)(sectionRadius,std::abs(p[1]));}
    ifc730::require(std::abs(sectionRadius-10)<1e-5,"IFC D20 section radius");
    std::vector<tekla::db1::Vec3> centers;
    for(const auto& p:points)if(std::abs(p[1]-sectionRadius)<1e-5)centers.push_back({p[0],0,p[2]});
    std::sort(centers.begin(),centers.end());
    centers.erase(std::unique(centers.begin(),centers.end(),[](const auto&a,const auto&b){return std::hypot(a[0]-b[0],a[2]-b[2])<1e-5;}),centers.end());
    ifc730::require(centers.size()>=6 && centers.size()<=8 && vertices.size()==8*centers.size(),"IFC section rings");
    const auto a=centers.front(),b=centers[centers.size()/2],c=centers.back();
    const double ax=b[0]-a[0],az=b[2]-a[2],bx=c[0]-a[0],bz=c[2]-a[2];
    const double u=(ax*ax+az*az)/2,v=(bx*bx+bz*bz)/2,det=ax*bz-az*bx;
    ifc730::require(std::abs(det)>1e-5,"IFC non-collinear ring centers");
    const double cx=a[0]+(u*bz-az*v)/det,cz=a[2]+(ax*v-u*bx)/det;
    const double radius=std::hypot(a[0]-cx,a[2]-cz);
    for(const auto& p:centers)ifc730::require(std::abs(std::hypot(p[0]-cx,p[2]-cz)-radius)<.0001,"IFC circular centerline");
    const auto angle=std::abs(std::atan2((a[0]-cx)*(c[2]-cz)-(a[2]-cz)*(c[0]-cx),(a[0]-cx)*(c[0]-cx)+(a[2]-cz)*(c[2]-cz)));
    ifc730::require(angle>.1 && angle<std::acos(-1.0),"IFC minor arc");
    return std::acos(-1.0)*sectionRadius*sectionRadius*radius*angle;
}

int check(const std::filesystem::path& directory, bool camberCheck)
{
    using namespace tekla::db1;
    try
    {
        Model model;std::string error;
        ifc730::require(parseModelFile(directory.parent_path()/"btscm-pechelire"/"pechelirejulV1.db1",model,error),error);
        const auto all=ifc730::read(directory/"export.ifc");
        std::map<unsigned,double> volumes;
        for(const auto& item:all)
        {
            const auto& e=item.second;if(e.type!="IFCRELDEFINESBYPROPERTIES")continue;
            const auto& set=all.at(ifc730::entityRef(e.args.at(5)));if(set.type!="IFCPROPERTYSET")continue;
            for(auto property:ifc730::refs(set.args.at(4)))
            {
                const auto& a=ifc730::entity(all,property,"IFCPROPERTYSINGLEVALUE",4).args;
                if(ifc730::string(a[0])!="Volume_avec_trous_et_coupes")continue;
                const std::string prefix="IFCVOLUMEMEASURE(";
                ifc730::require(a[2].substr(0,prefix.size())==prefix && a[2].back()==')',"volume property type");
                const auto value=ifc730::real(a[2].substr(prefix.size(),a[2].size()-prefix.size()-1))*1e9;
                for(auto product:ifc730::refs(e.args[4]))
                {const auto inserted=volumes.emplace(product,value);ifc730::require(inserted.second||inserted.first->second==value,"conflicting volume");}
            }
        }
        OcctGeometryModel geometry;
        ifc730::require(buildOcctGeometry(model,geometry,error),error);
        std::set<unsigned> checked;std::size_t pointsChecked=0;
        double maximumRelativeVolumeError=0,maximumVertexDistance=0;
        for(const auto& item:all)
        {
            const auto& e=item.second;if(e.type!="IFCBEAM" && e.type!="IFCCOLUMN" && e.type!="IFCPLATE")continue;
            const auto tag=ifc730::string(e.args.at(7));ifc730::require(tag.substr(0,3)=="TS_","product tag");
            const auto id=ifc730::number(tag.substr(3));const auto& part=model.parts.at(id);
            const bool cambered=std::any_of(part.properties.begin(),part.properties.end(),[](const auto&p){return p.name=="PartCambering" && p.kind==Property::Kind::Double && p.doubleValue!=0;});
            if(camberCheck?!cambered:(part.profile!="UPE140" && part.profile!="UPE220"))continue;
            ifc730::require(ifc730::guid(ifc730::string(e.args[0]))==part.guid && checked.insert(id).second,"geometry identity");
            const auto& shape=geometry.partShapes.at(id);ifc730::require(BRepCheck_Analyzer(shape).IsValid(),"invalid OCCT solid");
            std::set<unsigned> vertices,active;ifc730::vertices(all,ifc730::entityRef(e.args[6]),vertices,active);
            GProp_GProps mass;BRepGProp::VolumeProperties(shape,mass);
            const auto expected=camberCheck?camberedIfcVolume(all,vertices):volumes.at(item.first);
            ifc730::require(expected>0,"nonpositive IFC volume");
            const auto residual=std::abs(mass.Mass()-expected)/expected;
            maximumRelativeVolumeError=(std::max)(maximumRelativeVolumeError,residual);
            ifc730::require(residual<=1e-5,"OCCT/IFC volume mismatch for "+std::to_string(id)+": "+std::to_string(residual));
            const auto frame=ifc730::placement(all,ifc730::entityRef(e.args[5]));
            for(auto vertex:vertices)
            {
                const auto local=ifc730::vector(all,vertex,"IFCCARTESIANPOINT");Vec3 world{};
                for(std::size_t i=0;i<3;++i)world[i]=frame.origin[i]+local[0]*frame.x[i]+local[1]*frame.y[i]+local[2]*frame.z[i];
                BRepExtrema_DistShapeShape distance(BRepBuilderAPI_MakeVertex(gp_Pnt(world[0],world[1],world[2])).Vertex(),shape);
                ifc730::require(distance.IsDone(),"vertex distance failed");
                maximumVertexDistance=(std::max)(maximumVertexDistance,distance.Value());
                ifc730::require(distance.Value()<=.02,"OCCT/IFC vertex mismatch for "+std::to_string(id)+
                    " vertex="+std::to_string(vertex)+" distance="+std::to_string(distance.Value()));++pointsChecked;
            }
        }
        if(camberCheck)
            ifc730::require(checked==std::set<unsigned>{291219,291261,291352,291493,291514,291534} &&
                std::vector<unsigned>(checked.begin(),checked.end())==geometry.camberedPartIds && pointsChecked==336,"camber coverage/provenance");
        else ifc730::require(checked.size()==25 && std::vector<unsigned>(checked.begin(),checked.end())==geometry.nominalProfilePartIds,"UPE coverage/provenance");
        std::cerr<<"maximumRelativeVolumeError="<<maximumRelativeVolumeError<<" maximumVertexDistanceMm="<<maximumVertexDistance<<'\n';
        if(camberCheck)std::cout<<"cambered_parts="<<checked.size()<<" ifc_vertices="<<pointsChecked<<'\n';
        else std::cout<<"upe_parts="<<checked.size()<<" ifc_vertices="<<pointsChecked<<" nominal_profile_parts="<<geometry.nominalProfilePartIds.size()<<'\n';
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv)
{if(argc==2 && std::wstring(argv[1])==L"ifc_volume_evidence")return volumeEvidenceRegression();if(argc==3 && std::wstring(argv[1])==L"ifc_ellipse_geometry")return checkEllipseIfc(std::filesystem::path(argv[2]));return argc==3 && (std::wstring(argv[1])==L"ifc_geometry" || std::wstring(argv[1])==L"ifc_camber_geometry")?check(std::filesystem::path(argv[2]),std::wstring(argv[1])==L"ifc_camber_geometry"):2;}
#else
int main(int argc,char** argv)
{if(argc==2 && std::string(argv[1])=="ifc_volume_evidence")return volumeEvidenceRegression();if(argc==3 && std::string(argv[1])=="ifc_ellipse_geometry")return checkEllipseIfc(std::filesystem::u8path(argv[2]));return argc==3 && (std::string(argv[1])=="ifc_geometry" || std::string(argv[1])=="ifc_camber_geometry")?check(std::filesystem::u8path(argv[2]),std::string(argv[1])=="ifc_camber_geometry"):2;}
#endif
