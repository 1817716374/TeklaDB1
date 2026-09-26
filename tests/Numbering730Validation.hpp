// Pinned public Tekla 17.0 teaching model. History is independent counter
// evidence, not proof that all historical objects or positions are current.
void validateNumbering730Evidence(const std::filesystem::path& directory)
{
    tekla::Project project; std::string error;
    if(!tekla::readProject(directory,project,error))throw std::runtime_error(error);
    if(project.model.storageVersion!="7.30" || project.numbering.size()!=2 ||
       !project.componentLibrary || !project.objectNumberingSeriesAssociations.empty())
        throw std::runtime_error("7.30 project scope/numbering mismatch");
    std::map<std::pair<std::string,std::string>,std::uint32_t> maxima;
    const std::regex assignment(R"((Part|Assembly)\s+id:\s*\d+\s+series:(.*?)\s+.*?->\s+(.*?)\s*$)");
    for(const auto* name:{"numbering.history","numberingresults"})
    {
        std::ifstream stream(directory/name,std::ios::binary);
        if(!stream)throw std::runtime_error("missing 7.30 numbering history");
        std::string line; std::smatch match;
        while(std::getline(stream,line))
        {
            if(!std::regex_match(line,match,assignment))continue;
            const auto key=match[2].str(),position=match[3].str();
            const auto slash=position.rfind('/'),keySlash=key.rfind('/');
            if(slash==std::string::npos || keySlash==std::string::npos || position.substr(0,slash)!=key.substr(0,keySlash))
                throw std::runtime_error("history prefix/series mismatch");
            const auto suffix=position.substr(slash+1);
            if(suffix.empty() || suffix.find_first_not_of("0123456789")!=std::string::npos)continue;
            const auto number=std::stoull(suffix);
            if(number>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("history number overflow");
            auto& maximum=maxima[{match[1].str(),key}];
            maximum=std::max(maximum,static_cast<std::uint32_t>(number));
        }
    }
    std::size_t compared=0,seriesCount=0,empty=0;
    for(const auto& entry:project.numbering)
    {
        const auto& database=entry.second;
        if(database.raw.storageVersion!="7.30" || !database.raw.databaseGuid.empty())throw std::runtime_error("unexpected 7.30 DB2 identity");
        if(database.series.empty()){++empty;continue;}
        seriesCount+=database.series.size();
        for(const auto& series:database.series)
            for(const auto& counter:{std::make_pair("Part",series.partCounter),std::make_pair("Assembly",series.assemblyCounter)})
            {
                if(!counter.second)continue;
                const auto found=maxima.find({counter.first,series.key});
                if(found==maxima.end() || std::uint64_t(series.startNumber)+counter.second-1!=found->second)
                    throw std::runtime_error("7.30 counter disagrees with history");
                ++compared;
            }
    }
    if(seriesCount!=20 || compared!=21 || empty!=1)throw std::runtime_error("7.30 numbering evidence coverage changed");
    std::cout<<"numbering_series=20 history_counter_checks=21 empty_libraries=1 automatic_object_series_links=0\n";
}
