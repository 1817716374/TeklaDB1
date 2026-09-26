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

int check(const std::filesystem::path& directory)
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
            if(part.profile!="UPE140" && part.profile!="UPE220")continue;
            ifc730::require(ifc730::guid(ifc730::string(e.args[0]))==part.guid && checked.insert(id).second,"geometry identity");
            const auto& shape=geometry.partShapes.at(id);ifc730::require(BRepCheck_Analyzer(shape).IsValid(),"invalid OCCT solid");
            GProp_GProps mass;BRepGProp::VolumeProperties(shape,mass);const auto expected=volumes.at(item.first);
            ifc730::require(expected>0,"nonpositive IFC volume");
            const auto residual=std::abs(mass.Mass()-expected)/expected;
            maximumRelativeVolumeError=(std::max)(maximumRelativeVolumeError,residual);
            ifc730::require(residual<=1e-5,"OCCT/IFC volume mismatch for "+std::to_string(id)+": "+std::to_string(residual));
            const auto frame=ifc730::placement(all,ifc730::entityRef(e.args[5]));
            std::set<unsigned> vertices,active;ifc730::vertices(all,ifc730::entityRef(e.args[6]),vertices,active);
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
        ifc730::require(checked.size()==25 && std::vector<unsigned>(checked.begin(),checked.end())==geometry.nominalProfilePartIds,"UPE coverage/provenance");
        std::cerr<<"maximumRelativeVolumeError="<<maximumRelativeVolumeError<<" maximumVertexDistanceMm="<<maximumVertexDistance<<'\n';
        std::cout<<"upe_parts="<<checked.size()<<" ifc_vertices="<<pointsChecked<<" nominal_profile_parts="<<geometry.nominalProfilePartIds.size()<<'\n';
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv)
{return argc==3 && std::wstring(argv[1])==L"ifc_geometry"?check(std::filesystem::path(argv[2])):2;}
#else
int main(int argc,char** argv)
{return argc==3 && std::string(argv[1])=="ifc_geometry"?check(std::filesystem::u8path(argv[2])):2;}
#endif
