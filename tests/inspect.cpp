#include <tekla/db1/Parser.hpp>

#include <filesystem>
#include <iostream>
#include <string>

namespace
{
int inspect(const std::filesystem::path& directory)
{
    tekla::db1::Model model;
    std::string error;
    if (!tekla::db1::parseModelDirectory(directory, model, error))
    {
        std::cerr << error << '\n';
        return 1;
    }

    std::cout << "storageVersion=" << model.storageVersion
              << " parts=" << model.actualPartIds.size()
              << " operativeParts=" << model.operativePartIds.size()
              << " profiles=" << model.profiles.size()
              << " bolts=" << model.boltGroups.size()
              << " welds=" << model.welds.size()
              << " reinforcements=" << model.reinforcements.size()
              << " reinforcementDefinitions=" << model.reinforcementDefinitions.size()
              << " assemblies=" << model.assemblies.size()
              << " components=" << model.components.size()
              << " properties=" << model.properties.size()
              << '\n';
    return 0;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
{
    return argc == 2 ? inspect(std::filesystem::path(argv[1])) : 2;
}
#else
int main(int argc, char** argv)
{
    return argc == 2 ? inspect(std::filesystem::u8path(argv[1])) : 2;
}
#endif
