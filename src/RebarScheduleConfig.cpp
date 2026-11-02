#include <tekla/db1/Catalogs.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>

namespace tekla::db1
{
namespace
{
std::string trim(const std::string& value)
{
    const auto first=value.find_first_not_of(" \t\r\n\f\v");
    if(first==std::string::npos)return {};
    const auto last=value.find_last_not_of(" \t\r\n\f\v");
    return value.substr(first,last-first+1);
}

bool digits(const std::string& text,std::uint32_t& value)
{
    if(text.empty())return false;
    std::uint64_t parsed=0;
    for(const auto ch:text)
    {
        if(ch<'0'||ch>'9')return false;
        parsed=parsed*10+static_cast<unsigned>(ch-'0');
        if(parsed>std::numeric_limits<std::uint32_t>::max())return false;
    }
    value=static_cast<std::uint32_t>(parsed);
    return true;
}
}

bool parseRebarScheduleConfig(const std::filesystem::path& path,RebarScheduleConfig& result,std::string& error)
{
    result={};error.clear();
    try
    {
        std::error_code sizeError;
        const auto byteCount=std::filesystem::file_size(path,sizeError);
        if(sizeError)throw std::runtime_error("cannot determine rebar schedule configuration size");
        if(byteCount>64ULL*1024*1024)throw std::runtime_error("rebar schedule configuration exceeds 64 MiB limit");
        std::ifstream input(path,std::ios::binary);
        if(!input)throw std::runtime_error("cannot open rebar schedule configuration");
        RebarScheduleConfig parsed;parsed.sourcePath=path;
        parsed.rawText.assign(std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>());
        if(input.bad())throw std::runtime_error("failed reading rebar schedule configuration");
        std::set<std::pair<std::string,std::uint32_t>> seen;
        std::size_t begin=0,lineNumber=0;
        while(begin<parsed.rawText.size())
        {
            ++lineNumber;
            const auto end=parsed.rawText.find('\n',begin);
            const auto stop=end==std::string::npos?parsed.rawText.size():end;
            const auto original=parsed.rawText.substr(begin,stop-begin);
            auto parseLine=original;
            if(lineNumber==1&&parseLine.size()>=3&&
               static_cast<unsigned char>(parseLine[0])==0xef&&
               static_cast<unsigned char>(parseLine[1])==0xbb&&
               static_cast<unsigned char>(parseLine[2])==0xbf)
                parseLine.erase(0,3);
            const auto line=trim(parseLine);
            begin=end==std::string::npos?parsed.rawText.size():end+1;
            if(line.empty()||line[0]=='#'||line[0]==';')continue;
            constexpr char prefix[]="BEND_TYPE_";
            if(line.compare(0,sizeof(prefix)-1,prefix)!=0)
            {
                parsed.unparsedLines.emplace_back(lineNumber,original);
                continue;
            }
            const auto left=line.find('[');
            const auto right=left==std::string::npos?std::string::npos:line.find(']',left+1);
            const auto equal=right==std::string::npos?std::string::npos:line.find('=',right+1);
            if(left==std::string::npos||right==std::string::npos||equal==std::string::npos||left<=sizeof(prefix)-1||right+1!=equal)
                throw std::runtime_error("malformed BEND_TYPE entry at line "+std::to_string(lineNumber));
            const auto type=line.substr(sizeof(prefix)-1,left-(sizeof(prefix)-1));
            std::uint32_t index=0;
            if(type.empty()||!digits(line.substr(left+1,right-left-1),index)||index==0)
                throw std::runtime_error("invalid BEND_TYPE key at line "+std::to_string(lineNumber));
            const auto value=trim(line.substr(equal+1));
            if(value.empty())throw std::runtime_error("empty BEND_TYPE value at line "+std::to_string(lineNumber));
            if(!seen.emplace(type,index).second)
                throw std::runtime_error("duplicate BEND_TYPE index at line "+std::to_string(lineNumber));
            parsed.entries.push_back({type,index,value,lineNumber});
        }
        result=std::move(parsed);
        return true;
    }
    catch(const std::exception& e){result={};error=e.what();return false;}
}
}
