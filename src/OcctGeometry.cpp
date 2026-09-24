#include <tekla/db1/OcctGeometry.hpp>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Poly_Triangulation.hxx>
#include <TopAbs.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopTools_ListOfShape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GProp_GProps.hxx>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <regex>
#include <set>
#include <sstream>

namespace tekla::db1
{
namespace
{
constexpr double kTolerance = 1e-7;
constexpr double kPi = 3.14159265358979323846;

Vec3 add(const Vec3& one, const Vec3& two) { return {one[0] + two[0], one[1] + two[1], one[2] + two[2]}; }
Vec3 subtract(const Vec3& one, const Vec3& two) { return {one[0] - two[0], one[1] - two[1], one[2] - two[2]}; }
Vec3 scale(const Vec3& value, double factor) { return {value[0] * factor, value[1] * factor, value[2] * factor}; }
double dot(const Vec3& one, const Vec3& two) { return one[0] * two[0] + one[1] * two[1] + one[2] * two[2]; }
Vec3 cross(const Vec3& one, const Vec3& two)
{
    return {one[1] * two[2] - one[2] * two[1], one[2] * two[0] - one[0] * two[2], one[0] * two[1] - one[1] * two[0]};
}
double length(const Vec3& value) { return std::sqrt(dot(value, value)); }
Vec3 normalized(const Vec3& value)
{
    const auto magnitude = length(value);
    return magnitude > kTolerance ? scale(value, 1.0 / magnitude) : Vec3{0.0, 0.0, 0.0};
}
gp_Pnt point(const Vec3& value) { return gp_Pnt(value[0], value[1], value[2]); }
gp_Vec vector(const Vec3& value) { return gp_Vec(value[0], value[1], value[2]); }

struct Section
{
    struct Point
    {
        std::array<double, 2> value{};
        uint32_t chamferType = 0;
        double chamferX = 0.0;
        double chamferY = 0.0;

        Point() = default;
        Point(double x, double y, uint32_t type = 0, double cx = 0.0, double cy = 0.0)
            : value{x, y}, chamferType(type), chamferX(cx), chamferY(cy) {}
    };
    std::vector<Point> outer;
    std::vector<std::vector<Point>> holes;
    bool centerOnCentroid = false;
};

std::vector<Section::Point> rectangle(double height, double width, double radius = 0.0)
{
    const auto type = radius > kTolerance ? 20U : 0U;
    return {{-height * 0.5, -width * 0.5, type, radius, radius},
            {height * 0.5, -width * 0.5, type, radius, radius},
            {height * 0.5, width * 0.5, type, radius, radius},
            {-height * 0.5, width * 0.5, type, radius, radius}};
}

std::vector<Section::Point> circle(double radius, bool clockwise)
{
    std::vector<Section::Point> result;
    constexpr int segments = 8;
    for (int index = 0; index < segments; ++index)
    {
        const auto angle = (clockwise ? -1.0 : 1.0) * 2.0 * kPi * index / segments;
        result.emplace_back(radius * std::cos(angle), radius * std::sin(angle), index % 2 ? 40U : 0U);
    }
    return result;
}

std::vector<Section::Point> angleSection(double height, double width, double thickness, double rootRadius = 0.0)
{
    const auto h = height * 0.5;
    const auto w = width * 0.5;
    return {{-h, -w}, {h, -w}, {h, -w + thickness},
            {-h + thickness, -w + thickness, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
            {-h + thickness, w}, {-h, w}};
}

double parameter(const Profile& profile, uint32_t id)
{
    const auto found = profile.parameters.find(id);
    return found == profile.parameters.end() ? 0.0 : found->second;
}

const Profile* profileByName(const Model& model, const std::string& name)
{
    const auto found = model.profiles.find(name);
    if (found != model.profiles.end())
        return &found->second;

    const auto signature = [](const std::string& value) {
        struct Result { std::string family; std::vector<double> dimensions; } result;
        const auto firstNumber = value.find_first_of("0123456789");
        result.family = value.substr(0, firstNumber);
        result.family.erase(std::remove_if(result.family.begin(), result.family.end(),
                                           [](unsigned char character) {
                                               return character == '_' || character == '-' ||
                                                      std::isspace(character);
                                           }), result.family.end());
        std::transform(result.family.begin(), result.family.end(), result.family.begin(),
                       [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        const std::regex number(R"(([0-9]+(?:\.[0-9]+)?))");
        for (auto iterator = std::sregex_iterator(value.begin(), value.end(), number);
             iterator != std::sregex_iterator(); ++iterator)
            result.dimensions.push_back(std::stod((*iterator)[1]));
        const std::pair<const char*, const char*> aliases[] = {
            {"CBLA", "CBL"}, {"LBEAM", "BL"}, {"LSPAN", "LS"},
            {"COLUMN", "CN"}, {"CX", "CN"}, {"ITBEAM", "BT"},
            {"XDT", "DT"}, {"RSPAN", "RW"}, {"CORBEL", "ADD"}};
        for (const auto& alias : aliases)
            if (result.family == alias.first)
                result.family = alias.second;
        return result;
    };
    const auto requested = signature(name);
    for (const auto& entry : model.profiles)
    {
        if (entry.second.source != ProfileSource::SketchSolver)
            continue;
        const auto candidate = signature(entry.first);
        if (candidate.family != requested.family ||
            candidate.dimensions.size() != requested.dimensions.size())
            continue;
        bool equal = true;
        for (std::size_t index = 0; index < candidate.dimensions.size(); ++index)
        {
            const auto tolerance = (std::max)(1.0e-3, std::abs(requested.dimensions[index]) * 1.0e-5);
            if (std::abs(candidate.dimensions[index] - requested.dimensions[index]) > tolerance)
            {
                equal = false;
                break;
            }
        }
        if (equal)
            return &entry.second;
    }
    return nullptr;
}

Section sectionFor(const Model& model, const Part& part, std::string& mode)
{
    Section result;
    const auto* catalog = profileByName(model, part.profile);
    if (catalog && !catalog->fixedContours.empty() && !catalog->fixedContours.front().empty())
    {
        const auto convert = [](const std::vector<ProfileContourPoint>& contour) {
            std::vector<Section::Point> converted;
            converted.reserve(contour.size());
            for (const auto& item : contour)
                converted.emplace_back(item.value[0], item.value[1], item.chamferType,
                                       item.chamferX, item.chamferY);
            return converted;
        };
        result.outer = convert(catalog->fixedContours.front());
        for (std::size_t index = 1; index < catalog->fixedContours.size(); ++index)
            if (!catalog->fixedContours[index].empty())
                result.holes.push_back(convert(catalog->fixedContours[index]));
        mode = "profdb fixed contour";
        return result;
    }
    if (catalog)
    {
        const auto height = parameter(*catalog, 36);
        const auto width = parameter(*catalog, 37);
        const auto web = parameter(*catalog, 38);
        const auto flange = parameter(*catalog, 39);
        const auto rootRadius = parameter(*catalog, 40);
        if (catalog->type == 1 && height > 0 && width > 0 && web > 0 && flange > 0)
        {
            result.centerOnCentroid = true;
            const auto h = height * 0.5, w = width * 0.5, tw = web * 0.5;
            result.outer = {{-h, -w}, {-h + flange, -w},
                            {-h + flange, -tw, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
                            {h - flange, -tw, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
                            {h - flange, -w}, {h, -w}, {h, w}, {h - flange, w},
                            {h - flange, tw, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
                            {-h + flange, tw, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
                            {-h + flange, w}, {-h, w}};
            mode = "profdb I";
            return result;
        }
        if (catalog->type == 2 && height > 0 && width > 0)
        {
            const auto thickness = parameter(*catalog, 1164) > 0 ? parameter(*catalog, 1164) : web;
            if (thickness > 0)
            {
                result.centerOnCentroid = true;
                result.outer = angleSection(height, width, thickness, rootRadius);
                mode = "profdb L";
                return result;
            }
        }
        if ((catalog->type == 4 || catalog->type == 9) && height > 0 && width > 0 && web > 0 && flange > 0)
        {
            result.centerOnCentroid = true;
            const auto h = height * 0.5;
            const auto w = width * 0.5;
            result.outer = {{-h, -w}, {-h, w}, {-h + flange, w},
                            {-h + flange, -w + web, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
                            {h - flange, -w + web, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
                            {h - flange, w}, {h, w}, {h, -w}};
            mode = catalog->type == 4 ? "profdb U" : "profdb channel";
            return result;
        }
        if (catalog->type == 3 && height > 0 && width > 0)
        {
            const auto thickness = parameter(*catalog, 3091);
            if (thickness > 0 && height > 2 * thickness && width > thickness)
            {
                const auto h = height * 0.5, t = thickness * 0.5;
                result.centerOnCentroid = true;
                result.outer = {{-h, -width}, {-h, t}, {h - thickness, t}, {h - thickness, width},
                                {h, width}, {h, -t}, {-h + thickness, -t}, {-h + thickness, -width}};
                mode = "profdb Z";
                return result;
            }
        }
        if (catalog->type == 7)
        {
            const auto diameter = parameter(*catalog, 3697) > 0 ? parameter(*catalog, 3697) : parameter(*catalog, 3844);
            const auto wall = parameter(*catalog, 3091);
            if (diameter > 0)
            {
                result.centerOnCentroid = true;
                result.outer = circle(diameter * 0.5, false);
                if (wall > 0 && diameter > 2 * wall)
                    result.holes.push_back(circle(diameter * 0.5 - wall, true));
                mode = "profdb CHS";
                return result;
            }
        }
        if (catalog->type == 10 && height > 0 && width > 0 && web > 0 && flange > 0)
        {
            const auto h = height * 0.5, w = width * 0.5, tw = web * 0.5;
            result.centerOnCentroid = true;
            result.outer = {{-h, -w}, {-h + flange, -w},
                            {-h + flange, -tw, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
                            {h, -tw}, {h, tw},
                            {-h + flange, tw, rootRadius > kTolerance ? 20U : 0U, rootRadius, rootRadius},
                            {-h + flange, w}, {-h, w}};
            mode = "profdb T";
            return result;
        }
        if (catalog->type == 8 && height > 0 && width > 0)
        {
            result.centerOnCentroid = true;
            const auto outerRadius = parameter(*catalog, 3092);
            result.outer = rectangle(height, width, outerRadius);
            const auto wall = parameter(*catalog, 3091);
            if (wall > 0 && height > 2 * wall && width > 2 * wall)
            {
                auto inner = rectangle(height - 2 * wall, width - 2 * wall,
                                       (std::max)(0.0, outerRadius - wall));
                std::reverse(inner.begin(), inner.end());
                result.holes.push_back(std::move(inner));
            }
            mode = "profdb RHS";
            return result;
        }
    }

    std::smatch match;
    // Older Xsteel libraries use HL for the same thickness-by-width flat
    // section that later releases serialize as PL.
    const std::regex plate(R"(^(?:PL|HL)([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex flatBar(R"(^([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex platBar(R"(^PLAT([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex plateOnly(R"(^(?:PL|HL)([0-9.]+)$)", std::regex::icase);
    const std::regex round(R"(^PD([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex solidRound(R"(^D([0-9.]+)$)", std::regex::icase);
    const std::regex circularTube(R"(^TUBE-([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex rectangularTube(R"(^(?:TUBE-C-|TR-F)([0-9.]+)\*([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex angle(R"(^(?:L|RSA)([0-9.]+)\*([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex equalAngle(R"(^L([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex grating(R"(^GRATING([0-9.]+)\*([0-9.]+)(?:-.*)?$)", std::regex::icase);
    const std::regex zee(R"(^ZZ([0-9.]+)-([0-9.]+)-([0-9.]+)-([0-9.]+)$)", std::regex::icase);
    const std::regex upe(R"(^UPE([0-9.]+)$)", std::regex::icase);
    const std::regex ipe(R"(^IPE([0-9.]+)$)", std::regex::icase);
    const std::regex channel(R"(^C([0-9.]+)\*([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    const std::regex sadef(R"(^SADEF-C([0-9.]+)/([0-9.]+)/([0-9.]+)X([0-9.]+)$)", std::regex::icase);
    const std::regex eld(R"(^ELD([0-9.]+)\*([0-9.]+)\*([0-9.]+)\*([0-9.]+)$)", std::regex::icase);
    if (std::regex_match(part.profile, match, plate))
    {
        result.centerOnCentroid = true;
        const auto one = std::stod(match[1]), two = std::stod(match[2]);
        result.outer = rectangle((std::max)(one, two), (std::min)(one, two)); mode = "parametric PL";
    }
    else if (std::regex_match(part.profile, match, flatBar) || std::regex_match(part.profile, match, platBar))
    {
        result.centerOnCentroid = true;
        result.outer = rectangle(std::stod(match[1]), std::stod(match[2])); mode = "parametric flat bar";
    }
    else if (std::regex_match(part.profile, match, plateOnly))
    {
        result.centerOnCentroid = true;
        const auto thickness = std::stod(match[1]);
        result.outer = rectangle(thickness, thickness); mode = "parametric square PL";
    }
    else if (std::regex_match(part.profile, match, round))
    {
        result.centerOnCentroid = true;
        const auto diameter = std::stod(match[1]);
        const auto wall = std::stod(match[2]);
        result.outer = circle(diameter * 0.5, false);
        if (wall > 0 && diameter > 2 * wall)
            result.holes.push_back(circle(diameter * 0.5 - wall, true));
        mode = "parametric PD";
    }
    else if (std::regex_match(part.profile, match, solidRound))
    {
        result.centerOnCentroid = true;
        result.outer = circle(std::stod(match[1]) * 0.5, false); mode = "parametric solid round";
    }
    else if (std::regex_match(part.profile, match, circularTube))
    {
        result.centerOnCentroid = true;
        const auto diameter = std::stod(match[1]), wall = std::stod(match[2]);
        result.outer = circle(diameter * 0.5, false);
        if (wall > 0 && diameter > 2 * wall)
            result.holes.push_back(circle(diameter * 0.5 - wall, true));
        mode = "parametric circular tube";
    }
    else if (std::regex_match(part.profile, match, rectangularTube))
    {
        result.centerOnCentroid = true;
        const auto height = std::stod(match[1]), width = std::stod(match[2]), wall = std::stod(match[3]);
        result.outer = rectangle(height, width);
        if (wall > 0 && height > 2 * wall && width > 2 * wall)
        {
            auto inner = rectangle(height - 2 * wall, width - 2 * wall);
            std::reverse(inner.begin(), inner.end());
            result.holes.push_back(std::move(inner));
        }
        mode = "parametric rectangular tube";
    }
    else if (std::regex_match(part.profile, match, angle))
    {
        result.centerOnCentroid = true;
        result.outer = angleSection(std::stod(match[1]), std::stod(match[2]), std::stod(match[3])); mode = "parametric L";
    }
    else if (std::regex_match(part.profile, match, equalAngle))
    {
        result.centerOnCentroid = true;
        result.outer = angleSection(std::stod(match[1]), std::stod(match[1]), std::stod(match[2])); mode = "parametric equal L";
    }
    else if (std::regex_match(part.profile, match, grating))
    {
        result.centerOnCentroid = true;
        result.outer = rectangle(std::stod(match[1]), std::stod(match[2])); mode = "grating envelope";
    }
    else if (std::regex_match(part.profile, match, zee))
    {
        result.centerOnCentroid = true;
        const auto height = std::stod(match[1]), thickness = std::stod(match[2]);
        const auto lower = std::stod(match[3]), upper = std::stod(match[4]);
        const auto h = height * 0.5, t = thickness * 0.5;
        result.outer = {{-h, -lower}, {-h, t}, {h - thickness, t}, {h - thickness, upper},
                        {h, upper}, {h, -t}, {-h + thickness, -t}, {-h + thickness, -lower}};
        mode = "parametric Z";
    }
    else if (std::regex_match(part.profile, match, upe))
    {
        const auto height = std::stod(match[1]);
        const auto width = 0.25 * height + 30.0;
        const auto web = (std::max)(4.0, height * 0.0275);
        const auto flange = (std::max)(7.0, height * 0.041);
        const auto h = height * 0.5, w = width * 0.5;
        result.centerOnCentroid = true;
        result.outer = {{-h, -w}, {-h, w}, {-h + flange, w},
                        {-h + flange, -w + web}, {h - flange, -w + web},
                        {h - flange, w}, {h, w}, {h, -w}};
        mode = "parametric UPE";
    }
    else if (std::regex_match(part.profile, match, ipe))
    {
        const auto height = std::stod(match[1]);
        const auto width = 0.45 * height + 10.0;
        const auto web = (std::max)(4.1, height * 0.041);
        const auto flange = (std::max)(5.7, height * 0.057);
        const auto h = height * 0.5, w = width * 0.5, tw = web * 0.5;
        result.centerOnCentroid = true;
        result.outer = {{-h, -w}, {-h + flange, -w}, {-h + flange, -tw},
                        {h - flange, -tw}, {h - flange, -w}, {h, -w},
                        {h, w}, {h - flange, w}, {h - flange, tw},
                        {-h + flange, tw}, {-h + flange, w}, {-h, w}};
        mode = "parametric IPE";
    }
    else if (std::regex_match(part.profile, match, channel))
    {
        const auto height = std::stod(match[1]), width = std::stod(match[2]), thickness = std::stod(match[3]);
        const auto h = height * 0.5, w = width * 0.5;
        result.centerOnCentroid = true;
        result.outer = {{-h, -w}, {-h, w}, {-h + thickness, w},
                        {-h + thickness, -w + thickness}, {h - thickness, -w + thickness},
                        {h - thickness, w}, {h, w}, {h, -w}};
        mode = "parametric channel";
    }
    else if (std::regex_match(part.profile, match, sadef))
    {
        const auto height = std::stod(match[1]), width = std::stod(match[2]);
        const auto lip = std::stod(match[3]), thickness = std::stod(match[4]);
        const auto h = height * 0.5, w = width * 0.5;
        result.centerOnCentroid = true;
        result.outer = {{-h, -w}, {-h, w}, {-h + lip, w}, {-h + lip, w - thickness},
                        {-h + thickness, w - thickness}, {-h + thickness, -w + thickness},
                        {h - thickness, -w + thickness}, {h - thickness, w - thickness},
                        {h - lip, w - thickness}, {h - lip, w}, {h, w}, {h, -w}};
        mode = "parametric lipped C";
    }
    else if (std::regex_match(part.profile, match, eld))
    {
        const auto height = std::stod(match[1]) + std::stod(match[3]);
        const auto width = (std::max)(std::stod(match[2]), std::stod(match[4]));
        result.centerOnCentroid = true;
        result.outer = rectangle(height, width); mode = "ELD envelope";
    }
    else if (part.profile.find("ARVAL_") == 0)
    {
        double width = 900.0;
        const std::regex number(R"(([0-9]+(?:\.[0-9]+)?))");
        for (auto iterator = std::sregex_iterator(part.profile.begin(), part.profile.end(), number);
             iterator != std::sregex_iterator(); ++iterator)
            width = (std::max)(width, std::stod((*iterator)[1]));
        result.centerOnCentroid = true;
        result.outer = rectangle(0.75, width); mode = "ARVAL sheet envelope";
    }
    return result;
}

double distance2d(const std::array<double, 2>& one, const std::array<double, 2>& two)
{
    const auto x = one[0] - two[0], y = one[1] - two[1];
    return std::sqrt(x * x + y * y);
}

std::vector<Section::Point> expandSectionCorners(const std::vector<Section::Point>& source)
{
    std::vector<Section::Point> result;
    if (source.size() < 3)
        return result;
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        const auto& item = source[index];
        if (item.chamferType == 0 || item.chamferType == 40)
        {
            result.push_back(item);
            continue;
        }
        const auto& previous = source[(index + source.size() - 1) % source.size()].value;
        const auto& following = source[(index + 1) % source.size()].value;
        const auto towards = [&](const std::array<double, 2>& target, double requested) {
            const auto magnitude = distance2d(target, item.value);
            const auto distance = (std::min)(std::abs(requested), magnitude * 0.499999);
            if (magnitude <= kTolerance || distance <= kTolerance)
                return item.value;
            return std::array<double, 2>{item.value[0] + (target[0] - item.value[0]) * distance / magnitude,
                                         item.value[1] + (target[1] - item.value[1]) * distance / magnitude};
        };
        const auto x = std::abs(item.chamferX);
        const auto y = std::abs(item.chamferY) > kTolerance ? std::abs(item.chamferY) : x;
        if (x <= kTolerance || y <= kTolerance)
        {
            result.push_back(item);
            continue;
        }
        const auto incoming = towards(previous, x);
        const auto outgoing = towards(following, y);
        result.emplace_back(incoming[0], incoming[1]);
        if (item.chamferType == 20 || item.chamferType == 30 || item.chamferType == 70)
        {
            const std::array<double, 2> u{previous[0] - item.value[0], previous[1] - item.value[1]};
            const std::array<double, 2> v{following[0] - item.value[0], following[1] - item.value[1]};
            const auto ul = std::sqrt(u[0] * u[0] + u[1] * u[1]);
            const auto vl = std::sqrt(v[0] * v[0] + v[1] * v[1]);
            std::array<double, 2> middle{
                (incoming[0] + 2.0 * item.value[0] + outgoing[0]) * 0.25,
                (incoming[1] + 2.0 * item.value[1] + outgoing[1]) * 0.25};
            if (ul > kTolerance && vl > kTolerance)
            {
                const std::array<double, 2> nu{-u[1] / ul, u[0] / ul};
                const std::array<double, 2> nv{-v[1] / vl, v[0] / vl};
                const auto denominator = nu[0] * nv[1] - nu[1] * nv[0];
                if (std::abs(denominator) > kTolerance)
                {
                    const std::array<double, 2> delta{outgoing[0] - incoming[0], outgoing[1] - incoming[1]};
                    const auto t = (delta[0] * nv[1] - delta[1] * nv[0]) / denominator;
                    const std::array<double, 2> center{incoming[0] + nu[0] * t, incoming[1] + nu[1] * t};
                    const auto radius = distance2d(center, incoming);
                    const std::array<double, 2> toCorner{item.value[0] - center[0], item.value[1] - center[1]};
                    const auto cornerDistance = std::sqrt(toCorner[0] * toCorner[0] + toCorner[1] * toCorner[1]);
                    if (radius > kTolerance && cornerDistance > kTolerance)
                        middle = {center[0] + toCorner[0] * radius / cornerDistance,
                                  center[1] + toCorner[1] * radius / cornerDistance};
                }
            }
            result.emplace_back(middle[0], middle[1], 40U);
        }
        result.emplace_back(outgoing[0], outgoing[1]);
    }
    return result;
}

TopoDS_Wire wire(const std::vector<Section::Point>& source, const Part& part, double along)
{
    const auto polygon = expandSectionCorners(source);
    if (polygon.size() < 3)
        return {};
    const auto worldPoint = [&](std::size_t index) {
        const auto& coordinate = polygon[index].value;
        return point(add(add(add(part.origin, scale(part.axis, along)), scale(part.secondary, coordinate[0])),
                         scale(part.normal, coordinate[1])));
    };
    auto start = std::size_t{0};
    while (start < polygon.size() && polygon[start].chamferType == 40)
        ++start;
    if (start == polygon.size())
        return {};
    BRepBuilderAPI_MakeWire builder;
    auto addLine = [&](std::size_t one, std::size_t two) {
        const auto p1 = worldPoint(one), p2 = worldPoint(two);
        if (p1.Distance(p2) > kTolerance)
            builder.Add(BRepBuilderAPI_MakeEdge(p1, p2).Edge());
    };
    auto current = start;
    for (std::size_t guard = 0; guard <= polygon.size(); ++guard)
    {
        const auto next = (current + 1) % polygon.size();
        if (polygon[next].chamferType == 40)
        {
            const auto end = (next + 1) % polygon.size();
            if (end == current)
                return {};
            GC_MakeArcOfCircle arc(worldPoint(current), worldPoint(next), worldPoint(end));
            if (arc.IsDone())
                builder.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
            else
            {
                addLine(current, next);
                addLine(next, end);
            }
            current = end;
        }
        else
        {
            addLine(current, next);
            current = next;
        }
        if (current == start)
            break;
    }
    return builder.IsDone() ? builder.Wire() : TopoDS_Wire{};
}

TopoDS_Shape linearSolid(const Model& model, const Part& part, std::string& mode)
{
    const auto section = sectionFor(model, part, mode);
    if (section.outer.size() < 3 || part.length <= kTolerance)
        return {};
    BRepBuilderAPI_MakeFace faceBuilder(wire(section.outer, part, 0.0));
    for (const auto& hole : section.holes)
        faceBuilder.Add(wire(hole, part, 0.0));
    if (!faceBuilder.IsDone())
        return {};
    TopoDS_Shape face = faceBuilder.Face();
    return BRepPrimAPI_MakePrism(TopoDS::Face(face), vector(scale(part.axis, part.length))).Shape();
}

struct ContourPathPoint
{
    Vec3 value{};
    uint32_t type = 0;
};

std::vector<ContourPathPoint> expandedContour(const std::vector<ContourPoint>& source)
{
    std::vector<ContourPathPoint> result;
    if (source.size() < 3)
        return result;
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        const auto& item = source[index];
        if (item.chamferType != 10 && item.chamferType != 20)
        {
            result.push_back({item.value, item.chamferType});
            continue;
        }
        const auto& previous = source[(index + source.size() - 1) % source.size()].value;
        const auto& following = source[(index + 1) % source.size()].value;
        const auto towards = [&](const Vec3& target, double distance) {
            const auto delta = subtract(target, item.value);
            const auto magnitude = length(delta);
            return magnitude > kTolerance ? add(item.value, scale(delta, (std::min)(std::abs(distance), magnitude) / magnitude)) : item.value;
        };
        const auto incoming = towards(previous, item.chamferX);
        const auto outgoing = towards(following, item.chamferY);
        if (item.chamferType == 10)
        {
            result.push_back({incoming, 0}); result.push_back({outgoing, 0});
        }
        else
        {
            Vec3 middle = scale(add(add(incoming, outgoing), scale(item.value, 2.0)), 0.25);
            const auto u = normalized(subtract(previous, item.value));
            const auto v = normalized(subtract(following, item.value));
            const auto planeNormal = normalized(cross(u, v));
            if (length(planeNormal) > kTolerance)
            {
                const auto nu = normalized(cross(planeNormal, u));
                const auto nv = normalized(cross(planeNormal, v));
                const auto lineCross = cross(nu, nv);
                const auto denominator = dot(lineCross, lineCross);
                if (denominator > kTolerance)
                {
                    const auto t = dot(cross(subtract(outgoing, incoming), nv), lineCross) / denominator;
                    const auto center = add(incoming, scale(nu, t));
                    const auto radius = length(subtract(incoming, center));
                    const auto cornerDirection = normalized(subtract(item.value, center));
                    if (radius > kTolerance && length(cornerDirection) > kTolerance)
                        middle = add(center, scale(cornerDirection, radius));
                }
            }
            result.push_back({incoming, 0});
            result.push_back({middle, 40});
            result.push_back({outgoing, 0});
        }
    }
    return result;
}

double plateThickness(const Part& part)
{
    std::smatch match;
    if (std::regex_match(part.profile, match, std::regex(R"(^PL([0-9.]+)$)", std::regex::icase)))
        return std::stod(match[1]);
    if (std::regex_match(part.profile, match, std::regex(R"(^BL([0-9.]+)$)", std::regex::icase)))
        return std::stod(match[1]);
    if (std::regex_match(part.profile, match, std::regex(R"(^([0-9.]+)$)")))
        return std::stod(match[1]);
    const auto* separator = std::strchr(part.profile.c_str(), '*');
    if (part.profile.rfind("PL", 0) == 0 && separator)
        return std::stod(part.profile.substr(2, separator - part.profile.c_str() - 2));
    // Custom component contour plates use a family prefix (HP, HL, M, CBL,
    // and others) followed by their extrusion depth.  The in-plane outline is
    // already explicit in the contour record, so only this trailing depth is
    // needed to reconstruct the solid.
    if (std::regex_search(part.profile, match,
                          std::regex(R"(([0-9]+(?:\.[0-9]+)?)$)")))
        return std::stod(match[1]);
    return 0.0;
}

TopoDS_Shape contourSolid(const Part& part)
{
    const auto thickness = plateThickness(part);
    const auto contour = expandedContour(part.contour);
    if (thickness <= 0 || contour.size() < 3)
        return {};
    std::vector<ContourPathPoint> world;
    world.reserve(contour.size());
    const auto centerOffset = dot(subtract(part.origin, part.start), part.normal);
    const auto baseShift = scale(part.normal, centerOffset - thickness * 0.5);
    for (const auto& item : contour)
    {
        const auto& local = item.value;
        auto position = add(add(part.start, scale(part.axis, local[0])), scale(part.secondary, local[1]));
        position = add(add(position, scale(part.normal, local[2])), baseShift);
        world.push_back({position, item.type});
    }
    auto start = std::size_t{0};
    while (start < world.size() && world[start].type == 40)
        ++start;
    if (start == world.size())
        return {};
    BRepBuilderAPI_MakeWire wireBuilder;
    auto addLine = [&](std::size_t one, std::size_t two) {
        if (length(subtract(world[one].value, world[two].value)) > kTolerance)
            wireBuilder.Add(BRepBuilderAPI_MakeEdge(point(world[one].value), point(world[two].value)).Edge());
    };
    auto current = start;
    for (std::size_t guard = 0; guard <= world.size(); ++guard)
    {
        const auto next = (current + 1) % world.size();
        if (world[next].type == 40)
        {
            const auto end = (next + 1) % world.size();
            GC_MakeArcOfCircle arc(point(world[current].value), point(world[next].value), point(world[end].value));
            if (arc.IsDone()) wireBuilder.Add(BRepBuilderAPI_MakeEdge(arc.Value()).Edge());
            else { addLine(current, next); addLine(next, end); }
            current = end;
        }
        else
        {
            addLine(current, next);
            current = next;
        }
        if (current == start)
            break;
    }
    if (!wireBuilder.IsDone())
        return {};
    BRepBuilderAPI_MakeFace face(wireBuilder.Wire());
    const auto extrusion = scale(part.normal, thickness);
    return face.IsDone() ? BRepPrimAPI_MakePrism(face.Face(), vector(extrusion)).Shape() : TopoDS_Shape{};
}

TopoDS_Shape polybeamSolid(const Model& model, const Part& part, std::string& mode)
{
    if (part.contour.size() < 2)
        return {};
    const auto expanded = expandedContour(part.contour);
    std::vector<Vec3> path;
    path.reserve(expanded.size() * 2);
    const auto append = [&](const Vec3& value) {
        const auto world = add(part.start, value);
        if (path.empty() || length(subtract(world, path.back())) > kTolerance)
            path.push_back(world);
    };
    for (std::size_t index = 0; index < expanded.size(); ++index)
    {
        if (expanded[index].type == 40 && !path.empty() && index + 1 < expanded.size())
        {
            const auto middle = add(part.start, expanded[index].value);
            const auto ending = add(part.start, expanded[index + 1].value);
            GC_MakeArcOfCircle arc(point(path.back()), point(middle), point(ending));
            if (arc.IsDone())
            {
                const auto curve = arc.Value();
                constexpr int subdivisions = 8;
                for (int step = 1; step <= subdivisions; ++step)
                {
                    const auto parameter = curve->FirstParameter() +
                                           (curve->LastParameter() - curve->FirstParameter()) * step / subdivisions;
                    const auto value = curve->Value(parameter);
                    const Vec3 sample{value.X(), value.Y(), value.Z()};
                    if (path.empty() || length(subtract(sample, path.back())) > kTolerance)
                        path.push_back(sample);
                }
                ++index;
                continue;
            }
        }
        append(expanded[index].value);
    }
    if (path.size() < 2)
        return {};
    TopoDS_Compound result;
    BRep_Builder builder;
    builder.MakeCompound(result);
    std::string sectionMode;
    const auto section = sectionFor(model, part, sectionMode);
    double minimumSecondary = (std::numeric_limits<double>::max)();
    double maximumSecondary = (std::numeric_limits<double>::lowest)();
    double minimumNormal = (std::numeric_limits<double>::max)();
    double maximumNormal = (std::numeric_limits<double>::lowest)();
    for (const auto& point : section.outer)
    {
        minimumSecondary = (std::min)(minimumSecondary, point.value[0]);
        maximumSecondary = (std::max)(maximumSecondary, point.value[0]);
        minimumNormal = (std::min)(minimumNormal, point.value[1]);
        maximumNormal = (std::max)(maximumNormal, point.value[1]);
    }
    Vec3 pathPlaneNormal{};
    for (std::size_t index = 2; index < path.size(); ++index)
    {
        const auto previous = subtract(path[index - 1], path[index - 2]);
        const auto following = subtract(path[index], path[index - 1]);
        const auto candidate = cross(previous, following);
        if (length(candidate) > kTolerance)
        {
            pathPlaneNormal = normalized(candidate);
            break;
        }
    }
    const auto preserveSecondary = length(pathPlaneNormal) > kTolerance
                                       ? std::abs(dot(part.secondary, pathPlaneNormal)) >= std::abs(dot(part.normal, pathPlaneNormal))
                                       : maximumSecondary - minimumSecondary >= maximumNormal - minimumNormal;
    const auto profileOffset = subtract(part.origin, part.start);
    const auto secondaryOffset = dot(profileOffset, part.secondary);
    const auto normalOffset = dot(profileOffset, part.normal);
    std::size_t segmentCount = 0;
    for (std::size_t index = 1; index < path.size(); ++index)
    {
        const auto delta = subtract(path[index], path[index - 1]);
        const auto segmentLength = length(delta);
        if (segmentLength <= kTolerance)
            continue;
        Part segment = part;
        segment.contour.clear();
        segment.axis = scale(delta, 1.0 / segmentLength);
        if (preserveSecondary)
        {
            auto secondary = subtract(part.secondary, scale(segment.axis, dot(part.secondary, segment.axis)));
            if (length(secondary) <= kTolerance)
                secondary = subtract(part.normal, scale(segment.axis, dot(part.normal, segment.axis)));
            segment.secondary = normalized(secondary);
            segment.normal = normalized(cross(segment.axis, segment.secondary));
        }
        else
        {
            auto normal = subtract(part.normal, scale(segment.axis, dot(part.normal, segment.axis)));
            if (length(normal) <= kTolerance)
                normal = subtract(part.secondary, scale(segment.axis, dot(part.secondary, segment.axis)));
            segment.normal = normalized(normal);
            segment.secondary = normalized(cross(segment.normal, segment.axis));
            segment.normal = normalized(cross(segment.axis, segment.secondary));
        }
        segment.origin = add(add(path[index - 1], scale(segment.secondary, secondaryOffset)),
                             scale(segment.normal, normalOffset));
        segment.length = segmentLength;
        std::string segmentMode;
        const auto solid = linearSolid(model, segment, segmentMode);
        if (!solid.IsNull())
        {
            builder.Add(result, solid);
            ++segmentCount;
            mode = segmentMode;
        }
    }
    if (!segmentCount)
        return {};
    mode = "DB1 polybeam path / " + mode;
    return result;
}

TopoDS_Shape partSolid(const Model& model, const Part& part, std::string& mode)
{
    if (part.contourKindUnverified)
    {
        mode = "unverified contour kind";
        return {};
    }
    if (!part.contour.empty())
    {
        if (part.contourIsPath)
            return polybeamSolid(model, part, mode);
        mode = "DB1 contour plate";
        return contourSolid(part);
    }
    return linearSolid(model, part, mode);
}

bool applyBoolean(TopoDS_Shape& father, const TopoDS_Shape& tool, bool additive)
{
    if (father.IsNull() || tool.IsNull())
        return false;
    if (!additive)
    {
        Bnd_Box fatherBounds, toolBounds;
        BRepBndLib::Add(father, fatherBounds);
        BRepBndLib::Add(tool, toolBounds);
        if (!fatherBounds.IsVoid() && !toolBounds.IsVoid())
        {
            double fx0, fy0, fz0, fx1, fy1, fz1, tx0, ty0, tz0, tx1, ty1, tz1;
            fatherBounds.Get(fx0, fy0, fz0, fx1, fy1, fz1);
            toolBounds.Get(tx0, ty0, tz0, tx1, ty1, tz1);
            constexpr double overlapTolerance = 1e-4;
            if (fx1 < tx0 - overlapTolerance || tx1 < fx0 - overlapTolerance ||
                fy1 < ty0 - overlapTolerance || ty1 < fy0 - overlapTolerance ||
                fz1 < tz0 - overlapTolerance || tz1 < fz0 - overlapTolerance)
                return true;
        }
    }
    for (const auto fuzzy : {5e-2, 1e-3, 1e-5})
    {
        TopTools_ListOfShape arguments, tools;
        arguments.Append(father);
        tools.Append(tool);
        if (additive)
        {
            BRepAlgoAPI_Fuse operation;
            operation.SetArguments(arguments);
            operation.SetTools(tools);
            operation.SetFuzzyValue(fuzzy);
            operation.SetNonDestructive(true);
            operation.SetRunParallel(false);
            operation.Build();
            if (operation.IsDone() && !operation.Shape().IsNull())
            {
                GProp_GProps properties;
                BRepGProp::VolumeProperties(operation.Shape(), properties);
                if (additive || std::abs(properties.Mass()) > kTolerance)
                {
                    father = operation.Shape();
                    return true;
                }
            }
        }
        else
        {
            BRepAlgoAPI_Cut operation;
            operation.SetArguments(arguments);
            operation.SetTools(tools);
            operation.SetFuzzyValue(fuzzy);
            operation.SetNonDestructive(true);
            operation.SetRunParallel(false);
            operation.Build();
            if (operation.IsDone() && !operation.Shape().IsNull())
            {
                GProp_GProps properties;
                BRepGProp::VolumeProperties(operation.Shape(), properties);
                if (std::abs(properties.Mass()) > kTolerance)
                {
                    father = operation.Shape();
                    return true;
                }
            }
        }
    }
    return false;
}

bool applyPlane(TopoDS_Shape& shape, const PlaneOperation& operation)
{
    if (shape.IsNull() || length(operation.normal) <= kTolerance)
        return false;
    const auto normal = normalized(operation.normal);
    const gp_Pln plane(point(operation.origin), gp_Dir(vector(normal)));
    BRepBuilderAPI_MakeFace face(plane);
    // Plane-cut objects retain the negative normal side.  A fitting trims one
    // end of the current solid; its stored normal is not consistently oriented
    // across old Xsteel releases, so the side containing the solid's volume
    // centroid is retained.
    std::vector<Vec3> references{subtract(operation.origin, normal)};
    if (operation.internalType == 9)
    {
        GProp_GProps properties;
        BRepGProp::VolumeProperties(shape, properties);
        if (std::abs(properties.Mass()) > kTolerance)
        {
            const auto center = properties.CentreOfMass();
            references = {Vec3{center.X(), center.Y(), center.Z()}};
        }
    }
    else
        references.push_back(add(operation.origin, normal));
    for (const auto& reference : references)
    {
        BRepPrimAPI_MakeHalfSpace halfSpace(face.Face(), point(reference));
        if (!halfSpace.IsDone())
            continue;
        std::vector<TopoDS_Shape> retainedSolids;
        bool allResolved = true;
        for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next())
        {
            bool resolved = false;
            for (const auto fuzzy : {1e-5, 1e-3, 5e-2})
            {
                TopTools_ListOfShape arguments, tools;
                arguments.Append(explorer.Current());
                tools.Append(halfSpace.Solid());
                BRepAlgoAPI_Common common;
                common.SetArguments(arguments);
                common.SetTools(tools);
                common.SetFuzzyValue(fuzzy);
                common.SetNonDestructive(true);
                common.SetRunParallel(false);
                common.Build();
                if (!common.IsDone() || common.Shape().IsNull())
                    continue;
                GProp_GProps properties;
                BRepGProp::VolumeProperties(common.Shape(), properties);
                if (std::abs(properties.Mass()) > kTolerance)
                    retainedSolids.push_back(common.Shape());
                resolved = true;
                break;
            }
            if (!resolved)
            {
                allResolved = false;
                break;
            }
        }
        if (!allResolved || retainedSolids.empty())
            continue;
        if (retainedSolids.size() == 1)
            shape = retainedSolids.front();
        else
        {
            TopoDS_Compound clipped;
            BRep_Builder builder;
            builder.MakeCompound(clipped);
            for (const auto& solid : retainedSolids)
                builder.Add(clipped, solid);
            shape = clipped;
        }
        return true;
    }
    return false;
}

struct BoltPlacement
{
    Vec3 center{};
    Vec3 axis{};
};

std::vector<BoltPlacement> boltPlacements(const BoltGroup& group)
{
    const auto x = normalized(group.axis);
    const auto y = normalized(group.secondary);
    const auto axis = normalized(group.normal);
    std::vector<BoltPlacement> result;
    for (const auto& local : group.positions)
        result.push_back({add(add(add(group.origin, scale(x, local[0])), scale(y, local[1])), scale(axis, local[2])), axis});
    return result;
}

TopoDS_Shape compound(const std::vector<TopoDS_Shape>& shapes)
{
    TopoDS_Compound result;
    BRep_Builder builder;
    builder.MakeCompound(result);
    for (const auto& shape : shapes)
        if (!shape.IsNull())
            builder.Add(result, shape);
    return result;
}
}

bool buildOcctGeometry(const Model& model, OcctGeometryModel& result, std::string& error,
                       const OcctGeometryOptions& options)
{
    try
    {
        result = {};
        std::unordered_map<uint32_t, TopoDS_Shape> allPartShapes;
        std::unordered_map<uint32_t, std::string> constructionModes;
        std::size_t completed = 0;
        for (const auto& entry : model.parts)
        {
            if (options.progress)
                options.progress("parts", entry.first, completed, model.parts.size());
            ++completed;
            std::string mode;
            auto shape = partSolid(model, entry.second, mode);
            if (shape.IsNull())
            {
                result.unbuiltPartIds.push_back(entry.first);
                result.diagnostics.emplace_back("cannot construct profile " + entry.second.profile +
                                                " for part " + std::to_string(entry.first));
                continue;
            }
            allPartShapes[entry.first] = std::move(shape);
            constructionModes[entry.first] = std::move(mode);
        }
        const auto applyToOperative = [&](const PlaneOperation& operation, const char* label) {
            const auto found = allPartShapes.find(operation.fatherPartId);
            if (found != allPartShapes.end() &&
                std::find(model.operativePartIds.begin(), model.operativePartIds.end(), operation.fatherPartId) != model.operativePartIds.end() &&
                !applyPlane(found->second, operation))
                result.diagnostics.emplace_back(std::string(label) + " failed: " + std::to_string(operation.id));
        };
        for (const auto& operation : model.fittings) applyToOperative(operation, "operative fitting");
        for (const auto& operation : model.cutPlanes) applyToOperative(operation, "operative cut plane");

        // An operative part can itself be machined before it is used as the
        // cutting tool of a real part.  These nested operations are present in
        // old Xsteel 7.x models and must not be reported as missing real parts.
        completed = 0;
        for (const auto& operation : model.booleans)
        {
            if (options.progress)
                options.progress("booleans", operation.id, completed, model.booleans.size());
            const auto father = allPartShapes.find(operation.fatherPartId);
            const auto tool = allPartShapes.find(operation.id);
            if (father != allPartShapes.end() && tool != allPartShapes.end() &&
                std::find(model.operativePartIds.begin(), model.operativePartIds.end(), operation.fatherPartId) != model.operativePartIds.end() &&
                !applyBoolean(father->second, tool->second, operation.additive))
                result.diagnostics.emplace_back("nested operative boolean failed: " + std::to_string(operation.id));
        }

        for (const auto partId : model.actualPartIds)
        {
            const auto found = allPartShapes.find(partId);
            if (found != allPartShapes.end())
                result.partShapes[partId] = found->second;
        }

        for (const auto& operation : model.booleans)
        {
            if (std::find(model.operativePartIds.begin(), model.operativePartIds.end(), operation.fatherPartId) != model.operativePartIds.end())
                continue;
            auto father = result.partShapes.find(operation.fatherPartId);
            const auto tool = allPartShapes.find(operation.id);
            if (father == result.partShapes.end() || tool == allPartShapes.end() ||
                !applyBoolean(father->second, tool->second, operation.additive))
                result.diagnostics.emplace_back("boolean operation failed: " + std::to_string(operation.id));
            ++completed;
        }

        std::set<std::string> approximationModes;
        for (const auto partId : model.actualPartIds)
        {
            const auto mode = constructionModes.find(partId);
            if (mode != constructionModes.end() && mode->second.find("envelope") != std::string::npos)
                approximationModes.insert(mode->second + " for profile " + model.parts.at(partId).profile);
        }
        for (const auto& mode : approximationModes)
            result.diagnostics.emplace_back("profile geometry approximation used: " + mode);
        for (const auto& operation : model.fittings)
        {
            const auto father = result.partShapes.find(operation.fatherPartId);
            if (father != result.partShapes.end() && !applyPlane(father->second, operation))
                result.diagnostics.emplace_back("fitting failed: " + std::to_string(operation.id));
        }
        for (const auto& operation : model.cutPlanes)
        {
            const auto father = result.partShapes.find(operation.fatherPartId);
            if (father != result.partShapes.end() && !applyPlane(father->second, operation))
                result.diagnostics.emplace_back("cut plane failed: " + std::to_string(operation.id));
        }

        std::unordered_map<uint32_t, std::vector<TopoDS_Shape>> holesByPart;
        bool boltLayersUnavailable = false;
        completed = 0;
        for (const auto& group : model.boltGroups)
        {
            if (options.progress)
                options.progress("bolts", group.id, completed, model.boltGroups.size());
            const auto definition = model.boltDefinitions.find(group.definitionId);
            if (definition == model.boltDefinitions.end())
                continue;
            const auto placements = boltPlacements(group);
            std::vector<TopoDS_Shape> bolts;
            const auto radius = (std::max)(1.0, static_cast<double>(definition->second.diameter) * 0.5);
            const auto boltLength = (std::max)(static_cast<double>(definition->second.length), radius * 6.0);
            for (const auto& placement : placements)
            {
                const auto start = add(placement.center, scale(placement.axis, -boltLength * 0.5));
                bolts.push_back(BRepPrimAPI_MakeCylinder(gp_Ax2(point(start), gp_Dir(vector(placement.axis))), radius, boltLength).Shape());
                const auto holeRadius = radius + (std::max)(0.0, static_cast<double>(definition->second.tolerance));
                const auto cutterStart = add(placement.center, scale(placement.axis, -5000.0));
                const auto cutter = BRepPrimAPI_MakeCylinder(gp_Ax2(point(cutterStart), gp_Dir(vector(placement.axis))), holeRadius, 10000.0).Shape();
                if (group.layers.empty())
                    boltLayersUnavailable = true;
                else
                    for (const auto& layer : group.layers)
                        if (result.partShapes.count(layer.partId))
                            holesByPart[layer.partId].push_back(cutter);
            }
            result.boltShapes[group.id] = compound(bolts);
            ++completed;
        }
        for (auto& entry : holesByPart)
        {
            auto& father = result.partShapes.at(entry.first);
            const auto cutters = compound(entry.second);
            if (!applyBoolean(father, cutters, false))
                result.diagnostics.emplace_back("bolt-hole batch failed for part " + std::to_string(entry.first));
        }
        if (boltLayersUnavailable)
            result.diagnostics.emplace_back("bolt solids imported without hole subtraction because this DB1 schema has no bolt-layer records");

        for (auto& entry : result.partShapes)
        {
            GProp_GProps properties;
            BRepGProp::VolumeProperties(entry.second, properties);
            if (properties.Mass() < 0.0)
                entry.second.Reverse();
        }

        completed = 0;
        for (const auto& weld : model.welds)
        {
            if (options.progress)
                options.progress("welds", weld.id, completed, model.welds.size());
            const auto definition = model.weldDefinitions.find(weld.definitionId);
            const auto size = definition == model.weldDefinitions.end() ? 3.0 : (std::max)(2.0, std::abs(static_cast<double>(definition->second.size)));
            const auto start = weld.origin;
            BRepBuilderAPI_MakePolygon polygon;
            polygon.Add(point(add(start, scale(weld.secondary, -size * 0.5))));
            polygon.Add(point(add(start, scale(weld.secondary, size * 0.5))));
            polygon.Add(point(add(start, scale(weld.normal, size * 0.5))));
            polygon.Close();
            BRepBuilderAPI_MakeFace face(polygon.Wire());
            if (face.IsDone() && weld.length > kTolerance)
                result.weldShapes[weld.id] = BRepPrimAPI_MakePrism(face.Face(), vector(scale(weld.axis, weld.length))).Shape();
            ++completed;
        }
        if (!model.boltGroups.empty())
            result.diagnostics.emplace_back("bolt geometry is approximate: cylindrical shanks, without heads, nuts or washers");
        if (!model.welds.empty())
            result.diagnostics.emplace_back("weld geometry is approximate: triangular straight prisms, not all weld types");
        if (std::any_of(model.parts.begin(), model.parts.end(), [](const auto& entry) { return entry.second.contourIsPath; }))
            result.diagnostics.emplace_back("polybeam geometry may approximate curved path segments with eight chords");
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        result = {};
        return false;
    }
}

bool tessellateOcctShape(const TopoDS_Shape& shape, TriangleMesh& result, std::string& error,
                         const TessellationOptions& options)
{
    result = {};
    if (shape.IsNull())
    {
        error = "OCCT shape is empty";
        return false;
    }
    Bnd_Box bounds;
    BRepBndLib::Add(shape, bounds);
    if (bounds.IsVoid())
    {
        error = "Tekla DB1 shape has no bounds";
        return false;
    }
    double xmin, ymin, zmin, xmax, ymax, zmax;
    bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const auto diagonal = std::sqrt((xmax - xmin) * (xmax - xmin) + (ymax - ymin) * (ymax - ymin) + (zmax - zmin) * (zmax - zmin));
    const auto linearDeflection = options.linearDeflection > 0.0
                                      ? options.linearDeflection
                                      : (std::max)(0.01, diagonal * options.relativeDeflection);
    if (!std::isfinite(linearDeflection) || linearDeflection <= 0.0 ||
        !std::isfinite(options.angularDeflection) || options.angularDeflection <= 0.0)
    {
        error = "invalid OCCT tessellation tolerance";
        return false;
    }
    BRepMesh_IncrementalMesh mesher(shape, linearDeflection, false,
                                    options.angularDeflection, options.parallel);
    if (!mesher.IsDone())
    {
        error = "Open CASCADE failed to tessellate Tekla DB1 shape";
        return false;
    }
    result.origin = {(xmin + xmax) * 0.5, (ymin + ymax) * 0.5, (zmin + zmax) * 0.5};
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next())
    {
        const auto face = TopoDS::Face(explorer.Current());
        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull())
            continue;
        const auto transform = location.Transformation();
        for (int index = 1; index <= triangulation->NbTriangles(); ++index)
        {
            int one, two, three;
            triangulation->Triangle(index).Get(one, two, three);
            if (face.Orientation() == TopAbs_REVERSED)
                std::swap(two, three);
            gp_Pnt points[3]{triangulation->Node(one), triangulation->Node(two), triangulation->Node(three)};
            for (auto& item : points)
                item.Transform(transform);
            gp_Vec normal(gp_Vec(points[0], points[1]).Crossed(gp_Vec(points[0], points[2])));
            if (normal.Magnitude() <= kTolerance)
                continue;
            normal.Normalize();
            if (result.vertices.size() > (std::numeric_limits<std::uint32_t>::max)() - 3U)
            {
                error = "OCCT tessellation exceeds 32-bit mesh index capacity";
                result = {};
                return false;
            }
            const auto base = static_cast<uint32_t>(result.vertices.size());
            for (const auto& item : points)
            {
                result.vertices.push_back({item.X() - result.origin[0], item.Y() - result.origin[1],
                                           item.Z() - result.origin[2]});
                result.normals.push_back({normal.X(), normal.Y(), normal.Z()});
            }
            result.indices.insert(result.indices.end(), {base, base + 1, base + 2});
        }
    }
    if (result.indices.empty())
    {
        error = "Tekla DB1 tessellation produced no triangles";
        result = {};
        return false;
    }
    return true;
}
}
