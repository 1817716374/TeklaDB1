// This fixed model/drawing collection is an explicit evidence pair, not a
// general rule that unscoped numeric IDs uniquely identify a project.
void validateDrawing730Evidence(const std::filesystem::path& directory)
{
    tekla::Project project;tekla::ProjectOptions options;options.strictCompanions=true;
    options.readComponentLibrary=false;options.readRawCompanions=false;
    std::string error;
    if(!tekla::readProject(directory,project,error,options))throw std::runtime_error(error);
    if(project.drawings.size()!=35 || !project.drawingModelAssociations.empty() || !project.drawingSubjectAssociations.empty())
        throw std::runtime_error("7.30 project drawing scope mismatch");
    std::size_t views=0,sheets=0,properties=0,general=0,parts=0;std::set<std::uint32_t> subjects,axes;
    const auto& part=project.model.parts.at(311724);
    for(const auto& item:project.drawings)
    {
        const auto& drawing=item.second;
        if(drawing.raw.storageVersion!="7.30" || !drawing.projectGuid.empty() || !drawing.modelReferences.empty() || !drawing.subject)
            throw std::runtime_error("7.30 drawing identity/reference coverage mismatch");
        if(std::none_of(drawing.diagnostics.begin(),drawing.diagnostics.end(),[](const auto& d){return d.find("7.30 drawing model references remain raw")!=std::string::npos;}))
            throw std::runtime_error("7.30 reference gap not diagnosed");
        if(std::any_of(drawing.strings.begin(),drawing.strings.end(),[](const auto& s){return !s.second.complete;}))
            throw std::runtime_error("public 7.30 text became incomplete");
        views+=drawing.viewsByRecordId.size();sheets+=drawing.sheets.size();properties+=drawing.properties.size();
        if(drawing.subject->kind==tekla::DrawingSubjectKind::GeneralArrangement)
        {
            ++general;if(drawing.subject->modelObjectId)throw std::runtime_error("general drawing fabricated model subject");
        }
        else if(drawing.subject->kind==tekla::DrawingSubjectKind::SinglePart)
        {
            ++parts;subjects.insert(drawing.subject->modelObjectId);
            if(drawing.subject->modelObjectId!=part.id || part.internalType!=2 || part.profile!="L90*10")throw std::runtime_error("drawing/DB1 subject differs");
            for(const auto& view:drawing.viewsByRecordId)
            {
                const auto& x=view.second.viewCoordinates.axisX;double dot=0,norm=0;
                for(std::size_t i=0;i<3;++i){dot+=x[i]*part.axis[i];norm+=part.axis[i]*part.axis[i];}
                if(norm<=0 || std::abs(dot/std::sqrt(norm)-1)>1e-6)throw std::runtime_error("drawing X axis disagrees with DB1 part axis");
                axes.insert(view.first);
            }
            if(std::none_of(drawing.strings.begin(),drawing.strings.end(),[](const auto& s){return s.second.text.find("name=\"PART_POS\" value=\"153\"")!=std::string::npos;}))
                throw std::runtime_error("stored historical drawing mark lost");
        }
        else throw std::runtime_error("unexpected public drawing subject kind");
    }
    std::size_t partial=0;
    for(const auto& file:project.files)if(file.role==tekla::FileRole::Drawing)
    {
        if(file.level!=tekla::ReadLevel::PartialSemantic)throw std::runtime_error("7.30 DG support level overstated");++partial;
    }
    tekla::db1::RawDatabase model;
    if(!tekla::db1::parseRawDatabase(project.model.databasePath,model,error))throw std::runtime_error(error);
    const auto table=std::find_if(model.tables.begin(),model.tables.end(),[](const auto& t){return t.ordinal==27;});
    if(table==model.tables.end() || table->payloadSize!=228 || table->records.size()!=13)throw std::runtime_error("drawing catalog evidence missing");
    for(const auto& row:table->records)
    {
        const auto begin=row.payload.begin()+92,end=std::find(begin,begin+16,0);
        const std::string name(begin,end);
        const auto found=project.drawings.find(std::filesystem::absolute(directory/name).lexically_normal());
        if(found==project.drawings.end())throw std::runtime_error("DB1 drawing catalog filename unresolved");
        const auto label=row.payload[8];
        if((label=='W' && found->second.subject->kind!=tekla::DrawingSubjectKind::SinglePart) ||
           (label=='G' && found->second.subject->kind!=tekla::DrawingSubjectKind::GeneralArrangement) || (label!='W' && label!='G'))
            throw std::runtime_error("drawing subject differs from catalog type label");
    }
    if(views!=91 || sheets!=35 || properties!=104 || general!=33 || parts!=2 || subjects.size()!=1 || axes.size()!=2 || partial!=35)
        throw std::runtime_error("7.30 drawing evidence counts changed");
    std::cout<<"drawings=35 views=91 catalog_files=13 unique_subjects=1 unique_part_axes=2 automatic_subject_links=0\n";
}
