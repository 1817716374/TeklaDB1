#include <tekla/Project.hpp>
#include <tekla/db1/Parser.hpp>
int main()
{
    tekla::Project project;
    std::string error;
    if (tekla::readProject("this-directory-must-not-exist", project, error)) return 1;
    if (error.empty()) return 2;
    tekla::db1::Model model;
    if (tekla::db1::parseModelFile("this-file-must-not-exist.db1", model, error)) return 3;
    if (error.empty()) return 4;
    tekla::Drawing drawing;
    tekla::DrawingStraightDimension dimension; dimension.projectedLength=3;
    drawing.straightDimensions.emplace(67,dimension);
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
    tekla::OptionSettingsFile settings;
    if(tekla::parseOptionSettingsFile("this-file-must-not-exist.ini",settings,error) || error.empty())return 9;
    settings.settings.push_back({1,"XS_TEST","TRUE",false});
    options.options[1]={1,"XS_TEST",tekla::StoredValueKind::Boolean,{true,false},0};
    const auto matches=tekla::matchOptionSettings(settings,options);
    if(matches.size()!=1 || !matches[0].valueParsed || !matches[0].matchingSlots[0] || matches[0].matchingSlots[1])return 10;
    return 0;
}
