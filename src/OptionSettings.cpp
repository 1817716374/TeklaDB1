#include <tekla/Environment.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>

namespace tekla
{
namespace
{
bool space(char c) { return c==' ' || c=='\t'; }
std::string trim(const std::string& s)
{
    const auto first=s.find_first_not_of(" \t"),last=s.find_last_not_of(" \t");
    return first==std::string::npos?std::string{}:s.substr(first,last-first+1);
}
bool keyword(const std::string& line,const char* word)
{
    std::size_t n=0;
    for(;word[n];++n)
    {
        if(n>=line.size())return false;
        const auto c=line[n]>='a' && line[n]<='z'?line[n]-'a'+'A':line[n];
        if(c!=word[n])return false;
    }
    return n==line.size() || space(line[n]);
}
bool nameCharacter(char c,bool first)
{ return (c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_' || (!first && c>='0' && c<='9'); }
template<class T> bool numeric(const std::string& text,T& value)
{
    if(text.empty())return false;
    std::istringstream input(text);input.imbue(std::locale::classic());
    input>>std::noskipws>>value;return !input.fail() && input.eof();
}
bool converted(const std::string& raw,StoredValueKind kind,StoredValue& value)
{
    const auto text=trim(raw);
    if(kind==StoredValueKind::String){value=raw;return true;}
    if(kind==StoredValueKind::Boolean)
    {
        if(text=="TRUE"){value=true;return true;}
        if(text=="FALSE"){value=false;return true;}
        return false;
    }
    if(kind==StoredValueKind::Integer)
    {
        std::int64_t v=0;
        if(!numeric(text,v) || v<(std::numeric_limits<std::int32_t>::min)() || v>(std::numeric_limits<std::int32_t>::max)())return false;
        value=static_cast<std::int32_t>(v);return true;
    }
    if(kind==StoredValueKind::Real)
    {
        double v=0;if(!numeric(text,v) || !std::isfinite(v))return false;
        value=v;return true;
    }
    return false;
}
}

bool parseOptionSettingsFile(const std::filesystem::path& path,OptionSettingsFile& result,
                             std::string& error,std::size_t maxBytes)
{
    try
    {
        result={};error.clear();
        const auto size=std::filesystem::file_size(path);
        if(size>maxBytes || size>static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
            throw std::runtime_error("option settings exceed byte limit");
        std::ifstream input(path,std::ios::binary);
        if(!input)throw std::runtime_error("cannot open option settings");
        result.sourcePath=path;result.rawText.resize(static_cast<std::size_t>(size));
        if(size && !input.read(&result.rawText[0],static_cast<std::streamsize>(size)))throw std::runtime_error("truncated option settings");
        if(input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("option settings changed while reading");
        if(result.rawText.find('\0')!=std::string::npos || result.rawText.compare(0,2,"\xff\xfe")==0 || result.rawText.compare(0,2,"\xfe\xff")==0)
            throw std::runtime_error("unsupported binary or UTF-16/32 option settings");
        std::size_t start=result.rawText.compare(0,3,"\xef\xbb\xbf")==0?3:0,lineNumber=0;
        std::set<std::string> names;
        while(start<result.rawText.size())
        {
            ++lineNumber;const auto end=result.rawText.find_first_of("\r\n",start);
            auto line=result.rawText.substr(start,end==std::string::npos?end:end-start);
            if(end==std::string::npos)start=result.rawText.size();
            else {start=end+1;if(result.rawText[end]=='\r' && start<result.rawText.size() && result.rawText[start]=='\n')++start;}
            const auto first=line.find_first_not_of(" \t");if(first==std::string::npos)continue;
            line.erase(0,first);if(keyword(line,"REM"))continue;
            OptionSetting setting;setting.lineNumber=lineNumber;
            if(keyword(line,"SET")){setting.setPrefix=true;line.erase(0,3);const auto p=line.find_first_not_of(" \t");if(p!=std::string::npos)line.erase(0,p);}
            const auto equal=line.find('=');setting.name=trim(line.substr(0,equal));
            bool valid=equal!=std::string::npos && !setting.name.empty();
            for(std::size_t i=0;i<setting.name.size();++i)valid=valid && nameCharacter(setting.name[i],i==0);
            if(!valid)
            {
                result.uninterpretedLines.push_back(lineNumber);
                result.diagnostics.push_back("uninterpreted option settings line "+std::to_string(lineNumber));continue;
            }
            setting.value=line.substr(equal+1);
            if(!names.insert(setting.name).second)result.diagnostics.push_back("duplicate option setting retained: "+setting.name);
            result.settings.push_back(std::move(setting));
        }
        return true;
    }
    catch(const std::exception& e){result={};error=e.what();return false;}
}

std::vector<OptionValueMatch> matchOptionSettings(const OptionSettingsFile& settings,const OptionsDatabase& database)
{
    std::multimap<std::string,const StoredOption*> byName;
    for(const auto& option:database.options)byName.emplace(option.second.name,&option.second);
    std::vector<OptionValueMatch> result;
    for(std::size_t i=0;i<settings.settings.size();++i)
    {
        const auto& setting=settings.settings[i];const auto range=byName.equal_range(setting.name);
        for(auto found=range.first;found!=range.second;++found)
        {
            const auto& option=*found->second;OptionValueMatch match;match.settingIndex=i;match.optionId=option.id;
            StoredValue value;match.valueParsed=converted(setting.value,option.kind,value);
            if(match.valueParsed)for(std::size_t slot=0;slot<2;++slot)match.matchingSlots[slot]=value==option.valueSlots[slot];
            result.push_back(match);
        }
    }
    return result;
}
}
