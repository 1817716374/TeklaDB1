#include <tekla/AnalysisSettings.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <locale>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace tekla
{
namespace
{
std::string trim(const std::string& text)
{
    const auto first=text.find_first_not_of(" \t");
    return first==std::string::npos?std::string{}:text.substr(first,text.find_last_not_of(" \t")-first+1);
}
bool identifier(const std::string& text)
{
    if(text.empty())return false;
    for(std::size_t i=0;i<text.size();++i)
    {
        const auto c=text[i];
        if(!((c>='A' && c<='Z') || (c>='a' && c<='z') || c=='_' || (i && c>='0' && c<='9')))return false;
    }
    return true;
}
std::string decoded(const std::string& text)
{
    static constexpr std::uint16_t extended[32]={
        0x20ac,0,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
        0x02c6,0x2030,0x0160,0x2039,0x0152,0,0x017d,0,
        0,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
        0x02dc,0x2122,0x0161,0x203a,0x0153,0,0x017e,0x0178};
    std::string out;
    for(unsigned char byte:text)
    {
        const auto c=byte>=0x80 && byte<=0x9f?extended[byte-0x80]:static_cast<std::uint16_t>(byte);
        if(!c || (c<32 && c!='\t') || c==127)throw std::runtime_error("unsupported control or undefined Windows-1252 byte");
        if(c<0x80)out.push_back(static_cast<char>(c));
        else if(c<0x800){out.push_back(static_cast<char>(0xc0|(c>>6)));out.push_back(static_cast<char>(0x80|(c&63)));}
        else {out.push_back(static_cast<char>(0xe0|(c>>12)));out.push_back(static_cast<char>(0x80|((c>>6)&63)));out.push_back(static_cast<char>(0x80|(c&63)));}
    }
    return out;
}
AnalysisSettingValue literal(const std::string& text)
{
    if(text.empty())throw std::runtime_error("missing analysis setting value");
    if(text.front()=='"')
    {
        if(text.size()<2 || text.back()!='"' || text.find('"',1)!=text.size()-1)
            throw std::runtime_error("unsupported analysis setting quotation");
        // Windows paths retain backslashes. No unverified escape convention.
        return decoded(text.substr(1,text.size()-2));
    }
    // Decimal literal syntax only, independent of the process locale.
    std::size_t pos=0;if(text[pos]=='+' || text[pos]=='-')++pos;
    std::size_t digits=0;while(pos<text.size() && text[pos]>='0' && text[pos]<='9'){++pos;++digits;}
    bool real=false;
    if(pos<text.size() && text[pos]=='.')
    {real=true;++pos;while(pos<text.size() && text[pos]>='0' && text[pos]<='9'){++pos;++digits;}}
    if(!digits)throw std::runtime_error("invalid analysis setting number");
    if(pos<text.size() && (text[pos]=='e' || text[pos]=='E'))
    {
        real=true;++pos;if(pos<text.size() && (text[pos]=='+' || text[pos]=='-'))++pos;
        const auto start=pos;while(pos<text.size() && text[pos]>='0' && text[pos]<='9')++pos;
        if(pos==start)throw std::runtime_error("invalid analysis setting exponent");
    }
    if(pos!=text.size())throw std::runtime_error("unsupported analysis setting literal");
    std::istringstream input(text);input.imbue(std::locale::classic());input>>std::noskipws;
    if(real)
    {
        double v=0;input>>v;
        if(input.fail() || !input.eof() || !std::isfinite(v))throw std::runtime_error("analysis setting real out of range");
        const auto mantissa=text.substr(0,text.find_first_of("eE"));
        if(v==0 && mantissa.find_first_of("123456789")!=std::string::npos)
            throw std::runtime_error("analysis setting real underflow");
        return v;
    }
    std::int64_t v=0;input>>v;
    if(input.fail() || !input.eof())throw std::runtime_error("analysis setting integer out of range");
    return v;
}
}
bool parseAnalysisModelSettings(const std::filesystem::path& path,AnalysisModelSettings& result,
    std::string& error,std::size_t maxBytes)
{
    try
    {
        result={};error.clear();const auto size=std::filesystem::file_size(path);
        if(size>maxBytes || size>static_cast<std::uintmax_t>((std::numeric_limits<std::streamsize>::max)()))
            throw std::runtime_error("analysis settings exceed byte limit");
        std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("cannot open analysis settings");
        result.rawText.resize(static_cast<std::size_t>(size));
        if(size && !input.read(&result.rawText[0],static_cast<std::streamsize>(size)))throw std::runtime_error("truncated analysis settings");
        if(input.peek()!=std::char_traits<char>::eof())throw std::runtime_error("analysis settings changed while reading");
        result.sourcePath=path;
        std::set<std::string> names;std::set<std::tuple<std::string,std::string,std::string>> designNames;
        std::size_t start=0,lineNumber=0;
        while(start<result.rawText.size())
        {
            ++lineNumber;const auto end=result.rawText.find_first_of("\r\n",start);
            const auto line=trim(result.rawText.substr(start,end==std::string::npos?end:end-start));
            start=end==std::string::npos?result.rawText.size():end+1;
            if(end!=std::string::npos && result.rawText[end]=='\r' && start<result.rawText.size() && result.rawText[start]=='\n')++start;
            if(line.empty())continue;
            try
            {
                if(!result.codePage)
                {
                    const auto split=line.find_first_of(" \t");
                    if(split==std::string::npos || line.substr(0,split)!="encoding" || trim(line.substr(split))!="1252")
                        throw std::runtime_error("expected encoding 1252 analysis settings header");
                    result.codePage=1252;continue;
                }
                // A semicolon inside a quoted scalar belongs to the value.
                const auto split=line.find_first_of(" \t;");
                const auto name=line.substr(0,split);
                if(!identifier(name) || name=="encoding" || split==std::string::npos)
                    throw std::runtime_error("invalid analysis setting name or duplicate header");
                if(line[split]==';')
                {
                    std::vector<std::string> cells;std::size_t at=0;
                    for(unsigned i=0;i<3;++i)
                    {const auto semi=line.find(';',at);if(semi==std::string::npos)throw std::runtime_error("incomplete analysis design setting");cells.push_back(trim(line.substr(at,semi-at)));at=semi+1;}
                    AnalysisDesignSetting setting;setting.tableName=cells[0];setting.analysisEngine=cells[1];setting.field=cells[2];
                    if(!identifier(setting.tableName) || !identifier(setting.field) || setting.analysisEngine.empty() ||
                        setting.analysisEngine.find_first_of(" \t\";\\")!=std::string::npos)
                        throw std::runtime_error("invalid analysis design setting key");
                    setting.analysisEngine=decoded(setting.analysisEngine);setting.rawValue=trim(line.substr(at));
                    setting.value=literal(setting.rawValue);setting.lineNumber=lineNumber;
                    if(!designNames.emplace(setting.tableName,setting.analysisEngine,setting.field).second)
                        result.diagnostics.push_back("duplicate analysis design setting retained at line "+std::to_string(lineNumber));
                    result.designSettings.push_back(std::move(setting));
                }
                else
                {
                    AnalysisSetting setting;setting.name=name;setting.rawValue=trim(line.substr(split));
                    setting.value=literal(setting.rawValue);setting.lineNumber=lineNumber;
                    if(!names.insert(name).second)result.diagnostics.push_back("duplicate analysis setting retained: "+name);
                    result.settings.push_back(std::move(setting));
                }
            }
            catch(const std::exception& e){throw std::runtime_error("analysis settings line "+std::to_string(lineNumber)+": "+e.what());}
        }
        if(!result.codePage || (result.settings.empty() && result.designSettings.empty()))throw std::runtime_error("empty analysis settings");
        result.diagnostics.emplace_back("saved analysis preset only; enumeration meanings, effective values and DB6 association are not inferred");
        return true;
    }
    catch(const std::exception& e){result={};error=e.what();return false;}
}
}
