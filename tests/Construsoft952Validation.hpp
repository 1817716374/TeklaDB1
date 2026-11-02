int construsoft952Evidence(const std::filesystem::path& root)
{
    tekla::Project project;
    tekla::ProjectOptions options;
    options.strictCompanions=true;
    std::string error;
    if(!tekla::readProject(root,project,error,options))throw std::runtime_error(error);
    if(project.model.storageVersion!="9.52" || !project.componentLibrary ||
       project.componentLibrary->storageVersion!="9.52" || project.numbering.size()!=2)
        throw std::runtime_error("Construsoft 9.52 project components missing");

    std::map<std::filesystem::path,const tekla::db1::Model*> models;
    models.emplace(project.model.databasePath,&project.model);
    models.emplace(project.componentLibrary->databasePath,&*project.componentLibrary);
    std::size_t expectedLinks=0, mainReferences=0, libraryReferences=0;
    std::size_t cutparts=0, plates=0;
    std::set<std::pair<std::filesystem::path,std::uint32_t>> seenObjects;
    for(const auto& item:models)
    {
        const auto& model=*item.second;
        if(item.first==project.componentLibrary->databasePath)
        {
            for(const auto id:model.componentPartIds)
            {
                const auto& part=model.parts.at(id);
                if(part.internalType!=39 || part.guid.empty() ||
                   std::none_of(model.components.begin(),model.components.end(),[&](const auto& component){return component.id==part.ownerId;}))
                    throw std::runtime_error("pinned Construsoft 9.52 component-part identity/owner mismatch");
                if(part.name=="CUTPART")++cutparts;
                else if(part.name=="PLATE")++plates;
                else throw std::runtime_error("unexpected pinned Construsoft 9.52 component-part name");
            }
            if(!model.componentPartIds.empty() && cutparts+plates!=model.componentPartIds.size())
                throw std::runtime_error("pinned Construsoft 9.52 component-part names changed");
        }
        const auto target=root/(model.databasePath.stem().string()+".db2");
        const auto database=project.numbering.find(target);
        if(database==project.numbering.end() || database->second.raw.storageVersion!="9.52" ||
           model.databaseGuid.empty() || database->second.raw.databaseGuid!=model.databaseGuid)
            throw std::runtime_error("Construsoft DB1/DB2 scope mismatch");
        for(const auto& entry:model.objectNumberingReferences)
        {
            if(item.first==project.model.databasePath)++mainReferences;else ++libraryReferences;
            const auto record=model.objectNumberingRecords.find(entry.second.numberingRecordId);
            if(record==model.objectNumberingRecords.end() ||
               (record->second.kind!=tekla::db1::ObjectNumberingKind::Part &&
                record->second.kind!=tekla::db1::ObjectNumberingKind::Assembly))continue;
            std::vector<std::size_t> matches;
            for(std::size_t i=0;i<database->second.series.size();++i)
            {
                const auto& series=database->second.series[i];
                if(series.prefix==record->second.prefix && series.startNumber==record->second.startNumber)
                    matches.push_back(i);
            }
            const auto association=std::find_if(project.objectNumberingSeriesAssociations.begin(),
                project.objectNumberingSeriesAssociations.end(),[&](const auto& value) {
                    return value.database==model.databasePath && value.objectId==entry.first;
                });
            if(matches.size()!=1)
            {
                if(association!=project.objectNumberingSeriesAssociations.end())
                    throw std::runtime_error("ambiguous/missing Construsoft series was associated");
                continue;
            }
            ++expectedLinks;
            if(association==project.objectNumberingSeriesAssociations.end() ||
               association->numberingDatabase!=target || association->numberingRecordId!=record->second.id ||
               association->seriesIndex!=matches.front() ||
               association->pairingEvidence!=tekla::NumberingPairEvidence::DatabaseGuid ||
               !model.identities.count(entry.first) ||
               !seenObjects.emplace(model.databasePath,entry.first).second)
                throw std::runtime_error("Construsoft object-to-series association mismatch");
        }
    }
    const auto mainDb2=root/"Defining drawing views steel.db2";
    const auto libraryDb2=root/"xslib.db2";
    const auto mainNumbering=project.numbering.find(mainDb2);
    const auto libraryNumbering=project.numbering.find(libraryDb2);
    if(expectedLinks!=24 || project.objectNumberingSeriesAssociations.size()!=expectedLinks ||
       project.associations.size()!=3 ||
       mainNumbering==project.numbering.end() || mainNumbering->second.series.size()!=13 ||
       libraryNumbering==project.numbering.end() || !libraryNumbering->second.series.empty())
        throw std::runtime_error("Construsoft 9.52 numbering association counts changed: expected="+
            std::to_string(expectedLinks)+" actual="+std::to_string(project.objectNumberingSeriesAssociations.size())+
            " db2s="+std::to_string(project.numbering.size())+" main_series="+
            std::to_string(mainNumbering==project.numbering.end()?999:mainNumbering->second.series.size())+
            " library_series="+std::to_string(libraryNumbering==project.numbering.end()?999:libraryNumbering->second.series.size()));
    std::cout<<"main_references="<<mainReferences<<" library_references="<<libraryReferences
             <<" main_series=13 file_associations="<<project.associations.size()
             <<" object_series_links="<<expectedLinks<<" component_parts="<<cutparts+plates
             <<" names=CUTPART:"<<cutparts<<",PLATE:"<<plates<<'\n';
    return 0;
}
