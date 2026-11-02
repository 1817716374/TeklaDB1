int shapeValidation(const std::string& mode,const std::filesystem::path& path)
{
    std::string error;Fingerprint hash;
    if(mode=="rebar_shape_catalog")
    {
        tekla::db1::RebarShapeCatalog catalog;
        if(!tekla::db1::parseRebarShapeCatalog(path,catalog,error))throw std::runtime_error(error);
        std::size_t categorized=0;
        for(const auto& category:catalog.categories)
        {
            hash.text(category.name);hash.number(category.shapeIds.size());
            categorized+=category.shapeIds.size();
            for(const auto& id:category.shapeIds)hash.text(id);
        }
        for(const auto& id:catalog.uncategorizedShapeIds)hash.text(id);
        if(catalog.categories.size()!=5 || categorized!=14 || catalog.uncategorizedShapeIds!=std::vector<std::string>({"5_1","36_3"}) ||
           catalog.rawXml.size()!=736)throw std::runtime_error("official reinforcement shape catalog content changed");
        std::cout<<"categories="<<catalog.categories.size()<<" categorized_shapes="<<categorized
                 <<" uncategorized_shapes="<<catalog.uncategorizedShapeIds.size()<<" raw_bytes="<<catalog.rawXml.size()
                 <<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';return 0;
    }
    if(mode=="rebar_project_evidence")
    {
        tekla::Project project;
        if(!tekla::readProject(path,project,error))throw std::runtime_error(error);
        if(project.model.storageVersion!="9.08" || !project.rebarShapeCatalog ||
           project.rebarShapeCatalog->categories.size()!=5 || project.rebarShapeCatalog->uncategorizedShapeIds.size()!=2)
            throw std::runtime_error("official project rebar shape catalog was not read");
        std::size_t categorized=0;
        for(const auto& category:project.rebarShapeCatalog->categories)categorized+=category.shapeIds.size();
        if(categorized!=14)throw std::runtime_error("official project categorized shape count changed");
        const auto catalogPath=std::filesystem::absolute(path/"attributes/RebarShapeCatalog.rscx").lexically_normal();
        const auto file=std::find_if(project.files.begin(),project.files.end(),[&](const auto& entry){return entry.path==catalogPath;});
        const auto association=std::find_if(project.associations.begin(),project.associations.end(),[&](const auto& entry){return entry.target==catalogPath;});
        if(file==project.files.end() || file->role!=tekla::FileRole::Catalog || file->level!=tekla::ReadLevel::Semantic ||
           association==project.associations.end() || association->source!=project.model.databasePath)
            throw std::runtime_error("official rebar catalog was not joined to its project by resource provenance");
        std::cout<<"version=9.08 rebar_categories=5 categorized_shapes=14 uncategorized_shapes=2 resource=semantic model_file_association=1 db1_shape_links=unverified\n";
        return 0;
    }
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
    if(mode=="shape_project_evidence")
    {
        tekla::Project project;tekla::ProjectOptions options;options.strictCompanions=true;
        if(!tekla::readProject(path,project,error,options))throw std::runtime_error(error);
        const auto source=path/"Shapes/712cacb7-119f-4da3-8738-9c5dca170300.xml";
        const auto geometryStem=path/"ShapeGeometries/a1d452b2-d351-4a0a-9376-d4e4cfad751e";
        const auto xmlTarget=geometryStem.string()+".xml";
        const auto tezTarget=geometryStem.string()+".tez";
        const auto target=std::filesystem::exists(xmlTarget)?std::filesystem::path(xmlTarget):std::filesystem::path(tezTarget);
        if(project.associations.size()!=1 || project.associations.front().source!=source ||
           project.associations.front().target!=target)throw std::runtime_error("real shape project association differs from XML storage ID and file paths");
        std::size_t directories=0;
        for(const auto& file:project.files)if(file.path==source.parent_path() || file.path==target.parent_path())
        {
            if(file.role!=tekla::FileRole::Catalog || file.level!=tekla::ReadLevel::Semantic)throw std::runtime_error("valid shape directories not semantic catalogs");
            ++directories;
        }
        if(directories!=2)throw std::runtime_error("shape directory inventory missing");
        catalog=std::move(project.shapes);
    }
    else if(!tekla::db1::parseShapeCatalog(path,catalog,error))throw std::runtime_error(error);
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
    std::cout<<"definitions=1 geometries=1 linked=1 points=60 faces=58 edges=117 bounds=6 solid=0";
    if(mode=="shape_project_evidence")std::cout<<" project_links=1";
    std::cout<<'\n';return 0;
}
