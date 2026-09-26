// Independent oracle for the pinned IFC2X3 export only. This deliberately is
// not a general IFC reader or a public API. Expected values come from IFC,
// never from a saved output of this library.
namespace ifc730
{
using V = tekla::db1::Vec3;
struct Entity { std::string type; std::vector<std::string> args; };
using Entities = std::map<unsigned, Entity>;
void require(bool ok, const std::string& message)
{ if (!ok) throw std::runtime_error("IFC 7.30 evidence: " + message); }

std::vector<std::string> split(const std::string& s)
{
    std::vector<std::string> out; std::size_t start=0; int level=0; bool quoted=false;
    for(std::size_t i=0;i<s.size();++i)
    {
        const char c=s[i];
        if(c=='\'')
        {
            if(quoted && i+1<s.size() && s[i+1]=='\'') { ++i; continue; }
            quoted=!quoted;
        }
        else if(!quoted)
        {
            if(c=='(') ++level;
            else if(c==')') { --level; require(level>=0,"unbalanced STEP arguments"); }
            else if(c==',' && !level) { out.push_back(s.substr(start,i-start)); start=i+1; }
        }
    }
    require(!quoted && !level,"unfinished STEP argument");out.push_back(s.substr(start));return out;
}
unsigned number(const std::string& s)
{
    require(!s.empty() && s.find_first_not_of("0123456789")==std::string::npos,"bad unsigned number");
    const auto v=std::stoull(s);require(v<=UINT32_MAX,"number overflow");return static_cast<unsigned>(v);
}
unsigned entityRef(const std::string& s)
{ require(!s.empty() && s[0]=='#',"missing reference");return number(s.substr(1)); }
std::vector<unsigned> refs(const std::string& s)
{
    if(!s.empty() && s[0]=='#') return {entityRef(s)};
    require(s.size()>=2 && s.front()=='(' && s.back()==')',"bad reference list");
    std::vector<unsigned> out;if(s.size()==2)return out;
    for(const auto& x:split(s.substr(1,s.size()-2)))out.push_back(entityRef(x));return out;
}
std::string string(const std::string& s)
{
    require(s.size()>=2 && s.front()=='\'' && s.back()=='\'',"bad STEP string");
    std::string out;
    for(std::size_t i=1;i+1<s.size();++i)
    {
        unsigned char c=static_cast<unsigned char>(s[i]);
        if(c=='\'') { require(i+2<s.size() && s[i+1]=='\'',"unescaped quote");++i; }
        if(c=='\\')
        {
            if(i+4<s.size() && s.compare(i,3,"\\S\\")==0)
            {
                c=static_cast<unsigned char>(s[i+3]);require(c>=32 && c<=126,"bad STEP shift");
                const unsigned value=c+128;out+=static_cast<char>(0xc0|(value>>6));out+=static_cast<char>(0x80|(value&63));i+=3;continue;
            }
            require(i+2<s.size() && s[i+1]=='\\',"unsupported STEP string escape");++i;
        }
        out+=static_cast<char>(c);
    }
    return out;
}
double real(const std::string& s)
{
    std::size_t used=0;const auto v=std::stod(s,&used);
    require(used==s.size() && std::isfinite(v),"invalid real");return v;
}
Entities read(const std::filesystem::path& path)
{
    require(std::filesystem::file_size(path)<=64*1024*1024,"file budget");
    std::ifstream in(path);require(bool(in),"open export");Entities result;std::string line;bool schema=false;
    while(std::getline(in,line))
    {
        if(!line.empty() && line.back()=='\r')line.pop_back();
        if(line=="FILE_SCHEMA(('IFC2X3'));")schema=true;
        if(line.empty() || line[0]!='#')continue;
        const auto equal=line.find("= "),open=line.find('(',equal);
        require(equal!=std::string::npos && open!=std::string::npos && line.size()>open+2 && line.substr(line.size()-2)==");","entity syntax");
        Entity e{line.substr(equal+2,open-equal-2),split(line.substr(open+1,line.size()-open-3))};
        require(result.emplace(number(line.substr(1,equal-1)),std::move(e)).second,"duplicate entity ID");
    }
    require(schema && !result.empty() && in.eof(),"incomplete IFC2X3 export");return result;
}
const Entity& entity(const Entities& all,unsigned id,const std::string& type,std::size_t count)
{ const auto& e=all.at(id);require(e.type==type && e.args.size()==count,"unexpected "+type+" layout");return e; }
V cross(const V&a,const V&b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
double dot(const V&a,const V&b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
V unit(V v) { const auto n=std::sqrt(dot(v,v));require(n>0,"zero basis");for(auto& x:v)x/=n;return v; }
V vector(const Entities& all,unsigned id,const std::string& type)
{
    const auto& s=entity(all,id,type,1).args[0];require(s.front()=='(' && s.back()==')',"vector syntax");
    const auto a=split(s.substr(1,s.size()-2));require(a.size()==3,"vector dimension");return {real(a[0]),real(a[1]),real(a[2])};
}
struct Frame { V origin,x,y,z; };
Frame frame(const Entities& all,unsigned id)
{
    const auto& a=entity(all,id,"IFCAXIS2PLACEMENT3D",3).args;
    Frame f;f.origin=vector(all,entityRef(a[0]),"IFCCARTESIANPOINT");
    f.z=unit(vector(all,entityRef(a[1]),"IFCDIRECTION"));f.x=unit(vector(all,entityRef(a[2]),"IFCDIRECTION"));
    // IFC RefDirection defines the XZ half-plane; it need not be perpendicular
    // to Axis. IfcBuildAxes projects it to obtain the orthonormal X direction.
    f.y=unit(cross(f.z,f.x));f.x=unit(cross(f.y,f.z));return f;
}
void coordinateNear(double a,double b,double tolerance,const std::string& what)
{ require(std::isfinite(a) && std::isfinite(b) && std::abs(a-b)<=tolerance,what); }
Frame placement(const Entities& all,unsigned id)
{
    const auto& a=entity(all,id,"IFCLOCALPLACEMENT",2).args;auto result=frame(all,entityRef(a[1]));
    auto parent=a[0];std::set<unsigned> seen{id};
    while(parent!="$")
    {
        const auto p=entityRef(parent);require(seen.insert(p).second,"placement cycle");
        const auto& pa=entity(all,p,"IFCLOCALPLACEMENT",2).args;const auto f=frame(all,entityRef(pa[1]));
        for(std::size_t i=0;i<3;++i)
        {
            coordinateNear(f.origin[i],0,1e-12,"nonidentity parent origin");
            coordinateNear(f.x[i],i==0?1:0,1e-12,"nonidentity parent X");
            coordinateNear(f.y[i],i==1?1:0,1e-12,"nonidentity parent Y");
            coordinateNear(f.z[i],i==2?1:0,1e-12,"nonidentity parent Z");
        }
        parent=pa[0];
    }
    return result;
}
std::string guid(const std::string& s)
{
    static const std::string alphabet="0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
    require(s.size()==22,"GUID length");std::array<unsigned char,16> bytes{};
    for(const auto c:s)
    {
        auto carry=alphabet.find(c);require(carry!=std::string::npos,"GUID character");
        for(int i=15;i>=0;--i){carry+=unsigned(bytes[i])*64;bytes[i]=static_cast<unsigned char>(carry&255);carry>>=8;}
        require(carry==0,"GUID overflow");
    }
    std::ostringstream out;out<<"ID"<<std::uppercase<<std::hex<<std::setfill('0');
    for(std::size_t i=0;i<16;++i){if(i==4||i==6||i==8||i==10)out<<'-';out<<std::setw(2)<<unsigned(bytes[i]);}return out.str();
}
// Follow only geometry references, never unrelated styled items or placements.
void vertices(const Entities& all,unsigned id,std::set<unsigned>& out,std::set<unsigned>& active)
{
    require(active.size()<16 && active.insert(id).second,"geometry cycle/depth");const auto& e=all.at(id);
    if(e.type=="IFCCARTESIANPOINT")out.insert(id);
    else
    {
        std::size_t slot=0,count=1;
        if(e.type=="IFCPRODUCTDEFINITIONSHAPE"){slot=2;count=3;}
        else if(e.type=="IFCSHAPEREPRESENTATION")
        {slot=3;count=4;require(e.args.at(1)=="'Body'" && e.args.at(2)=="'Brep'","non-BREP representation");}
        else if(e.type=="IFCFACEOUTERBOUND" || e.type=="IFCFACEBOUND"){count=2;require(e.args.at(1)==".T.","reversed bound");}
        else require(e.type=="IFCFACETEDBREP" || e.type=="IFCCLOSEDSHELL" || e.type=="IFCFACE" || e.type=="IFCPOLYLOOP","unexpected geometry entity");
        require(e.args.size()==count,"geometry layout");
        for(const auto child:refs(e.args[slot]))vertices(all,child,out,active);
    }
    active.erase(id);
}
void validate(const std::filesystem::path& directory)
{
    tekla::db1::Model model;std::string error;
    require(tekla::db1::parseModelFile(directory.parent_path()/"btscm-pechelire"/"pechelirejulV1.db1",model,error),error);
    require(model.storageVersion=="7.30" && model.actualPartIds.size()==1240,"model scope");
    const auto all=read(directory/"export.ifc");
    std::size_t lengthUnits=0;
    for(const auto& item:all)if(item.second.type=="IFCSIUNIT")
    {
        const auto& a=item.second.args;require(a.size()==4,"unit layout");
        if(a[1]==".LENGTHUNIT.")
        { ++lengthUnits;require(a[2]==".MILLI." && a[3]==".METRE.","length units are not millimetres"); }
    }
    require(lengthUnits==1,"ambiguous length units");
    std::map<unsigned,std::map<std::string,std::string>> properties;
    for(const auto& item:all)
    {
        const auto& e=item.second;if(e.type!="IFCRELDEFINESBYPROPERTIES")continue;
        require(e.args.size()==6,"property relation layout");const auto& set=all.at(entityRef(e.args[5]));
        if(set.type=="IFCELEMENTQUANTITY")continue;
        require(set.type=="IFCPROPERTYSET" && set.args.size()==5,"property set layout");
        for(auto target:refs(e.args[4]))for(auto p:refs(set.args[4]))
        {
            const auto& a=entity(all,p,"IFCPROPERTYSINGLEVALUE",4).args;
            auto& values=properties[target];const auto name=string(a[0]);
            const auto inserted=values.emplace(name,a[2]);require(inserted.second||inserted.first->second==a[2],"conflicting property");
        }
    }
    std::set<unsigned> modified,exported;
    for(const auto& op:model.booleans)modified.insert(op.fatherPartId);
    for(const auto& op:model.cutPlanes)modified.insert(op.fatherPartId);
    for(const auto& op:model.fittings)modified.insert(op.fatherPartId);
    std::size_t boxes=0;std::map<std::string,std::size_t> kinds;
    const std::regex rectangular(R"(^([0-9]+(?:\.[0-9]+)?)\*([0-9]+(?:\.[0-9]+)?)$)");
    for(const auto& item:all)
    {
        const auto& e=item.second;if(e.type!="IFCBEAM" && e.type!="IFCPLATE" && e.type!="IFCCOLUMN")continue;
        require(e.args.size()==8,"product layout");const auto tag=string(e.args[7]);require(tag.substr(0,3)=="TS_","missing Tekla tag");
        const auto id=number(tag.substr(3));require(exported.insert(id).second,"duplicate product tag");++kinds[e.type];
        const auto& p=model.parts.at(id);require(p.internalType==2 && guid(string(e.args[0]))==p.guid,"identity "+std::to_string(id));
        const auto& props=properties.at(item.first);
        const auto unwrap=[&](const std::string& name,const std::string& type){
            const auto& v=props.at(name);require(v.substr(0,type.size()+1)==type+"(" && v.back()==')',"property value type");return v.substr(type.size()+1,v.size()-type.size()-2);};
        require(string(e.args[2])==p.name && string(unwrap("Nom","IFCLABEL"))==p.name,"name");
        require(string(unwrap("Mat\xc3\xa9" "riau","IFCLABEL"))==p.material,"material");
        require(string(unwrap("Classe","IFCLABEL"))==p.classNumber,"class");
        const auto f=placement(all,entityRef(e.args[5]));
        for(std::size_t i=0;i<3;++i)
        {
            const std::string axis(1,"XYZ"[i]);
            coordinateNear(p.start[i],real(unwrap("Origine"+axis,"IFCLENGTHMEASURE")),.002,"start "+std::to_string(id));
            coordinateNear(p.end[i],real(unwrap("Extr\xc3\xa9mit\xc3\xa9"+axis,"IFCLENGTHMEASURE")),.002,"end "+std::to_string(id));
            coordinateNear(p.origin[i],f.origin[i],.002,"origin "+std::to_string(id));
        }
        std::smatch dimensions;
        if(modified.count(id) || !p.contour.empty() || !std::regex_match(p.profile,dimensions,rectangular))continue;
        ++boxes;std::set<unsigned> points,active;vertices(all,entityRef(e.args[6]),points,active);require(points.size()==8,"box vertex count");
        const V basis[]{unit(p.axis),unit(p.secondary),unit(p.normal)};
        const double halfHeight=real(dimensions[1])/2,halfWidth=real(dimensions[2])/2;
        std::set<unsigned> corners;
        for(auto point:points)
        {
            const auto v=vector(all,point,"IFCCARTESIANPOINT");V w{};
            for(std::size_t i=0;i<3;++i)w[i]=f.origin[i]+v[0]*f.x[i]+v[1]*f.y[i]+v[2]*f.z[i]-p.origin[i];
            const V local{dot(w,basis[0]),dot(w,basis[1]),dot(w,basis[2])};unsigned corner=0;
            const double limits[3][2]={{0,p.length},{-halfHeight,halfHeight},{-halfWidth,halfWidth}};
            for(std::size_t i=0;i<3;++i)
            {
                const unsigned side=std::abs(local[i]-limits[i][1])<std::abs(local[i]-limits[i][0]);corner|=side<<i;
                coordinateNear(local[i],limits[i][side],.01,"box corner "+std::to_string(id));
            }
            require(corners.insert(corner).second,"duplicate box corner");
        }
        require(corners.size()==8,"missing box corner");
    }
    require(exported.size()==1215 && kinds["IFCBEAM"]==957 && kinds["IFCPLATE"]==91 && kinds["IFCCOLUMN"]==167 && boxes==536,"evidence coverage changed");
    std::set<unsigned> missing;for(auto id:model.actualPartIds)if(!exported.count(id))missing.insert(id);
    const std::set<unsigned> expectedMissing{505,91397,91404,91410,91452,91458,91516,96654,96674,96695,96715,96741,96760,96780,99460,107789,267581,267880,268649,268844,270270,270294,270460,270612,270616};
    require(missing==expectedMissing,"unexported scope changed");
    std::cout<<"products=1215 names=1215 materials=1215 classes=1215 endpoints=2430 origins=1215 boxes=536 corners=4288 unexported=25\n";
}
}
