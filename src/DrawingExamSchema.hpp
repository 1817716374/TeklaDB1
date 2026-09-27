// Exact signatures audited from the pinned official exam DG files.
// Included inside RelatedFormats.cpp's anonymous namespace.
void requireExamDrawingSchema(const db1::RawDatabase& raw,bool sectioned)
{
    struct Signature { unsigned type,width,fields; std::vector<unsigned> references; };
    const std::array<Signature,48> signature{{
        {0,4,2,{0}},
        {253,128,20,{0}},
        {254,596,63,{0,61,62}},
        {256,1480,205,{0,65}},
        {257,136,22,{0,6}},
        {259,656,101,{0,5}},
        {260,4676,646,{0,7,117,326,432}},
        {263,580,16,{0}},
        {264,568,84,{0,59}},
        {266,52,11,{0}},
        {268,32,6,{0}},
        {269,6132,804,{0,5}},
        {273,948,162,{0,32}},
        {275,232,37,{0,24}},
        {277,600,92,{0,4}},
        {278,296,48,{0}},
        {279,496,21,{0}},
        {280,144,21,{0}},
        {281,20,6,{0,4}},
        {293,45,6,{0}},
        {295,12,4,{0}},
        {296,37,5,{0}},
        {297,12,4,{0}},
        {298,110,5,{0}},
        {301,3438,50,{0}},
        {302,288,47,{0,31}},
        {303,860,143,{0,20}},
        {304,24,7,{0}},
        {305,64,12,{0}},
        {306,16,5,{0,4}},
        {307,36,10,{0,4,6,8}},
        {308,40,9,{0,4}},
        {309,20,6,{0,4}},
        {310,40,9,{0,4,6}},
        {311,120,21,{0}},
        {312,12,4,{0,2}},
        {313,64,10,{0}},
        {314,32,7,{0,3}},
        {315,56,9,{0}},
        {316,156,24,{0,4}},
        {317,24,7,{0}},
        {318,60,16,{0,6,7,8,9,10,11,12,13,14,15}},
        {319,112,17,{0}},
        {320,20,6,{0}},
        {321,60,16,{0}},
        {322,112,20,{0,3,8}},
        {323,144,23,{0,3}},
        {324,60,11,{0}},
    }};
    if(raw.tables.size()!=(sectioned?48U:47U))throw std::runtime_error("unsupported 8.95/9.08 drawing table count");
    std::size_t index=0;
    for(const auto& expected:signature)
    {
        if(!sectioned && expected.type==263)continue;
        const auto& t=raw.tables.at(index++);
        if(t.ordinal!=expected.type || t.payloadSize!=expected.width)
            throw std::runtime_error("unsupported 8.95/9.08 drawing table signature");
        if(sectioned)
        {
            if(t.fieldDescriptors.size()!=expected.fields)throw std::runtime_error("unsupported 9.08 drawing field count");
            for(std::size_t f=0;f<expected.fields;++f)
                if(t.fieldDescriptors[f]!=(std::find(expected.references.begin(),expected.references.end(),f)!=expected.references.end()?1U:0U))
                    throw std::runtime_error("unsupported 9.08 drawing reference signature");
        }
    }
}
