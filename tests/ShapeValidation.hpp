int shapeValidation(const std::string& mode,const std::filesystem::path& path)
{
    std::string error;Fingerprint hash;
    if(mode=="shape_definition")
    {
        tekla::db1::ShapeDefinition d;
        if(!tekla::db1::parseShapeDefinition(path,d,error))throw std::runtime_error(error);
        hash.text(d.name);hash.text(d.guid);hash.text(d.brepStorageId);hash.number(d.hasGeometryType);hash.number(d.geometryType);
        hash.number(d.isSolid.has_value());hash.number(d.isSolid.value_or(false));hash.text(d.fingerprint);
        for(auto v:d.minimum)hash.real(v);for(auto v:d.maximum)hash.real(v);
        std::cout<<"raw_bytes="<<d.rawXml.size()<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';return 0;
    }
    if(mode=="shape_geometry")
    {
        tekla::db1::ShapeGeometry g;
        if(!tekla::db1::parseShapeGeometry(path,g,error))throw std::runtime_error(error);
        hash.text(g.storageId);hash.number(g.points.size());for(const auto& p:g.points)for(auto v:p)hash.real(v);
        hash.number(g.faces.size());
        for(const auto& f:g.faces)
        {
            hash.number(f.outerLoop.size());for(auto v:f.outerLoop)hash.number(v);
            hash.number(f.innerLoops.size());for(const auto& l:f.innerLoops){hash.number(l.size());for(auto v:l)hash.number(v);}
        }
        hash.number(g.edges.size());
        for(const auto& e:g.edges){hash.number(e.firstVertex);hash.number(e.secondVertex);hash.number(static_cast<unsigned>(e.type));hash.text(e.rawType);}
        std::cout<<"points="<<g.points.size()<<" faces="<<g.faces.size()<<" edges="<<g.edges.size()<<" raw_bytes="<<g.rawXml.size()<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';return 0;
    }
    tekla::db1::ShapeCatalog catalog;
    if(!tekla::db1::parseShapeCatalog(path,catalog,error))throw std::runtime_error(error);
    if(catalog.definitionsByGuid.size()!=1 || catalog.geometriesByStorageId.size()!=1 || !catalog.diagnostics.empty())throw std::runtime_error("real XML shape pair not recovered");
    const auto& d=catalog.definitionsByGuid.at("712cacb7-119f-4da3-8738-9c5dca170300");
    const auto& g=catalog.geometriesByStorageId.at(d.brepStorageId);
    if(d.hasGeometryType || !d.isSolid || *d.isSolid || g.points.size()!=60 || g.faces.size()!=58 || g.edges.size()!=117)throw std::runtime_error("real non-solid shape declaration/counts mismatch");
    for(std::size_t axis=0;axis<3;++axis)
    {
        auto lo=g.points.front()[axis],hi=lo;
        for(const auto& p:g.points){lo=std::min(lo,p[axis]);hi=std::max(hi,p[axis]);}
        if(std::abs(lo-d.minimum[axis])>1e-9 || std::abs(hi-d.maximum[axis])>1e-9)throw std::runtime_error("independent definition/geometry bounds disagree");
    }
    std::cout<<"definitions=1 geometries=1 linked=1 points=60 faces=58 edges=117 bounds=6 solid=0\n";return 0;
}
