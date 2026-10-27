#pragma once

// Evidence helper for the pinned IFC2X3 export, not a public IFC reader.
// A numerical volume is useful only after the exported boundary is checked.
namespace ifc730
{
inline V volumeSubtract(const V& a, const V& b)
{ return {a[0]-b[0],a[1]-b[1],a[2]-b[2]}; }
inline V volumeCross(const V& a, const V& b)
{ return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
inline double volumeDot(const V& a, const V& b)
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }

inline double closedShellVolume(const Entities& all, unsigned shellId)
{
    const auto& shell=entity(all,shellId,"IFCCLOSEDSHELL",1);
    const auto faces=refs(shell.args[0]);
    require(faces.size()>=4,"volume shell has too few faces");
    require(std::set<unsigned>(faces.begin(),faces.end()).size()==faces.size(),"duplicate volume face");
    std::map<std::pair<unsigned,unsigned>,unsigned> edges;
    V reference{};bool haveReference=false;double result=0;
    for(auto faceId:faces)
    {
        const auto bounds=refs(entity(all,faceId,"IFCFACE",1).args[0]);
        require(!bounds.empty(),"volume face has no bounds");
        require(std::set<unsigned>(bounds.begin(),bounds.end()).size()==bounds.size(),"duplicate volume bound");
        unsigned outerCount=0;std::vector<V> facePoints;V faceOrigin{},faceNormal{};
        for(auto boundId:bounds)
        {
            const auto& bound=all.at(boundId);
            require((bound.type=="IFCFACEOUTERBOUND" || bound.type=="IFCFACEBOUND") && bound.args.size()==2,"volume face bound type");
            require(bound.args[1]==".T." || bound.args[1]==".F.","volume bound orientation");
            auto ids=refs(entity(all,entityRef(bound.args[0]),"IFCPOLYLOOP",1).args[0]);
            require(ids.size()>=3 && std::set<unsigned>(ids.begin(),ids.end()).size()==ids.size(),"volume loop vertices");
            if(bound.args[1]==".F.")std::reverse(ids.begin(),ids.end());
            std::vector<V> points;for(auto id:ids)points.push_back(vector(all,id,"IFCCARTESIANPOINT"));
            if(!haveReference){reference=points.front();haveReference=true;}
            V normal{};
            for(std::size_t i=0;i<ids.size();++i)
            {
                const auto j=(i+1)%ids.size();
                const auto edge=volumeSubtract(points[j],points[i]);
                require(volumeDot(edge,edge)>1e-18,"zero length volume edge");
                require(++edges[{ids[i],ids[j]}]==1,"repeated directed volume edge");
            }
            for(std::size_t i=1;i+1<points.size();++i)
            {
                const auto n=volumeCross(volumeSubtract(points[i],points[0]),volumeSubtract(points[i+1],points[0]));
                for(unsigned k=0;k<3;++k)normal[k]+=n[k];
                result+=volumeDot(volumeSubtract(points[0],reference),
                    volumeCross(volumeSubtract(points[i],reference),volumeSubtract(points[i+1],reference)))/6;
            }
            require(volumeDot(normal,normal)>1e-18,"degenerate volume loop");
            if(bound.type=="IFCFACEOUTERBOUND")
            {++outerCount;faceOrigin=points[0];faceNormal=normal;}
            facePoints.insert(facePoints.end(),points.begin(),points.end());
        }
        require(outerCount==1,"volume face must have one outer bound");
        const auto magnitude=std::sqrt(volumeDot(faceNormal,faceNormal));
        // Fixed export evidence precision, separate from the .02 mm geometry
        // comparison. The 12 ELD cases have a measured maximum below .000089.
        for(const auto& point:facePoints)
            require(std::abs(volumeDot(volumeSubtract(point,faceOrigin),faceNormal))/magnitude<=1e-4,
                    "nonplanar volume face");
    }
    for(const auto& edge:edges)
    {
        const auto reverse=edges.find({edge.first.second,edge.first.first});
        require(reverse!=edges.end() && reverse->second==1,"open or inconsistently oriented volume shell");
    }
    require(std::isfinite(result) && result>0,"nonpositive oriented shell volume");
    return result;
}

inline double facetedVolume(const Entities& all,unsigned productShape)
{
    const auto shapes=refs(entity(all,productShape,"IFCPRODUCTDEFINITIONSHAPE",3).args[2]);
    require(!shapes.empty(),"no volume representations");double result=0;
    require(std::set<unsigned>(shapes.begin(),shapes.end()).size()==shapes.size(),"duplicate volume representation");
    std::set<unsigned> shells;
    for(auto shape:shapes)
    {
        const auto& representation=entity(all,shape,"IFCSHAPEREPRESENTATION",4);
        require(representation.args[2]=="'Brep'","volume representation must be Brep");
        const auto items=refs(representation.args[3]);require(!items.empty(),"empty volume representation");
        require(std::set<unsigned>(items.begin(),items.end()).size()==items.size(),"duplicate volume BREP");
        for(auto brep:items)
        {
            const auto shell=entityRef(entity(all,brep,"IFCFACETEDBREP",1).args[0]);
            require(shells.insert(shell).second,"repeated volume shell");
            result+=closedShellVolume(all,shell);
        }
    }
    return result;
}
}
