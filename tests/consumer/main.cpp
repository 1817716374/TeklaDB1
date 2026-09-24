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
    return error.empty() ? 4 : 0;
}
