#include <tekla/db1/Database.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
using Bytes=std::vector<std::uint8_t>;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void word(Bytes& b,std::uint32_t n){for(unsigned i=0;i<4;++i)b.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
void put(Bytes& b,std::size_t at,std::uint32_t n){for(unsigned i=0;i<4;++i)b.at(at+i)=static_cast<std::uint8_t>(n>>(8*i));}
Bytes prefix(std::uint32_t contexts,std::uint32_t entries)
{
    Bytes b;for(auto n:{10014U,1U,0xdbcec0bcU,0U,1U,13U,contexts,155050U})word(b,n);
    for(std::uint32_t i=1;i<contexts;++i){word(b,i);for(unsigned j=0;j<20;++j)b.push_back(static_cast<std::uint8_t>(i+j));}
    word(b,5);word(b,entries);for(std::uint32_t i=0;i<entries*2;++i)word(b,i);
    for(auto n:{contexts,191488U,0U,0U,191456U})word(b,n);
    return b;
}
void table(Bytes& b,bool populated)
{
    word(b,0xdbcec066);word(b,12);word(b,2);word(b,0);word(b,1);
    if(populated){b.push_back(4);word(b,0xdbcec066);word(b,0);word(b,0xffffffff);word(b,0xdbcec066);word(b,0);}
    b.push_back(0);
}
Bytes fixture(){auto b=prefix(3,7);table(b,true);table(b,false);return b;}
Bytes rebuild(const tekla::db1::RawDatabase& raw)
{
    auto b=raw.preamble;
    for(const auto& t:raw.tables)
    {
        check(t.schemaValid && t.fileOffset==b.size() && t.opaqueSection.empty(),"table offset/framing");
        word(b,0xdbcec066);word(b,t.payloadSize);word(b,static_cast<std::uint32_t>(t.fieldDescriptors.size()));
        for(auto f:t.fieldDescriptors)word(b,f);
        for(const auto& r:t.records)
        {
            check(r.fileOffset==b.size() && r.payload.size()==t.payloadSize && r.allocatorMetadata.size()==8,"record offset/framing");
            b.push_back(r.allocationTag);b.insert(b.end(),r.payload.begin(),r.payload.end());
            b.insert(b.end(),r.allocatorMetadata.begin(),r.allocatorMetadata.end());
        }
        b.insert(b.end(),t.trailer.begin(),t.trailer.end());
    }
    return b;
}
}
int main(int argc,char** argv)
{
    if(argc!=3)return 2;
    try
    {
        const auto directory=std::filesystem::u8path(argv[1]);std::filesystem::create_directories(directory);
        const std::string mode=argv[2];const auto path=directory/(mode+".db6");
        tekla::db1::RawDatabase raw;tekla::db1::RawDatabaseOptions options;std::string error;
        const auto write=[&](const Bytes& b){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),static_cast<std::streamsize>(b.size()));check(bool(f),"write fixture");};
        const auto accept=[&](const Bytes& b){write(b);error="stale";check(tekla::db1::parseRawDatabase(path,raw,error,options),error.c_str());
            check(error.empty() && raw.kind==tekla::db1::DatabaseKind::Analysis && raw.storageVersion=="DB6-10014" && raw.databaseGuid.empty(),"kind/version/scope");
            check(rebuild(raw)==b && !raw.diagnostics.empty(),"lossy reconstruction");};
        const auto reject=[&](const Bytes& b){write(b);raw.storageVersion="stale";raw.preamble={1};
            check(!tekla::db1::parseRawDatabase(path,raw,error,options),"invalid DB6 accepted");
            check(!error.empty() && raw.storageVersion.empty() && raw.preamble.empty() && raw.tables.empty() && raw.sourcePath.empty(),"failed output not cleared");};
        const auto original=fixture();const auto offset=prefix(3,7).size();
        if(mode=="analysis_raw_roundtrip")
        {
            accept(original);check(raw.tables.size()==2 && raw.tables[0].records.size()==1 && raw.tables[1].records.empty(),"embedded magic became tables");
            check(raw.decompressedFileImage.empty(),"default image retention");
            options.retainDecompressedFileImage=true;accept(original);check(raw.decompressedFileImage==original,"retained image");
        }
        else if(mode=="analysis_raw_prefix")
        {
            for(auto contexts:{1U,2U,9U})for(auto entries:{0U,1U,17U}){auto b=prefix(contexts,entries);table(b,true);accept(b);}
        }
        else if(mode=="analysis_raw_header")
        {
            for(auto at:{0U,4U,8U,12U,16U,20U,24U,32U,56U,80U,144U}){auto b=original;put(b,at,0xffffffff);reject(b);}
            auto b=original;put(b,24,0);reject(b);b=original;put(b,84,0xffffffff);reject(b);
            b=prefix(3,7);reject(b);
        }
        else if(mode=="analysis_raw_tables")
        {
            for(auto at:{offset,offset+4,offset+8,offset+12}){auto b=original;put(b,at,0xffffffff);reject(b);}
            for(auto at:{offset+4,offset+8}){auto b=original;put(b,at,0);reject(b);}
            for(auto tag:{1U,5U,12U,255U}){auto b=original;b[offset+20]=static_cast<std::uint8_t>(tag);reject(b);}
            auto b=original;b.back()=4;reject(b);b=original;b.push_back(0);reject(b);
        }
        else if(mode=="analysis_raw_truncated")
        {
            // Ending exactly after a whole table is indistinguishable from
            // a smaller valid schema: no table count has been identified.
            const auto completeFirst=offset+20+21+1;
            for(std::size_t n=0;n<original.size();++n)if(n!=completeFirst)reject(Bytes(original.begin(),original.begin()+n));
        }
        else if(mode=="analysis_raw_limits")
        {
            options.maxDecodedBytes=original.size()-1;reject(original);
            options.maxDecodedBytes=original.size();accept(original);
        }
        else return 2;
        std::cout<<mode<<" passed\n";
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
