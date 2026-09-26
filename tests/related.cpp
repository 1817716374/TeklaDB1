#include <tekla/Drawing.hpp>
#include <tekla/Numbering.hpp>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
using Bytes=std::vector<std::uint8_t>;
void check(bool ok,const std::string& message) { if (!ok) throw std::runtime_error(message); }
template<class T> void put(Bytes& b,std::size_t offset,T value) { check(offset+sizeof(value)<=b.size(),"fixture bounds"); std::memcpy(b.data()+offset,&value,sizeof(value)); }
void word(Bytes& b,std::uint32_t value) { const auto p=b.size(); b.resize(p+4); put(b,p,value); }
void text(Bytes& b,std::size_t p,const std::string& value) { check(p+value.size()<b.size(),"fixture string bounds"); std::copy(value.begin(),value.end(),b.begin()+static_cast<std::ptrdiff_t>(p)); }
void save(const std::filesystem::path& p,const Bytes& b) { std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size())); check(bool(f),"fixture write"); }
Bytes numbering(const std::string& key="P/100",bool old=false)
{
    const std::string h=old ? "Xsteel\x80 7.82" : "Xsteel\x80 9.60 01234567-89ab-cdef-0123-456789abcdef";
    Bytes b(h.begin(),h.end()); word(b,10000040); word(b,0); word(b,184); word(b,0x00bc614f);
    word(b,22); word(b,1); word(b,old?76:80); b.push_back(4);
    Bytes row(old?76:80); text(row,0,key); put<std::uint32_t>(row,52,13); put<std::uint32_t>(row,56,2);
    b.insert(b.end(),row.begin(),row.end()); word(b,0x00bc614f); return b;
}
struct DgFixture
{
    std::array<unsigned,47> types{{253,254,256,257,259,260,263,264,266,268,269,273,275,277,278,279,280,281,293,295,296,297,298,301,302,303,304,305,306,307,308,309,310,311,312,313,314,315,316,317,318,319,320,321,322,323,324}};
    std::array<unsigned,47> widths{{128,620,1496,144,712,4788,580,584,52,32,6196,964,240,608,296,496,144,32,45,12,37,12,110,3438,296,876,24,64,28,72,48,32,64,120,24,64,40,56,164,24,180,112,20,60,144,152,60}};
    std::array<std::vector<Bytes>,47> rows;
    DgFixture()
    {
        Bytes first(45), second(45); put<unsigned>(first,0,1); put<unsigned>(first,4,2); text(first,16,"<Mark><UserText>HELLO ");
        put<unsigned>(second,0,2); text(second,16,"WORLD</UserText></Mark>"); rows[18]={first,second};
        Bytes property(110); put<unsigned>(property,0,3); put<unsigned>(property,4,2); text(property,8,"grProjectGuid");
        text(property,29,"01234567-89ab-cdef-0123-456789abcdef"); rows[22]={property};
        Bytes link(12); put<unsigned>(link,0,4); put<unsigned>(link,4,3); put<unsigned>(link,8,42); rows[21]={link};
        Bytes sheet(52); put<unsigned>(sheet,0,266); put<unsigned>(sheet,4,42); put<double>(sheet,16,594); put<double>(sheet,24,420); rows[8]={sheet};
    }
    Bytes encode()
    {
        const std::string h="Xsteel  9.54"; Bytes b(h.begin(),h.end()); word(b,47); word(b,4);
        for (auto i=types.rbegin();i!=types.rend();++i) { b.push_back(4); word(b,*i); }
        for (std::size_t i=0;i<types.size();++i)
        {
            word(b,static_cast<unsigned>(rows[i].size())); word(b,widths[i]);
            for (const auto& r:rows[i]) { b.push_back(4); b.insert(b.end(),r.begin(),r.end()); }
        }
        return b;
    }
};
}
int main(int argc,char** argv)
{
    if (argc!=3) return 2;
    try
    {
        const std::string mode=argv[2]; const auto root=std::filesystem::u8path(argv[1])/mode;
        std::filesystem::create_directories(root);
        const auto db2=root/"sample.db2", dg=root/"renamed.dg";
        std::string error="stale"; tekla::NumberingDatabase n; tekla::Drawing d; tekla::db1::RawDatabase raw;
        if (mode=="numbering_valid")
        {
            for (bool old:{false,true})
            {
                auto b=numbering("P/100",old); if (old) b[40]=5; save(db2,b);
                check(tekla::parseNumberingDatabase(db2,n,error),error); check(error.empty(),"stale numbering error");
                check(n.raw.tables.size()==2 && n.raw.tables[0].ordinal==10000040,"large table ID lost");
                check(n.series.size()==1 && n.series[0].prefix=="P" && n.series[0].startNumber==100 && n.series[0].partCounter==13 && n.series[0].assemblyCounter==2,"series fields incorrect");
                check(tekla::db1::parseRawDatabase(db2,raw,error,{true}),error); check(raw.decompressedFileImage==b,"DB2 raw bytes lost");
            }
        }
        else if (mode=="numbering_truncated")
        {
            auto b=numbering();
            // A header-only DB2 and a complete table boundary are legitimate.
            for (std::size_t i=0;i<b.size();++i)
            {
                if (i==49 || i==65) continue;
                save(db2,Bytes(b.begin(),b.begin()+static_cast<std::ptrdiff_t>(i)));
                check(!tekla::parseNumberingDatabase(db2,n,error),"truncated DB2 accepted at "+std::to_string(i));
                check(n.raw.tables.empty() && n.series.empty() && !error.empty(),"DB2 failed output not cleared");
            }
        }
        else if (mode=="numbering_invalid")
        {
            auto b=numbering(); b.back()=1; save(db2,b); check(!tekla::parseNumberingDatabase(db2,n,error),"bad terminator accepted");
            b=numbering(); put<unsigned>(b,69,0xffffffffU); save(db2,b); check(!tekla::parseNumberingDatabase(db2,n,error),"oversized row count accepted");
            b=numbering(); const Bytes duplicate(b.begin()+49,b.begin()+65); b.insert(b.end(),duplicate.begin(),duplicate.end());
            save(db2,b); check(!tekla::parseNumberingDatabase(db2,n,error),"duplicate table accepted");
            save(db2,numbering("P/4294967296")); check(!tekla::parseNumberingDatabase(db2,n,error),"start overflow accepted");
            save(db2,numbering("P/not-a-number")); check(!tekla::parseNumberingDatabase(db2,n,error),"invalid start accepted");
        }
        else if (mode=="numbering_unknown")
        {
            auto b=numbering(); b[11]='1'; save(db2,b);
            check(!tekla::parseNumberingDatabase(db2,n,error),"unknown numbering semantic version accepted");
            check(tekla::db1::parseRawDatabase(db2,raw,error),error);
            check(raw.storageVersion=="9.61" && raw.tables.size()==2,"unknown numbering raw preservation");
        }
        else
        {
            DgFixture fixture;
            if (mode=="drawing_cycle") put<unsigned>(fixture.rows[18][1],4,1);
            if (mode=="drawing_missing_chunk") put<unsigned>(fixture.rows[18][0],4,999);
            if (mode=="drawing_duplicate") fixture.rows[18].push_back(fixture.rows[18][0]);
            if (mode=="drawing_missing_property") put<unsigned>(fixture.rows[21][0],4,999);
            if (mode=="drawing_signature") fixture.widths[0]=129;
            if (mode=="drawing_nonfinite") put<double>(fixture.rows[8][0],16,std::numeric_limits<double>::quiet_NaN());
            auto b=fixture.encode(); if (mode=="drawing_unknown") b[11]='5'; save(dg,b);
            if (mode=="drawing_valid")
            {
                check(tekla::parseDrawing(dg,d,error,{true}),error); check(error.empty(),"stale drawing error");
                check(d.strings.size()==1 && d.strings.at(1).text=="<Mark><UserText>HELLO WORLD</UserText></Mark>","drawing XML chain incorrect");
                check(d.projectGuid=="01234567-89ab-cdef-0123-456789abcdef","GUID must come from stored property");
                check(d.sheets.size()==1 && d.sheets[0].width==594 && d.sheets[0].height==420,"sheet dimensions");
                check(d.propertyLinks.size()==1 && d.propertyLinks[0].ownerId==42,"property link");
                check(d.raw.decompressedFileImage==b,"drawing raw retention");
                check(tekla::db1::parseRawDatabase(dg,raw,error),error); check(raw.tables.size()==48 && raw.kind==tekla::db1::DatabaseKind::Drawing,"raw DG classification");
            }
            else if (mode=="drawing_truncated")
            {
                for (std::size_t i=0;i<b.size();++i)
                {
                    save(dg,Bytes(b.begin(),b.begin()+static_cast<std::ptrdiff_t>(i)));
                    check(!tekla::parseDrawing(dg,d,error),"truncated DG accepted at "+std::to_string(i));
                }
                b.push_back(0); save(dg,b); check(!tekla::parseDrawing(dg,d,error),"trailing DG bytes accepted");
            }
            else if (mode=="drawing_limit")
            {
                tekla::db1::RawDatabaseOptions options; options.maxDecodedBytes=b.size()-1;
                check(!tekla::parseDrawing(dg,d,error,options),"DG decoded byte budget ignored");
            }
            else if (mode=="drawing_unknown")
            {
                check(tekla::db1::parseRawDatabase(dg,raw,error),error);
                check(!tekla::parseDrawing(dg,d,error),"unknown drawing semantic version accepted");
            }
            else check(!tekla::parseDrawing(dg,d,error),"invalid drawing accepted: "+mode);
            if (mode!="drawing_valid") check(d.raw.tables.empty() && d.strings.empty() && !error.empty(),"DG failed output not cleared");
        }
        std::cout<<"PASS "<<mode<<'\n'; return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
