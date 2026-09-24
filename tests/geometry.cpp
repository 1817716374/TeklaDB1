#include <tekla/db1/OcctGeometry.hpp>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <cmath>
#include <iostream>

int main()
{
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
    tekla::db1::OcctGeometryModel result; std::string error;
    if (!tekla::db1::buildOcctGeometry(model,result,error)) { std::cerr<<error; return 1; }
    if (result.partShapes.size()!=2 || result.unbuiltPartIds!=std::vector<std::uint32_t>{3}) return 2;
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
