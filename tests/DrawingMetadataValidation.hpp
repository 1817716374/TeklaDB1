int drawingMetadataValidation(const std::filesystem::path& path,const std::string& mode)
{
    std::string error;
    if(mode=="drawing_metadata_fields" || mode=="drawing_metadata")
    {
        tekla::DrawingVersionMetadata metadata;
        if(!tekla::parseDrawingVersionMetadata(path,metadata,error))throw std::runtime_error(error);
        if(mode=="drawing_metadata")
        {
            Fingerprint hash;
            for(const auto* value:{&metadata.drawingGuid,&metadata.mainObjectGuid,&metadata.author,&metadata.mark,&metadata.name,
                &metadata.title1,&metadata.title2,&metadata.title3,&metadata.rawXml})hash.text(*value);
            for(const auto* value:{&metadata.width,&metadata.height}){hash.number(value->has_value());if(*value)hash.real(**value);}
            hash.number(metadata.drawingType.has_value());if(metadata.drawingType)hash.number(*metadata.drawingType);
            for(const auto& entry:metadata.storedIntegers){hash.text(entry.first);hash.number(static_cast<std::uint64_t>(entry.second));}
            std::cout<<"stored_integers="<<metadata.storedIntegers.size()<<" raw_bytes="<<metadata.rawXml.size()<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
            return 0;
        }
        const auto text=[](const char* key,const std::string& value){
            static const char digits[]="0123456789abcdef";
            std::cout<<key<<'=';for(unsigned char c:value)std::cout<<digits[c>>4]<<digits[c&15];std::cout<<'\n';};
        text("DrawingGuid",metadata.drawingGuid);text("MainObjectGuid",metadata.mainObjectGuid);
        text("Author",metadata.author);text("Mark",metadata.mark);text("Name",metadata.name);
        text("Title1",metadata.title1);text("Title2",metadata.title2);text("Title3",metadata.title3);text("raw",metadata.rawXml);
        if(metadata.width)std::cout<<"Width="<<std::setprecision(17)<<*metadata.width<<'\n';
        if(metadata.height)std::cout<<"Height="<<std::setprecision(17)<<*metadata.height<<'\n';
        if(metadata.drawingType)std::cout<<"DrawingType="<<*metadata.drawingType<<'\n';
        for(const auto& n:metadata.storedIntegers)std::cout<<n.first<<'='<<n.second<<'\n';
        return 0;
    }
    tekla::Project project;tekla::ProjectOptions options;
    options.mainDatabase="API_Developer_Exam_01.db1";
    options.strictCompanions=mode!="drawing_metadata_project_lenient";
    options.readDrawingMetadata=mode!="drawing_metadata_project_disabled";
    options.readDrawings=mode!="drawing_metadata_project_no_drawings";
    if(!tekla::readProject(path,project,error,options))throw std::runtime_error(error);
    std::size_t objects=0,failed=0,discovered=0;
    for(const auto& pair:project.drawingMetadataAssociations)
    {
        const auto& metadata=project.drawingMetadata.at(pair.metadata);const auto& drawing=project.drawings.at(pair.drawing);
        if(pair.modelObjectId)
        {
            ++objects;
            if(project.model.identities.at(*pair.modelObjectId).guid!=metadata.mainObjectGuid || drawing.subject->modelObjectId!=*pair.modelObjectId)
                throw std::runtime_error("metadata association lost identity provenance");
        }
    }
    for(const auto& file:project.files)if(file.role==tekla::FileRole::DrawingMetadata)
    {
        failed+=file.level==tekla::ReadLevel::Failed;discovered+=file.level==tekla::ReadLevel::Discovered;
        if(file.level==tekla::ReadLevel::Semantic)throw std::runtime_error("metadata overstates lifecycle semantics");
    }
    std::cout<<"metadata="<<project.drawingMetadata.size()<<" pairs="<<project.drawingMetadataAssociations.size()<<" objects="<<objects
        <<" failed="<<failed<<" discovered="<<discovered<<" drawings="<<project.drawings.size()<<'\n';
    return 0;
}
