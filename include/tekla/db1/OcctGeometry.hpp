#pragma once

#include <tekla/db1/Model.hpp>

#include <TopoDS_Shape.hxx>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tekla::db1
{
// Optional Open CASCADE result.  This header belongs to the separate
// TeklaDB1::occt target; including the core Parser.hpp never requires OCCT.
struct OcctGeometryModel
{
    std::unordered_map<std::uint32_t, TopoDS_Shape> partShapes;
    std::unordered_map<std::uint32_t, TopoDS_Shape> boltShapes;
    std::unordered_map<std::uint32_t, TopoDS_Shape> weldShapes;
    // Parts whose database records were parsed successfully but whose profile
    // or solid operation could not yet be represented by this OCCT backend.
    // They remain visible to callers instead of being silently discarded.
    std::vector<std::uint32_t> unbuiltPartIds;
    std::vector<std::string> diagnostics;
    // No empty compounds or invented placements are reported as built bolts.
    std::vector<std::uint32_t> unbuiltBoltGroupIds;
};

struct OcctGeometryOptions
{
    // Optional low-frequency progress hook for long-running model builds.
    // stage is stable API text (parts, booleans, bolts, welds); objectId is
    // the DB1 object currently being processed.
    std::function<void(std::string_view stage, std::uint32_t objectId,
                       std::size_t completed, std::size_t total)> progress;
};

struct TessellationOptions
{
    // <= 0 selects max(0.01, bounding-box diagonal * relativeDeflection).
    double linearDeflection = 0.0;
    double relativeDeflection = 1.0e-4;
    double angularDeflection = 0.25;
    bool parallel = true;
};

struct TriangleMesh
{
    // Vertices are relative to this origin to retain float-like rendering
    // precision even when a model uses large world coordinates.
    Vec3 origin{};
    std::vector<Vec3> vertices;
    std::vector<Vec3> normals;
    std::vector<std::uint32_t> indices;
};

bool buildOcctGeometry(const Model& model, OcctGeometryModel& result, std::string& error,
                       const OcctGeometryOptions& options = {});
bool tessellateOcctShape(const TopoDS_Shape& shape, TriangleMesh& result, std::string& error,
                         const TessellationOptions& options = {});
}
