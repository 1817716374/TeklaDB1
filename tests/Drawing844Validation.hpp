int drawing844Validation(const std::filesystem::path& folder)
{
    const auto root=folder.parent_path();
    tekla::Drawing drawing;std::string error;
    if(!tekla::parseDrawing(folder/"DID54A03D14-3328-47D1-B298-9B37193B88A2.dg",drawing,error))throw std::runtime_error(error);
    if(drawing.raw.storageVersion!="8.44" || drawing.strings.size()!=41 || drawing.properties.size()!=5 || drawing.propertyLinks.size()!=5 || drawing.sheets.size()!=1)
        throw std::runtime_error("8.44 drawing text/property coverage changed");
    bool mark=false,rawGuid=false;
    for(const auto& entry:drawing.strings)
        if(entry.second.complete && entry.second.text.find("name=\"PART_POS\" value=\"S14\"")!=std::string::npos)mark=true;
    for(const auto& entry:drawing.properties)
        if(entry.second.name=="grProjectGuid" && entry.second.stringValue=="ID152F6ED8-4DAA-4276-B493-0AEA86E0BD65")rawGuid=true;
    std::ifstream stream(folder/"drawing_history.log",std::ios::binary);
    if(!stream)throw std::runtime_error("missing independent drawing history");
    std::ostringstream text;text<<stream.rdbuf();
    if(!mark || text.str().find("DRAWING DELETED: W   [S.14]")==std::string::npos)
        throw std::runtime_error("8.44 drawing mark/history evidence mismatch");
    tekla::GuidMappingFile mappings;
    if(!tekla::parseGuidMappingFile(root/"guid-mapping-auvent"/"guid.mapper",mappings,error))throw std::runtime_error(error);
    std::size_t matches=0;
    for(const auto& batch:mappings.batches)for(const auto& pair:batch.mappings)
        if(pair.sourceGuid=="152f6ed8-4daa-4276-b493-0aea86e0bd65" && pair.targetGuid=="2bd3f71c-e5e6-49ee-a2c5-7e870b087658")++matches;
    if(!rawGuid || matches!=1 || drawing.projectGuid!="152F6ED8-4DAA-4276-B493-0AEA86E0BD65" || drawing.storedFileName!="DID54A03D14-3328-47D1-B298-9B37193B88A2.dg")
        throw std::runtime_error("8.44 drawing historical project identity evidence mismatch");
    if(!drawing.modelReferences.empty() || drawing.subject || !drawing.viewsByContext.empty() || !drawing.straightDimensions.empty() || drawing.unhandledViewRecordIds.size()!=1 || drawing.unhandledDimensionRecordIds.size()!=13 || drawing.diagnostics.empty())
        throw std::runtime_error("8.44 unknown geometry/identity scope was silently changed");
    std::cout<<"version=8.44 strings=41 properties=5 links=5 mark_history=1 project_history=1 deferred_views=1 deferred_dimensions=13\n";
    return 0;
}
