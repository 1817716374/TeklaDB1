#include <tekla/Project.hpp>
#include <tekla/DrawingMetadata.hpp>
#include <tekla/db1/Parser.hpp>
int main(int argc,char** argv)
{
    if(argc>2)return 20;
    tekla::DrawingMetadataExtension extension;
    extension.name="AlwaysNecessary";
    extension.scalarText="0";
    if(extension.name!="AlwaysNecessary" || extension.scalarText!="0" || extension.hasAttributes)return 15;
    tekla::AttributeChoice storedChoice;
    storedChoice.allocationTag=6;
    if(storedChoice.allocationTag!=6)return 24;
    tekla::db1::ProfileContourPoint sketchPoint;
    sketchPoint.chamferDz1=1.25;
    sketchPoint.chamferDz2=2.5;
    sketchPoint.chamferFlags=7;
    if(sketchPoint.chamferDz1!=1.25 || sketchPoint.chamferDz2!=2.5 || sketchPoint.chamferFlags!=7)return 16;
    tekla::Project project;
    tekla::db1::RebarShapeCatalog rebarCatalog;
    rebarCatalog.categories.push_back({"Straight", {"1"}});
    project.rebarShapeCatalog = rebarCatalog;
    if(!project.rebarShapeCatalog || project.rebarShapeCatalog->categories.front().shapeIds.front()!="1")return 18;
    tekla::ReinforcementNumberingSeriesAssociation reinforcementNumbering;
    reinforcementNumbering.db1RawField = 4;
    reinforcementNumbering.db2RawField = 4;
    project.reinforcementNumberingSeriesAssociations.push_back(reinforcementNumbering);
    if(project.reinforcementNumberingSeriesAssociations.size()!=1 ||
       project.reinforcementNumberingSeriesAssociations.front().db1RawField != 4 ||
       project.reinforcementNumberingSeriesAssociations.front().db2RawField != 4)return 23;
    std::string rebarError;
    if(tekla::db1::parseRebarShapeCatalog("this-file-must-not-exist.rscx",rebarCatalog,rebarError) || rebarError.empty())return 19;
    if(argc==2)
    {
        if(!tekla::db1::parseRebarShapeCatalog(argv[1],rebarCatalog,rebarError) || rebarCatalog.categories.size()!=5 ||
           rebarCatalog.uncategorizedShapeIds.size()!=2)return 21;
        std::size_t shapeCount=0;for(const auto& category:rebarCatalog.categories)shapeCount+=category.shapeIds.size();
        if(shapeCount!=14)return 22;
    }
    tekla::PartMaterialAssociation materialAssociation;
    materialAssociation.objectId=17;
    materialAssociation.materialName="S355";
    materialAssociation.materialIndex=2;
    project.partMaterialAssociations.push_back(materialAssociation);
    if(project.partMaterialAssociations.size()!=1 || project.partMaterialAssociations.front().objectId!=17 ||
       project.partMaterialAssociations.front().materialName!="S355" || project.partMaterialAssociations.front().materialIndex!=2)return 17;
    tekla::ProjectOptions projectOptions;
    projectOptions.readProfileLibraries = true;
    projectOptions.trustLegacyDrawingObjectIds = false;
    tekla::DrawingModelAssociation drawingAssociation;
    if(drawingAssociation.pairingEvidence!=tekla::DrawingPairEvidence::ProjectGuidAndObjectIdentity)return 12;
    drawingAssociation.pairingEvidence=tekla::DrawingPairEvidence::DirectSubjectGuidIdentity;
    if(drawingAssociation.pairingEvidence!=tekla::DrawingPairEvidence::DirectSubjectGuidIdentity)return 25;
    tekla::db1::ClbDocument clb;
    clb.libraryId = "consumer";
    tekla::db1::ClbSection section;
    section.name = "section";
    section.baseAttributes.push_back({"width"});
    section.expressions.push_back({"offset"});
    clb.sections.push_back(section);
    clb.includes.push_back({"section.clb", 2});
    tekla::ProfileRuleSectionAssociation ruleLink;
    ruleLink.ruleIndex = 0;
    tekla::ClbIncludeAssociation includeLink;
    includeLink.includeIndex = 0;
    std::string error;
    if (tekla::readProject("this-directory-must-not-exist", project, error, projectOptions)) return 1;
    if (error.empty()) return 2;
    tekla::db1::Model model;
    if (tekla::db1::parseModelFile("this-file-must-not-exist.db1", model, error)) return 3;
    if (error.empty()) return 4;
    model.componentPartIds.push_back(39);
    if(model.componentPartIds.size()!=1 || model.componentPartIds.front()!=39)return 14;
    tekla::Drawing drawing;
    tekla::DrawingStraightDimension dimension; dimension.projectedLength=3;
    drawing.straightDimensions.emplace(67,dimension);
    tekla::DrawingLineSegment line; line.recordId=68; line.startPoint={1.0,2.0}; line.endPoint={3.0,4.0};
    drawing.lineSegments.push_back(line);
    if(drawing.lineSegments.size()!=1 || drawing.lineSegments.front().endPoint[1]!=4.0)return 15;
    tekla::DrawingType281LineReference lineReference; lineReference.referenceRecordId=69; lineReference.lineRecordId=68;
    drawing.type281LineReferences.push_back(lineReference);
    if(drawing.type281LineReferences.size()!=1 || drawing.type281LineReferences.front().lineRecordId!=68)return 16;
    drawing.straightDimensionSets.emplace(66,tekla::DrawingStraightDimensionSet{66,45,{67}});
    drawing.unhandledDimensionRecordIds.push_back(99);
    if (tekla::parseDrawing("this-file-must-not-exist.dg",drawing,error) || error.empty()) return 5;
    if(!drawing.straightDimensions.empty() || !drawing.straightDimensionSets.empty() || !drawing.unhandledDimensionRecordIds.empty())return 11;
    tekla::NumberingDatabase numbering;
    if (tekla::parseNumberingDatabase("this-file-must-not-exist.db2",numbering,error) || error.empty()) return 6;
    tekla::EnvironmentDatabase environment;
    if (tekla::parseEnvironmentDatabase("this-file-must-not-exist.db",environment,error) || error.empty()) return 7;
    tekla::OptionsDatabase options;
    if (tekla::parseOptionsDatabase("this-file-must-not-exist.db",options,error) || error.empty()) return 8;
    tekla::IdRangeMapperFile idrm;
    if (tekla::parseIdRangeMapperFile("this-file-must-not-exist.idrm", idrm, error) || error.empty()) return 13;
    tekla::OptionSettingsFile settings;
    if(tekla::parseOptionSettingsFile("this-file-must-not-exist.ini",settings,error) || error.empty())return 9;
    settings.settings.push_back({1,"XS_TEST","TRUE",false});
    options.options[1]={1,"XS_TEST",tekla::StoredValueKind::Boolean,{true,false},0};
    const auto matches=tekla::matchOptionSettings(settings,options);
    if(matches.size()!=1 || !matches[0].valueParsed || !matches[0].matchingSlots[0] || matches[0].matchingSlots[1])return 10;
    return 0;
}
