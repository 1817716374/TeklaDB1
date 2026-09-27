#include <tekla/Environment.hpp>
#include <algorithm>
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
void text(Bytes& b,std::size_t offset,const std::string& value) { check(offset+value.size()<b.size(),"fixture bounds"); std::copy(value.begin(),value.end(),b.begin()+static_cast<std::ptrdiff_t>(offset)); }
void save(const std::filesystem::path& p,const Bytes& b) { std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size())); check(bool(f),"fixture write"); }
struct Fixture
{
    std::vector<unsigned> widths;
    std::vector<std::vector<unsigned>> fields;
    std::vector<std::vector<Bytes>> rows;
    bool environment,legacy,extended;
    Fixture(bool env,bool old=false,bool modern=true):environment(env),legacy(old),extended(modern)
    {
        if (env)
        {
            widths={25,12,72,84,144,32,47,55,124};
            fields={{1,1,0},{1,1,1,1},{1,1,0,0,0,0,0,1},{1,1,0,0,0,0,0,1},
                    {1,1,0,0,0,1},{1,1,0,0,0,0,0,0,0},{1,1,1,0,0,0},{1,1,1,0,0,0},{1,1,1,0,0,0}};
            if (extended) { widths.insert(widths.end(),{104,116,172,77,85,154}); for (auto i:{2,3,4,6,7,8}) fields.push_back(fields[i]); }
            rows.resize(widths.size());
            Bytes c(25); put<unsigned>(c,0,1); text(c,4,"part"); rows[0]={c};
            Bytes m(32); put<unsigned>(m,0,2); rows[5]={m};
            const auto t=extended?9:2;
            Bytes a(widths[t]); put<unsigned>(a,0,3); text(a,4,"OBJECT_LOCKED"); text(a,25,"j_Locked");
            put<std::int32_t>(a,extended?88:56,-1); put<unsigned>(a,a.size()-4,2); rows[t]={a};
            Bytes link(12); put<unsigned>(link,0,4); put<unsigned>(link,4,1); put<unsigned>(link,8,3); rows[1]={link};
            Bytes choice(widths[extended?12:6]); put<unsigned>(choice,0,5); put<unsigned>(choice,4,3);
            put<unsigned>(choice,8,2); put<int>(choice,12,1); text(choice,16,"j_Yes"); rows[extended?12:6]={choice};
        }
        else
        {
            widths={86,98,114,2130,80,88,332,72,12};
            fields={{1,1,0,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0,0,0,0,0},
                {1,1,0,0,0,0,0,0,0,0},{1,1,1,0,0,0},{1,1,1,0,0,0},{1,1,1,0,0,0},{1,1,0,1},{1,1,1,1}};
            rows.resize(9);
            for (unsigned t=0;t<4;++t)
            {
                Bytes row(widths[t]); put<unsigned>(row,0,t+1); text(row,4,"XS_TEST_"+std::to_string(t)); put<unsigned>(row,68,5);
                if (t==0) row[72]=1;
                if (t==1) { put<int>(row,72,-42); put<int>(row,76,3); }
                if (t==2) { put<double>(row,72,1.25); put<double>(row,80,2.5); }
                if (t==3) { text(row,72,"first"); text(row,1096,"second"); }
                rows[t]={row};
            }
        }
    }
    Bytes encode(unsigned tag=1)
    {
        Bytes b;
        if (legacy) for (auto v:{1U,0U,0U,0U,1U,0xdbcec0bcU}) word(b,v);
        else
        {
            b={'D','B','V','@'}; const std::string name=environment?"Environment":"EnvModelOptions";
            word(b,static_cast<unsigned>(name.size())); b.insert(b.end(),name.begin(),name.end());
            for (auto v:{1U,environment?3U:2U,0U,0U,0U,1U}) word(b,v);
        }
        for (std::size_t t=0;t<widths.size();++t)
        {
            word(b,0xdbcec066); word(b,widths[t]); word(b,static_cast<unsigned>(fields[t].size()));
            for (auto f:fields[t]) word(b,f);
            for (const auto& row:rows[t]) { b.push_back(static_cast<std::uint8_t>(tag)); b.insert(b.end(),row.begin(),row.end()); b.resize(b.size()+8); }
            b.push_back(0);
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
        std::filesystem::create_directories(root); const auto path=root/"renamed.db";
        std::string error; tekla::EnvironmentDatabase env; tekla::OptionsDatabase options;
        if (mode=="environment_valid")
        {
            for (bool old:{false,true}) for (bool extended:{false,true}) for (auto tag:{1U,4U,12U})
            {
                Fixture f(true,old,extended); const auto b=f.encode(tag); save(path,b);
                check(tekla::parseEnvironmentDatabase(path,env,error,{true}),error);
                check(error.empty() && env.raw.decompressedFileImage==b,"raw retention/error");
                const auto& a=env.attributes.at(3);
                check(a.name=="OBJECT_LOCKED" && std::get<std::int32_t>(*a.storedValue)==-1 && a.choices.at(0).integerValue==1,"attribute values");
                check(a.objectClassIds==std::vector<std::uint32_t>{1} && env.objectClasses.at(1).attributeIds==std::vector<std::uint32_t>{3},"class join");
            }
        }
        else if (mode=="options_valid" || mode=="dbv_embedded_magic")
        {
            for (bool old:{false,true})
            {
                Fixture f(false,old);
                if (mode=="dbv_embedded_magic") put<unsigned>(f.rows[3][0],90,0xdbcec066);
                save(path,f.encode()); check(tekla::parseOptionsDatabase(path,options,error),error);
                check(options.options.size()==4 && std::get<bool>(options.options.at(1).valueSlots[0]) && !std::get<bool>(options.options.at(1).valueSlots[1]),"boolean pair");
                check(std::get<std::int32_t>(options.options.at(2).valueSlots[0])==-42 && std::get<double>(options.options.at(3).valueSlots[1])==2.5,"numeric pair");
                check(std::get<std::string>(options.options.at(4).valueSlots[1])=="second" && options.raw.tables.size()==9,"string pair/magic");
            }
        }
        else if (mode=="dbv_truncations")
        {
            Fixture f(true); const auto b=f.encode();
            for (std::size_t i=0;i<b.size();++i)
            {
                save(path,Bytes(b.begin(),b.begin()+static_cast<std::ptrdiff_t>(i)));
                check(!tekla::parseEnvironmentDatabase(path,env,error),"truncated DBV accepted at "+std::to_string(i));
                check(env.raw.tables.empty() && env.attributes.empty() && !error.empty(),"failed output not cleared");
            }
        }
        else if (mode=="environment_invalid")
        {
            for (int mutation=0;mutation<8;++mutation)
            {
                Fixture f(true);
                if (mutation==0) f.fields[9][2]=1;
                if (mutation==1) put<unsigned>(f.rows[1][0],8,999);
                if (mutation==2) put<unsigned>(f.rows[9][0],100,999);
                if (mutation==3) put<unsigned>(f.rows[12][0],4,999);
                if (mutation==4) f.rows[9].push_back(f.rows[9].front());
                if (mutation==5) f.rows[1].push_back(f.rows[1].front());
                if (mutation==6) f.rows[12].push_back(f.rows[12].front());
                auto b=f.encode(); if (mutation==7) b[8]='X'; save(path,b);
                check(!tekla::parseEnvironmentDatabase(path,env,error),"invalid environment accepted "+std::to_string(mutation));
                check(env.raw.tables.empty() && !error.empty(),"failed environment output retained");
            }
        }
        else if (mode=="options_invalid")
        {
            for (int mutation=0;mutation<6;++mutation)
            {
                Fixture f(false);
                if (mutation==0) f.rows[0][0][72]=2;
                if (mutation==1) put<double>(f.rows[2][0],80,std::numeric_limits<double>::infinity());
                if (mutation==2) f.rows[1].push_back(f.rows[1].front());
                if (mutation==3) put<unsigned>(f.rows[1][0],0,1);
                if (mutation==4) f.fields[2][2]=1;
                auto b=f.encode(); if (mutation==5) b.back()=9; save(path,b);
                check(!tekla::parseOptionsDatabase(path,options,error),"invalid options accepted "+std::to_string(mutation));
                check(options.options.empty() && options.raw.tables.empty() && !error.empty(),"failed options output retained");
            }
        }
        else if (mode=="dbv_unknown_rows")
        {
            Fixture f(false); f.rows[4].push_back(Bytes(f.widths[4])); save(path,f.encode());
            check(tekla::parseOptionsDatabase(path,options,error),error);
            check(options.raw.tables[4].records.size()==1 && options.diagnostics.size()>1,"unknown table lost");
            // Historical databases may contain the same key with different storage types.
            f=Fixture(false); text(f.rows[1][0],4,"XS_TEST_0"); save(path,f.encode());
            check(tekla::parseOptionsDatabase(path,options,error),error);
            check(options.options.size()==4 && options.diagnostics.size()>1,"cross-type key lost");
        }
        else if (mode=="dbv_limit")
        {
            Fixture f(true); const auto b=f.encode(); save(path,b);
            check(!tekla::parseEnvironmentDatabase(path,env,error,{false,b.size()-1}),"DBV decode limit ignored");
        }
        else return 2;
        return 0;
    }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
