int validateDrawingDimensions(const std::filesystem::path& path)
{
    tekla::Drawing drawing;std::string error;
    if(!tekla::parseDrawing(path,drawing,error))throw std::runtime_error(error);
    Fingerprint hash;
    for(const auto& entry:drawing.straightDimensionSets)
    {
        const auto& set=entry.second;hash.number(set.recordId);hash.number(set.contextId);
        for(auto id:set.dimensionIds)hash.number(id);
    }
    for(const auto& entry:drawing.straightDimensions)
    {
        const auto& d=entry.second;hash.number(d.recordId);hash.number(d.contextId);
        hash.number(d.dimensionSetId);hash.number(d.subtypeCode);
        for(auto point:{d.startPoint,d.endPoint,d.upDirection})for(auto value:point)hash.real(value);
        hash.real(d.distance);hash.number(bool(d.projectedLength));if(d.projectedLength)hash.real(*d.projectedLength);
    }
    for(auto id:drawing.unhandledDimensionRecordIds)hash.number(id);
    std::cout<<"dimension_sets="<<drawing.straightDimensionSets.size()<<" dimensions="<<drawing.straightDimensions.size()
             <<" unhandled="<<drawing.unhandledDimensionRecordIds.size()<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
    return 0;
}

int drawingDimensionEvidence(const std::filesystem::path& evidenceDirectory)
{
    // Values transcribed from Appendix 2, PDF pages 58 and 61. The corpus
    // runner verifies the independent source PDF's pinned size and SHA-256;
    // this check does not claim to parse PDF graphics or validate text styling.
    std::ifstream pdf(evidenceDirectory/"Krozanovski_Edgar.pdf",std::ios::binary);
    char signature[5]{};pdf.read(signature,5);
    if(!pdf || std::string(signature,5)!="%PDF-")throw std::runtime_error("independent drawing output is missing");
    const auto root=evidenceDirectory.parent_path()/"steel-fundamentals/Steel Fundamentals Modeling Assignment";
    tekla::db1::Model model;std::string error;
    if(!tekla::db1::parseModelDirectory(root,model,error))throw std::runtime_error(error);
    struct Evidence {const char* file;std::uint32_t object;const char* prefix;std::uint32_t number;std::map<std::uint32_t,double> values;};
    const std::array<Evidence,2> expected{{
        {"D2ba7aa95-d28a-4e9b-8f59-03f48f1ce3f2.dg",509825,"",1010,{{2131,105},{2128,130},{2048,70},{2047,35},{2045,30},{2044,70},{2043,30}}},
        {"D0af4f427-b208-4018-b750-6bb0549b6dda.dg",467268,"C",2,{{9832,10000},{9811,450},{9808,450},{9665,9980},{9571,4760},{9568,4840}}}
    }};
    std::size_t checked=0;
    for(const auto& source:expected)
    {
        tekla::Drawing drawing;
        if(!tekla::parseDrawing(root/"drawings"/source.file,drawing,error))throw std::runtime_error(error);
        if(!drawing.subject || drawing.subject->modelGuid!=model.identities.at(source.object).guid)
            throw std::runtime_error("independent dimension drawing subject mismatch");
        std::size_t positions=0;
        for(const auto& entry:model.objectNumberingReferences)
            if(entry.second.objectId==source.object && entry.second.numberingRecordId)
            {
                const auto& number=model.objectNumberingRecords.at(entry.second.numberingRecordId);
                if(number.prefix==source.prefix && number.positionNumber==source.number)++positions;
            }
        if(positions!=1)throw std::runtime_error("independent printed drawing number mismatch");
        for(const auto& item:source.values)
        {
            const auto& d=drawing.straightDimensions.at(item.first);
            if(!d.projectedLength || std::abs(*d.projectedLength-item.second)>1e-6 ||
               !drawing.viewsByContext.count(d.contextId))throw std::runtime_error("independent printed dimension mismatch");
            const auto& set=drawing.straightDimensionSets.at(d.dimensionSetId);
            if(set.contextId!=d.contextId || std::count(set.dimensionIds.begin(),set.dimensionIds.end(),d.recordId)!=1)
                throw std::runtime_error("dimension set ownership mismatch");
            ++checked;
        }
        if(source.object==509825)
        {
            if(drawing.straightDimensions.size()!=7 || drawing.straightDimensionSets.size()!=4 ||
               drawing.straightDimensionSets.at(2042).dimensionIds.size()!=3 ||
               drawing.straightDimensionSets.at(2046).dimensionIds.size()!=2)
                throw std::runtime_error("independent plate dimension chains differ");
        }
    }
    std::cout<<"independent_dimension_values="<<checked<<" drawings=2 plate_chains=4\n";
    return 0;
}
