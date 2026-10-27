#include <tekla/DrawingMetadata.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
std::string wrap(const std::string& value){return "<DrawingVersionMetadata>"+value+"</DrawingVersionMetadata>";}
}
int main(int argc,char** argv)
{
    if(argc!=3)return 2;
    try
    {
        const auto directory=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(directory);
        const std::string mode=argv[2];const auto path=directory/(mode+".dg.metadata");
        tekla::DrawingVersionMetadata value;std::string error;
        const auto write=[&](const std::string& text){std::ofstream f(path,std::ios::binary);f.write(text.data(),static_cast<std::streamsize>(text.size()));check(bool(f),"write fixture");};
        const auto accept=[&](const std::string& text){write(text);error="stale";check(tekla::parseDrawingVersionMetadata(path,value,error),error.c_str());check(value.rawXml==text && error.empty(),"raw XML/error reset");};
        const auto reject=[&](const std::string& text){write(text);value.rawXml="stale";value.width=1;value.name="stale";
            check(!tekla::parseDrawingVersionMetadata(path,value,error),"invalid metadata accepted");
            check(!error.empty() && value.rawXml.empty() && value.name.empty() && !value.width && value.storedIntegers.empty(),"failure output not cleared");};
        if(mode=="drawing_metadata_fields")
        {
            accept(wrap("<DrawingGuid> ABCDEFAB-0000-0000-0000-000000000001 </DrawingGuid><MainObjectGuid/>"
                "<Author>A</Author><Mark>[M.92]</Mark><Name> STANDARD </Name><Title1>T1</Title1><Title2>T2</Title2><Title3>T3</Title3>"
                "<Width>297</Width><Height>210.5</Height><DrawingType>1</DrawingType><IssueDate>0</IssueDate><CreateDate>1625531731</CreateDate>"
                "<UpToDate>1</UpToDate><Lock>0</Lock><Flag>5</Flag><PartsNo>25</PartsNo><Revision>-1</Revision>"));
            check(value.drawingGuid=="abcdefab-0000-0000-0000-000000000001" && value.mainObjectGuid.empty() && value.author=="A" &&
                value.mark=="[M.92]" && value.name=="STANDARD" && value.title1=="T1" && value.title2=="T2" && value.title3=="T3" &&
                value.width==297 && value.height==210.5 && value.drawingType==1 && value.storedIntegers.at("CreateDate")==1625531731 &&
                value.storedIntegers.at("Revision")==-1 && value.storedIntegers.at("Flag")==5,"metadata fields lost");
            accept(wrap("<Name/>"));check(!value.width && !value.drawingType && value.storedIntegers.empty(),"absent value became zero");
        }
        else if(mode=="drawing_metadata_scope")
        {
            accept(wrap("<!--<Name>wrong</Name>--><Extension><Name>nested</Name></Extension><Name>direct</Name>"));check(value.name=="direct","wrong field scope");
            accept(wrap("<Extension><Name>nested</Name></Extension>"));check(value.name.empty(),"nested fallback");
            reject(wrap("<Name>A</Name><Name>B</Name>"));reject(wrap("<Name><Nested/></Name>"));
            reject(wrap("<Name xmlns='other'>wrong</Name>"));reject("<DrawingVersionMetadata xmlns='other'/>");
            reject("<DrawingVersionMetadata/><DrawingVersionMetadata/>");reject("<Other/>");reject(wrap("text"));
        }
        else if(mode=="drawing_metadata_entities")
        {
            accept(wrap("<Name> &amp;lt; &#x4e2d; &#x1f600; </Name><Title1>before<!--c--><![CDATA[&x;]]>after</Title1>"));
            check(value.name=="&lt; \xe4\xb8\xad \xf0\x9f\x98\x80" && value.title1=="before&x;after","entity decoding");
            accept(wrap("<Name>A\n\rB\rC\r\nD&#13;</Name>"));check(value.name=="A\n\nB\nC\nD","line normalization/trim");
            for(const char* text:{"&missing;","&#0;","&#xD800;","&amp"})reject(wrap(std::string("<Name>")+text+"</Name>"));
            reject(wrap("<Extension a='&unknown;'/>"));
        }
        else if(mode=="drawing_metadata_numbers")
        {
            for(const char* text:{"","NaN","inf","-1","0","1,5","1e309","1junk"})reject(wrap(std::string("<Width>")+text+"</Width>"));
            for(const char* text:{"","1.2","1x","9223372036854775808","-9223372036854775809"})reject(wrap(std::string("<Flag>")+text+"</Flag>"));
            reject(wrap("<DrawingType>-1</DrawingType>"));reject(wrap("<DrawingType>4294967296</DrawingType>"));
            accept(wrap("<Flag>-9223372036854775808</Flag><Revision>9223372036854775807</Revision><Width>2.97e2</Width>"));
            check(value.width==297 && value.storedIntegers.at("Revision")==9223372036854775807LL,"numeric limits");
            reject(wrap("<Flag>0</Flag><Flag>1</Flag>"));
        }
        else if(mode=="drawing_metadata_guid")
        {
            for(const char* text:{"abc","000000000000000000000000000000000000","zzzzzzzz-0000-0000-0000-000000000000","{00000000-0000-0000-0000-000000000000}"})
                reject(wrap(std::string("<MainObjectGuid>")+text+"</MainObjectGuid>"));
            accept(wrap("<MainObjectGuid>02F6A87B-E776-431E-A2DB-9F8F1865E98C</MainObjectGuid>"));
            check(value.mainObjectGuid=="02f6a87b-e776-431e-a2db-9f8f1865e98c","GUID canonicalization");
        }
        else if(mode=="drawing_metadata_declarations")
        {
            accept("\xef\xbb\xbf<?xml version='1.0' encoding='UTF-8'?>"+wrap("<Name>A</Name>"));
            reject("<!DOCTYPE DrawingVersionMetadata>"+wrap(""));reject("<?xml version='1.0' encoding='UTF-16'?>"+wrap(""));
            reject(wrap("<?pi ignored?>"));reject(wrap("")+"<?xml version='1.0'?>");reject(wrap("<Name>\xc0\xaf</Name>"));
        }
        else if(mode=="drawing_metadata_limits")
        {
            const auto text=wrap("<Name>bounded</Name>");write(text);
            check(tekla::parseDrawingVersionMetadata(path,value,error,text.size()),"exact size budget");
            check(!tekla::parseDrawingVersionMetadata(path,value,error,text.size()-1) && value.rawXml.empty(),"size budget ignored");
            check(!tekla::parseDrawingVersionMetadata(directory/"absent.dg.metadata",value,error),"missing file accepted");
        }
        else throw std::runtime_error("unknown mode");
        std::cout<<mode<<" passed\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
