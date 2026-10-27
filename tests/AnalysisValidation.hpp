// Included inside validate.cpp's private namespace.
int analysisProjectValidation(const std::filesystem::path& path,const std::string& mode)
{
    tekla::Project project;tekla::ProjectOptions options;std::string error;
    options.readRawCompanions=mode!="analysis_project_disabled";
    options.strictCompanions=mode=="analysis_project_strict";
    if(!tekla::readProject(path,project,error,options))throw std::runtime_error(error);
    std::size_t files=0,raw=0,failed=0,discovered=0,rows=0;
    for(const auto& f:project.files)if(f.role==tekla::FileRole::Analysis)
    {
        ++files;raw+=f.level==tekla::ReadLevel::Raw;failed+=f.level==tekla::ReadLevel::Failed;
        discovered+=f.level==tekla::ReadLevel::Discovered;
        if(f.level!=tekla::ReadLevel::Raw && f.level!=tekla::ReadLevel::Failed && f.level!=tekla::ReadLevel::Discovered)
            throw std::runtime_error("DB6 incorrectly marked semantic");
        for(const auto& pair:project.associations)
            if(pair.source==f.path || pair.target==f.path)throw std::runtime_error("unverified DB6 file association");
        const auto it=project.rawCompanions.find(f.path);
        if(f.level==tekla::ReadLevel::Raw)
        {
            if(it==project.rawCompanions.end() || it->second.kind!=tekla::db1::DatabaseKind::Analysis || !it->second.databaseGuid.empty())
                throw std::runtime_error("DB6 raw result missing or invented scope");
            for(const auto& t:it->second.tables)rows+=t.records.size();
        }
        else if(it!=project.rawCompanions.end())throw std::runtime_error("failed/disabled DB6 raw result retained");
    }
    std::cout<<"analysis_files="<<files<<" raw="<<raw<<" failed="<<failed<<" discovered="<<discovered<<" rows="<<rows<<" associations=0\n";
    return 0;
}
