#pragma once
#include <tekla/IdRangeMapper.hpp>
#include <tekla/Project.hpp>
#include <algorithm>
#include <iostream>
#include <stdexcept>

inline int idRangeMapperValidation(const std::filesystem::path& path, const std::string& mode)
{
    if (mode == "w3_project_evidence")
    {
        tekla::Project project;
        std::string error;
        if (!tekla::readProject(path, project, error)) throw std::runtime_error(error);
        if (project.model.storageVersion != "8.95" || project.model.actualPartIds.size() != 21 ||
            project.model.operativePartIds.size() != 8 || project.model.assemblies.size() != 19 ||
            project.model.components.size() != 5 || project.model.properties.size() != 73)
            throw std::runtime_error("W3 main DB1 semantic projection differs from the pinned baseline: " +
                project.model.storageVersion + "/" + std::to_string(project.model.actualPartIds.size()) + "/" +
                std::to_string(project.model.operativePartIds.size()) + "/" +
                std::to_string(project.model.assemblies.size()) + "/" +
                std::to_string(project.model.components.size()) + "/" +
                std::to_string(project.model.properties.size()));
        if (!project.componentLibrary || project.componentLibrary->storageVersion != "8.95" ||
            project.componentLibrary->parts.size() != 38)
            throw std::runtime_error("W3 xslib did not parse as an 8.95 component library");
        std::size_t failed = 0;
        std::size_t mainDb2 = 0;
        std::size_t libraryDb2 = 0;
        std::size_t idrmFiles = 0;
        std::size_t optionsDbv = 0;
        std::size_t iniFiles = 0;
        for (const auto& file : project.files)
        {
            failed += file.level == tekla::ReadLevel::Failed;
            if (file.role == tekla::FileRole::Numbering && file.level == tekla::ReadLevel::PartialSemantic)
            {
                if (file.path.filename() == "oefening W3.db2") ++mainDb2;
                if (file.path.filename() == "xslib.db2") ++libraryDb2;
            }
            if (file.role == tekla::FileRole::IdRangeMapper && file.level == tekla::ReadLevel::PartialSemantic)
                ++idrmFiles;
            if (file.role == tekla::FileRole::Options && file.path.extension() == ".db" &&
                file.level == tekla::ReadLevel::PartialSemantic) ++optionsDbv;
            if (file.role == tekla::FileRole::Options && file.path.filename() == "options.ini" &&
                file.level == tekla::ReadLevel::PartialSemantic) ++iniFiles;
        }
        if (failed || mainDb2 != 1 || libraryDb2 != 1 || idrmFiles != 2 || optionsDbv != 2 || iniFiles != 1 ||
            project.numbering.size() != 2 || project.optionsDatabases.size() != 2 || !project.optionSettings)
            throw std::runtime_error("W3 companion semantic reads differ from the pinned project baseline");
        std::size_t db2Associations = 0;
        for (const auto& association : project.associations)
            if (association.target.extension() == ".db2") ++db2Associations;
        if (db2Associations != 2)
            throw std::runtime_error("W3 DB1/DB2 file pairing differs from the pinned project baseline");
        std::cout << "version=8.95 main_parts=" << project.model.actualPartIds.size()
                  << " xslib_parts=" << project.componentLibrary->parts.size()
                  << " db2_pairs=" << db2Associations << " dbv=" << project.optionsDatabases.size()
                  << " idrm=" << idrmFiles << " failed=0\n";
        return 0;
    }

    if (mode == "idrm_project")
    {
        tekla::Project project;
        std::string error;
        if (!tekla::readProject(path, project, error)) throw std::runtime_error(error);
        std::size_t discovered = 0;
        for (const auto& file : project.files)
            if (file.role == tekla::FileRole::IdRangeMapper)
            {
                ++discovered;
                if (file.level != tekla::ReadLevel::PartialSemantic)
                    throw std::runtime_error("IDRM static range and namespace semantics were not read");
            }
        if (discovered != 2 || project.idRangeMappers.size() != 2)
            throw std::runtime_error("project did not discover and read both IDRM files");
        const auto containsInPreamble=[&](const std::filesystem::path& database,const tekla::IdRangeMapperRecord& record) {
            tekla::db1::RawDatabase raw;
            if(!tekla::db1::parseRawDatabase(database,raw,error))throw std::runtime_error("raw DB1 read failed: "+error);
            return std::search(raw.preamble.begin(),raw.preamble.end(),record.namespaceGuidBytes.begin(),
                               record.namespaceGuidBytes.end())!=raw.preamble.end();
        };
        for (const auto& association : project.associations)
            if ((association.source.extension() == ".idrm" || association.target.extension() == ".idrm") &&
                association.reason.find("IDRM namespace GUID bytes match this DB1 allocation preamble") == std::string::npos)
                throw std::runtime_error("IDRM project association lacks allocation-preamble evidence");
        std::size_t namespaceLinks=0;
        for(const auto& entry:project.idRangeMappers)
        {
            const auto stem=entry.first.stem().string();
            const auto expected=stem=="xslib"?project.componentLibrary->databasePath:project.model.databasePath;
            const auto match=std::find_if(project.associations.begin(),project.associations.end(),[&](const auto& a){
                return a.source==entry.first && a.target==expected &&
                    a.reason.find("IDRM namespace GUID bytes match this DB1 allocation preamble")!=std::string::npos;});
            if(match==project.associations.end())throw std::runtime_error("IDRM namespace was not paired to its matching DB1 preamble");
            for(const auto& record:entry.second.records)
            {
                const bool mainHas=containsInPreamble(project.model.databasePath,record);
                const bool libraryHas=project.componentLibrary && containsInPreamble(project.componentLibrary->databasePath,record);
                if(stem=="xslib" ? (!libraryHas || mainHas) : (!mainHas || libraryHas))
                    throw std::runtime_error("IDRM range namespace did not independently match exactly one expected DB1 preamble");
            }
            ++namespaceLinks;
        }
        if(namespaceLinks!=2)throw std::runtime_error("expected two IDRM namespace-to-database links");
        std::cout << "idrm_files=" << discovered << " namespace_links=" << namespaceLinks
                  << " runtime_conversion=unimplemented\n";
        return 0;
    }

    if(mode=="idrm_snapshot_evidence")
    {
        const auto idrmPath=path.parent_path()/"auvent-idrm"/"db.idrm";
        const auto current=path/"AUVENT_CCF_BE_AMCR1_2019_final.db1";
        const auto backup=path/"AUVENT_CCF_BE_AMCR1_2019.db1.bak";
        tekla::IdRangeMapperFile mapper;
        std::string error;
        if(!tekla::parseIdRangeMapperFile(idrmPath,mapper,error))throw std::runtime_error(error);
        tekla::db1::RawDatabase currentRaw,backupRaw;
        if(!tekla::db1::parseRawDatabase(current,currentRaw,error))throw std::runtime_error(error);
        if(!tekla::db1::parseRawDatabase(backup,backupRaw,error))throw std::runtime_error(error);
        std::size_t currentMatches=0,backupMatches=0;
        for(const auto& record:mapper.records)
        {
            currentMatches+=std::search(currentRaw.preamble.begin(),currentRaw.preamble.end(),record.namespaceGuidBytes.begin(),
                                        record.namespaceGuidBytes.end())!=currentRaw.preamble.end();
            backupMatches+=std::search(backupRaw.preamble.begin(),backupRaw.preamble.end(),record.namespaceGuidBytes.begin(),
                                       record.namespaceGuidBytes.end())!=backupRaw.preamble.end();
        }
        if(mapper.records.size()!=2 || currentMatches!=2 || backupMatches!=0)
            throw std::runtime_error("AUVENT IDRM namespace scope differs between current and backup DB1 snapshots");
        std::cout<<"idrm_ranges="<<mapper.records.size()<<" current_preamble_matches="<<currentMatches
                 <<" backup_preamble_matches="<<backupMatches<<" runtime_conversion=unimplemented\n";
        return 0;
    }

    if (mode == "idrm_fields")
    {
        struct ExpectedRange
        {
            std::uint32_t staticFirst;
            const char* namespaceGuidHex;
            std::uint32_t namespaceFirst;
            std::uint32_t count;
        };
        struct ExpectedFile
        {
            std::uint32_t boundary;
            std::vector<ExpectedRange> ranges;
        };
        const auto bytesToHex = [](const std::array<std::uint8_t, 16>& bytes) {
            constexpr char digits[] = "0123456789abcdef";
            std::string result;
            result.reserve(32);
            for (const auto byte : bytes)
            {
                result.push_back(digits[byte >> 4]);
                result.push_back(digits[byte & 15]);
            }
            return result;
        };
        tekla::IdRangeMapperFile mapper;
        std::string error;
        if (!tekla::parseIdRangeMapperFile(path, mapper, error)) throw std::runtime_error(error);
        // Fixed values come from independent row walks of pinned native files.
        ExpectedFile expected;
        switch (mapper.headerValueAt13)
        {
        case 45172:
            expected = {45172, {{0, "83a31317d3dfe9419f747217efc91bfb", 6718, 45172}}};
            break;
        case 3127:
            expected = {3127, {{0, "69bbfb01c387c14b8b245eb83e3a9f91", 2036, 3127}}};
            break;
        case 509708643:
            expected = {509708643, {{1, "edc1569f7e1c264398fb9e82829cb587", 0, 509708639},
                                    {509708640, "edc1569f7e1c264398fb9e86829cb587", 1003, 3}}};
            break;
        case 5155779:
            expected = {5155779, {{1, "adf149561c022b43946b515a66cacbd1", 0, 5155775},
                                  {5155776, "adf149561c022b43946b515a66cacbd1", 5155775, 3}}};
            break;
        default:
            throw std::runtime_error("unrecognized pinned IDRM field-evidence sample");
        }
        if (mapper.staticIdBoundary != expected.boundary || mapper.records.size() != expected.ranges.size() ||
            !mapper.staticRangesMatchBoundary)
            throw std::runtime_error("IDRM range boundary or record count mismatch");
        for (std::size_t i = 0; i < expected.ranges.size(); ++i)
        {
            const auto& actual = mapper.records[i];
            const auto& want = expected.ranges[i];
            if (actual.staticFirstId != want.staticFirst || bytesToHex(actual.namespaceGuidBytes) != want.namespaceGuidHex ||
                actual.namespaceFirstId != want.namespaceFirst || actual.idCount != want.count)
                throw std::runtime_error("IDRM typed range fields differ from the pinned independent row walk");
        }
        std::cout << "ranges=" << mapper.records.size() << " static_boundary=" << mapper.staticIdBoundary
                  << " contiguous=1 runtime_conversion=unimplemented\n";
        return 0;
    }

    tekla::IdRangeMapperFile mapper;
    std::string error;
    if (!tekla::parseIdRangeMapperFile(path, mapper, error)) throw std::runtime_error(error);
    std::cout << "records=" << mapper.records.size() << " header_value=" << mapper.headerValueAt13
              << " marker=" << std::hex << mapper.marker << std::dec
              << " trailing=" << mapper.trailingBytes.size() << '\n';
    return 0;
}
