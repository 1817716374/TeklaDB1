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
    if (tekla::parseDrawing("this-file-must-not-exist.dg",drawing,error) || error.empty()) return 5;
    tekla::NumberingDatabase numbering;
    if (tekla::parseNumberingDatabase("this-file-must-not-exist.db2",numbering,error) || error.empty()) return 6;
    return 0;
}
