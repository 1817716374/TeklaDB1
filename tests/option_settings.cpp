#include <tekla/Environment.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void write(const std::filesystem::path& path,const std::string& text)
{std::ofstream file(path,std::ios::binary);file.write(text.data(),static_cast<std::streamsize>(text.size()));check(bool(file),"write fixture");}
}
int main(int argc,char** argv)
{
    if(argc!=3)return 2;
    try
    {
        const auto directory=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(directory);
        const std::string mode=argv[2];const auto path=directory/(mode+".ini");
        tekla::OptionSettingsFile result;std::string error;
        if(mode=="settings_text")
        {
            const std::string input="\xef\xbb\xbf" "rem disabled\r\n\r\nXS_PATH= a=b;%ROOT% "+std::string(1,char(0xe9))+"\rset XS_EMPTY=\nXS_N=1\nXS_N=2\n[section]\n";
            write(path,input);check(tekla::parseOptionSettingsFile(path,result,error),error.c_str());
            check(result.rawText==input && result.settings.size()==4 && result.sourcePath==path,"text preservation");
            check(result.settings[0].lineNumber==3 && result.settings[0].value==" a=b;%ROOT% "+std::string(1,char(0xe9)),"raw assignment value");
            check(result.settings[1].setPrefix && result.settings[1].value.empty() && result.settings[1].lineNumber==4,"empty SET assignment");
            check(result.settings[2].name==result.settings[3].name && result.settings[2].value=="1" && result.settings[3].value=="2","ordered duplicates");
            check(result.uninterpretedLines==std::vector<std::size_t>{7} && result.diagnostics.size()==2,"unknown and duplicate diagnostics");
        }
        else if(mode=="settings_failure")
        {
            for(const auto& input:std::vector<std::string>{std::string("X=a\0b",5),std::string("\xff\xfe",2)})
            {
                write(path,input);result.settings.push_back({1,"OLD","value",false});
                check(!tekla::parseOptionSettingsFile(path,result,error) && !error.empty(),"binary settings accepted");
                check(result.settings.empty() && result.rawText.empty() && result.sourcePath.empty(),"failed output not cleared");
            }
            write(path,"X=abc");check(!tekla::parseOptionSettingsFile(path,result,error,4),"byte limit ignored");
            check(tekla::parseOptionSettingsFile(path,result,error,5) && error.empty(),"exact byte limit");
            write(path,"");check(tekla::parseOptionSettingsFile(path,result,error,0) && result.settings.empty(),"empty file");
            check(!tekla::parseOptionSettingsFile(directory/"absent.ini",result,error) && result.rawText.empty(),"missing settings file");
        }
        else if(mode=="settings_matching")
        {
            write(path,"B=TRUE\nI=-42\nR=1.25e0\nS=first\nI=3\nI=2147483648\nR=nan\nB=YES\nS= first\nUNKNOWN=1\ni=3\n");
            check(tekla::parseOptionSettingsFile(path,result,error),error.c_str());
            tekla::OptionsDatabase database;
            database.options[1]={1,"B",tekla::StoredValueKind::Boolean,{true,false},0};
            database.options[2]={2,"I",tekla::StoredValueKind::Integer,{std::int32_t{-42},std::int32_t{3}},1};
            database.options[3]={3,"R",tekla::StoredValueKind::Real,{1.25,2.5},0};
            database.options[4]={4,"S",tekla::StoredValueKind::String,{std::string("first"),std::string("second")},0};
            database.options[5]={5,"I",tekla::StoredValueKind::String,{std::string("3"),std::string("-42")},0};
            const auto matches=tekla::matchOptionSettings(result,database);check(matches.size()==12,"name/type matching coverage");
            unsigned primary=0,secondary=0,unparsed=0,different=0;
            for(const auto& match:matches)
            {
                primary+=match.matchingSlots[0];secondary+=match.matchingSlots[1];unparsed+=!match.valueParsed;
                different+=match.valueParsed && !match.matchingSlots[0] && !match.matchingSlots[1];
                check(match.optionId && match.settingIndex<9,"match provenance");
            }
            check(primary==5 && secondary==2 && unparsed==3 && different==2,"typed comparison or slot direction");
            check(result.settings.size()==11 && database.options.size()==5,"matching mutated input");
        }
        else if(mode=="settings_unknown")
        {
            write(path,"1BAD=x\nSET\n=bad\nX-Y=z\n#include file\nREM disabled\nREMARK=kept\nX=a # literal comment\n");
            check(tekla::parseOptionSettingsFile(path,result,error),error.c_str());
            check(result.uninterpretedLines==std::vector<std::size_t>({1,2,3,4,5}),"unknown syntax guessed");
            check(result.settings.size()==2 && result.settings[0].name=="REMARK" && result.settings[1].value=="a # literal comment","comment boundary");
        }
        else return 2;
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
