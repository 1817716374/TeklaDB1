// Included inside validate.cpp's private namespace.
int analysisSettingsValidation(const std::filesystem::path& path,const std::string& mode)
{
    std::string error;
    if(mode.rfind("analysis_settings_project",0)==0)
    {
        tekla::Project p;tekla::ProjectOptions options;
        options.readAnalysisSettings=mode!="analysis_settings_project_disabled";
        options.readRawCompanions=mode!="analysis_settings_project_raw_disabled";
        options.strictCompanions=mode=="analysis_settings_project_strict";
        if(!tekla::readProject(path,p,error,options))throw std::runtime_error(error);
        std::size_t files=0,parsed=0,failed=0,discovered=0,fields=0,designs=0;
        for(const auto& f:p.files)if(f.role==tekla::FileRole::AnalysisSettings)
        {
            ++files;parsed+=f.level==tekla::ReadLevel::PartialSemantic;failed+=f.level==tekla::ReadLevel::Failed;discovered+=f.level==tekla::ReadLevel::Discovered;
            if(f.level!=tekla::ReadLevel::PartialSemantic && f.level!=tekla::ReadLevel::Failed && f.level!=tekla::ReadLevel::Discovered)throw std::runtime_error("incorrect analysis preset level");
            for(const auto& a:p.associations)if(a.source==f.path || a.target==f.path)throw std::runtime_error("unverified preset association");
            const auto it=p.analysisSettings.find(f.path);
            if(f.level==tekla::ReadLevel::PartialSemantic)
            {if(it==p.analysisSettings.end())throw std::runtime_error("missing preset result");fields+=it->second.settings.size();designs+=it->second.designSettings.size();}
            else if(it!=p.analysisSettings.end())throw std::runtime_error("failed/disabled preset retained");
        }
        std::cout<<"files="<<files<<" parsed="<<parsed<<" failed="<<failed<<" discovered="<<discovered<<" fields="<<fields<<" designs="<<designs<<" associations=0\n";return 0;
    }
    tekla::AnalysisModelSettings s;if(!tekla::parseAnalysisModelSettings(path,s,error))throw std::runtime_error(error);
    const bool verbose=mode=="analysis_settings_fields";Fingerprint hash;
    const auto hex=[](const std::string& text){std::string out;for(unsigned char c:text){out.push_back("0123456789abcdef"[c>>4]);out.push_back("0123456789abcdef"[c&15]);}return out;};
    const auto value=[&](const tekla::AnalysisSettingValue& v)
    {
        hash.number(v.index());
        std::visit([&](const auto& x){using T=std::decay_t<decltype(x)>;
            if constexpr(std::is_same_v<T,std::string>){hash.text(x);if(verbose)std::cout<<"s\t"<<hex(x);}
            else if constexpr(std::is_same_v<T,double>){hash.real(x);if(verbose){std::uint64_t bits;std::memcpy(&bits,&x,8);std::cout<<"d\t"<<std::hex<<bits<<std::dec;}}
            else {hash.number(static_cast<std::uint64_t>(x));if(verbose)std::cout<<"i\t"<<x;}
        },v);
    };
    hash.number(s.codePage);
    for(const auto& f:s.settings)
    {
        hash.text(f.name);hash.text(f.rawValue);hash.number(f.lineNumber);
        if(verbose)std::cout<<"P\t"<<f.name<<'\t'<<f.lineNumber<<'\t'<<hex(f.rawValue)<<'\t';
        value(f.value);if(verbose)std::cout<<'\n';
    }
    for(const auto& f:s.designSettings)
    {
        hash.text(f.tableName);hash.text(f.analysisEngine);hash.text(f.field);hash.text(f.rawValue);hash.number(f.lineNumber);
        if(verbose)std::cout<<"D\t"<<f.tableName<<'\t'<<hex(f.analysisEngine)<<'\t'<<f.field<<'\t'<<f.lineNumber<<'\t'<<hex(f.rawValue)<<'\t';
        value(f.value);if(verbose)std::cout<<'\n';
    }
    hash.text(s.rawText);
    if(verbose)std::cout<<"R\t"<<hex(s.rawText)<<'\n';
    else std::cout<<"codepage="<<s.codePage<<" fields="<<s.settings.size()<<" designs="<<s.designSettings.size()<<" raw_bytes="<<s.rawText.size()<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
    return 0;
}
