#include <tekla/AnalysisSettings.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}}
int main(int argc,char** argv)
{
    if(argc!=3)return 2;
    try
    {
        const auto directory=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(directory);
        const std::string mode=argv[2];const auto path=directory/(mode+".admodel");
        tekla::AnalysisModelSettings value;std::string error;
        const auto write=[&](const std::string& text){std::ofstream f(path,std::ios::binary);f.write(text.data(),static_cast<std::streamsize>(text.size()));check(bool(f),"write fixture");};
        const auto accept=[&](const std::string& text){write(text);error="stale";check(tekla::parseAnalysisModelSettings(path,value,error),error.c_str());check(error.empty() && value.codePage==1252 && value.rawText==text && value.sourcePath==path,"source/header/reset");};
        const auto reject=[&](const std::string& text){write(text);value.rawText="stale";value.codePage=1;check(!tekla::parseAnalysisModelSettings(path,value,error),"invalid settings accepted");check(!error.empty() && value.codePage==0 && value.rawText.empty() && value.sourcePath.empty() && value.settings.empty() && value.designSettings.empty(),"failed output not cleared");};
        const std::string header="encoding 1252\n";
        if(mode=="analysis_settings_fields")
        {
            accept("encoding 1252\r\r\nModelName \"Model 1\"\r\r\nAccuracyOfIteration 0.001000\r\r\nModeCount 6\r\r\n");
            check(value.settings.size()==3 && std::get<std::string>(value.settings[0].value)=="Model 1" && std::get<double>(value.settings[1].value)==0.001 && std::get<std::int64_t>(value.settings[2].value)==6,"literal fields");
            check(value.settings[0].lineNumber==3 && value.settings[1].lineNumber==5 && value.settings[2].lineNumber==7 && value.settings[1].rawValue=="0.001000","positions/lexeme");
            accept(header+"ModeCount 1\nModeCount 2\nFutureSetting -3\n");check(value.settings.size()==3 && std::get<std::int64_t>(value.settings[0].value)==1 && std::get<std::int64_t>(value.settings[1].value)==2 && value.diagnostics.size()==2,"duplicate/precedence");
        }
        else if(mode=="analysis_settings_numbers")
        {
            accept(header+"A -9223372036854775808\nB 9223372036854775807\nC +.125e+2\nD -0.0\n");check(std::get<std::int64_t>(value.settings[0].value)==(std::numeric_limits<std::int64_t>::min)() && std::get<double>(value.settings[2].value)==12.5,"numeric values");
            for(const auto* v:{"9223372036854775808","-9223372036854775809","nan","inf","1e9999","1e-9999","0x10","1,25","1.2junk","1e","+","1 2","true"})reject(header+"A "+v+"\n");
        }
        else if(mode=="analysis_settings_quotes")
        {
            accept(header+"A \"\"\nB \" \"\nC \"C:\\models\\name;variant\"\n");check(std::get<std::string>(value.settings[0].value).empty() && std::get<std::string>(value.settings[1].value)==" " && std::get<std::string>(value.settings[2].value)=="C:\\models\\name;variant","string content");
            for(const auto* v:{"\"unterminated","\"a\" suffix","\"a\"\"b\"","unquoted","\"a\nb\"","\"a\\\"b\""})reject(header+"A "+v+"\n");
        }
        else if(mode=="analysis_settings_encoding")
        {
            accept(header+"Name \"caf\xe9 \x80 \x93test\x94\"\n");check(std::get<std::string>(value.settings[0].value)=="caf\xc3\xa9 \xe2\x82\xac \xe2\x80\x9ctest\xe2\x80\x9d","Windows-1252 decoding");
            for(unsigned char c:{0,1,31,127,129,141,143,144,157})reject(header+"Name \""+std::string(1,static_cast<char>(c))+"\"\n");
            reject("\xef\xbb\xbf"+header+"A 1\n");reject("encoding 65001\nA 1\n");reject("encoding 1251\nA 1\n");
        }
        else if(mode=="analysis_settings_design")
        {
            accept(header+"SteelDesignTable;NeutralFileIOLib;code;0\nConcreteDesignTable;XStaad;code;2\nSteelDesignTable;NeutralFileIOLib;code;1\n");check(value.designSettings.size()==3 && value.designSettings[0].tableName=="SteelDesignTable" && value.designSettings[0].analysisEngine=="NeutralFileIOLib" && value.designSettings[0].field=="code" && std::get<std::int64_t>(value.designSettings[2].value)==1 && value.diagnostics.size()==2,"design keys/duplicates");
            for(const auto* s:{"Table;;code;0","Table;Engine;;0","Table;Engine;code","Table;Engine;code;0;1","Table;Engine;code;","Table;\"Engine\";code;0","Table;En gine;code;0"})reject(header+s+"\n");
            accept(header+"Table;Engine;Field;\"text;value\"\n");check(std::get<std::string>(value.designSettings[0].value)=="text;value","quoted design value");
        }
        else if(mode=="analysis_settings_header")
        {
            for(const auto& text:std::vector<std::string>{"",header,"Name \"x\"\n",header+"encoding 1252\nA 1\n",header+"bad-name 1\n",header+"1Name 1\n",header+"A\n",header+"A = 1\n"})reject(text);
            accept("\n\tencoding\t1252\r\n\tName\t\"x\"\t");check(value.settings[0].lineNumber==3,"blank/header lines");
        }
        else if(mode=="analysis_settings_limits")
        {
            const auto text=header+"A 1\n";write(text);check(!tekla::parseAnalysisModelSettings(path,value,error,text.size()-1) && value.rawText.empty(),"byte limit ignored");check(tekla::parseAnalysisModelSettings(path,value,error,text.size()),"exact byte limit rejected");std::filesystem::remove(path);check(!tekla::parseAnalysisModelSettings(path,value,error) && value.rawText.empty(),"missing file accepted");
        }
        else return 2;
        std::cout<<mode<<" passed\n";
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
