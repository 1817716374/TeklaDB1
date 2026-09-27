int drawingExamValidation(const std::filesystem::path& path,const std::string& mode)
{
    using namespace tekla;
    const auto require=[](bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);};
    std::string error;
    if(mode=="drawing_exam_fields")
    {
        Drawing d;require(parseDrawing(path,d,error),error);
        require(d.sheets.size()==1 && d.subject && d.subject->modelGuid.empty() && d.viewsByRecordId.empty() && d.straightDimensions.empty(),"exam DG field scope changed");
        require(std::filesystem::u8path(d.storedFileName)==path.filename(),"stored drawing name differs from paired file");
        std::cout<<"width="<<d.sheets[0].width<<" height="<<d.sheets[0].height<<" type="<<d.subject->typeCode
            <<" subject_id="<<d.subject->modelObjectId<<'\n';return 0;
    }
    Project p;ProjectOptions options;options.strictCompanions=true;
    options.mainDatabase="API_Developer_Exam_01.db1";
    require(readProject(path,p,error,options),error);
    require(p.drawings.size()==5 && p.model.storageVersion=="9.08","exam DG project scope changed");
    std::size_t partial=0;
    for(const auto& file:p.files)if(file.role==FileRole::Drawing){require(file.level==ReadLevel::PartialSemantic,"exam DG overstates semantics");++partial;}
    require(partial==5,"exam DG inventory lost");
    if(mode=="drawing_exam_unpaired")
    {
        require(p.drawingSubjectAssociations.empty() && p.drawingModelAssociations.empty(),"unverified numeric DG identity was paired");
        std::cout<<"drawings=5 subject_links=0 reference_links=0\n";return 0;
    }
    require(p.drawingSubjectAssociations.size()==1 && p.drawingModelAssociations.size()==1,"exam DG project pairing changed");
    const auto& subject=p.drawingSubjectAssociations[0];const auto& reference=p.drawingModelAssociations[0];
    require(subject.drawing==reference.drawing && subject.modelObjectId==reference.modelObjectId && subject.modelGuid==reference.modelGuid,
        "exam DG subject and reference disagree");
    const auto& d=p.drawings.at(subject.drawing);
    require(d.raw.storageVersion=="9.08" && d.subject->modelObjectId==subject.modelObjectId && d.modelReferences[0].modelObjectId==subject.modelObjectId &&
        subject.drawingRecordId==d.subject->recordId && reference.drawingRecordId==d.modelReferences[0].recordId &&
        reference.drawingContextId==d.modelReferences[0].drawingContextId && p.model.identities.at(subject.modelObjectId).guid==subject.modelGuid,
        "exam DG association lost source identity or context");
    const auto& link=p.model.objectNumberingReferences.at(subject.modelObjectId);
    const auto& number=p.model.objectNumberingRecords.at(link.numberingRecordId);
    require(number.positionNumber.has_value(),"exam drawing subject has no decoded position number");
    std::cout<<"drawings=5 subject_links=1 reference_links=1 guid="<<subject.modelGuid<<" prefix="<<number.prefix
        <<" number="<<*number.positionNumber<<'\n';return 0;
}
