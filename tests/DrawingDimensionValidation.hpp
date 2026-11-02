int validateDrawingDimensions(const std::filesystem::path& path)
{
    tekla::Drawing drawing;std::string error;
    if(!tekla::parseDrawing(path,drawing,error))throw std::runtime_error(error);
    if (path.filename()=="D0af4f427-b208-4018-b750-6bb0549b6dda.dg")
    {
        bool printedMark1001=false,printedMark1018=false;
        const auto inspect=[&](const auto& self,const tekla::DrawingMarkNode& node)->void {
            const auto name=node.attributes.find("name"),value=node.attributes.find("value");
            if (name!=node.attributes.end() && name->second=="PART_POS" && value!=node.attributes.end())
            {
                printedMark1001|=value->second=="1001";
                printedMark1018|=value->second=="1018";
            }
            for (const auto& child:node.children) self(self,child);
        };
        for (const auto& entry:drawing.strings)
            if (entry.second.markTemplate)
                for (const auto& node:entry.second.markTemplate->children) inspect(inspect,node);
        if (!printedMark1001 || !printedMark1018)
            throw std::runtime_error("9.54 drawing mark XML does not expose the two PDF-visible PART_POS values");
    }
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

int validateDrawingDimensionsDirectory(const std::filesystem::path& path)
{
    std::vector<std::filesystem::path> files;
    for (const auto& entry:std::filesystem::directory_iterator(path))
        if (entry.is_regular_file() && entry.path().extension()==".dg") files.push_back(entry.path());
    std::sort(files.begin(),files.end());
    Fingerprint hash;
    std::size_t setCount=0,dimensionCount=0,duplicateSetCount=0,duplicateSetIdCount=0,duplicateDimensionCount=0,duplicateDimensionIdCount=0,unhandledSetCount=0,unhandledCount=0,subtype0=0,subtype1=0,projected=0,linked=0,unlinked=0,missingSet=0,contextMismatch=0,missingMember=0,missingSetNoRow=0,missingSetContextMatch=0,missingSetContextMismatch=0,orphanSetIdCount=0;
    for (const auto& file:files)
    {
        tekla::Drawing drawing;std::string error;
        if(!tekla::parseDrawing(file,drawing,error))throw std::runtime_error(file.filename().string()+": "+error);
        if(drawing.raw.storageVersion!="7.82")throw std::runtime_error("non-7.82 drawing in legacy dimension corpus");
        setCount+=drawing.straightDimensionSets.size();
        dimensionCount+=drawing.straightDimensions.size();
        unhandledSetCount+=drawing.unhandledDimensionSetRecordIds.size();
        unhandledCount+=drawing.unhandledDimensionRecordIds.size();
        for(const auto& entry:drawing.straightDimensionSets)
        {
            const auto& set=entry.second;hash.number(set.recordId);hash.number(set.contextId);
            for(auto id:set.dimensionIds)hash.number(id);
        }
        const auto rawSetTable=std::find_if(drawing.raw.tables.begin(),drawing.raw.tables.end(),[](const auto& t){return t.ordinal==257;});
        if(rawSetTable==drawing.raw.tables.end())throw std::runtime_error("legacy dimension set raw table missing");
        std::map<std::uint32_t,std::vector<std::uint32_t>> rawSetContexts;
        std::set<std::uint32_t> orphanSetIdsInFile;
        for(const auto& row:rawSetTable->records)
        {
            const auto word=[&](std::size_t at){return std::uint32_t(row.payload[at])|(std::uint32_t(row.payload[at+1])<<8)|
                (std::uint32_t(row.payload[at+2])<<16)|(std::uint32_t(row.payload[at+3])<<24);};
            rawSetContexts[word(4)].push_back(word(8));
        }
        std::set<std::size_t> duplicateSetOffsets;
        std::map<std::uint32_t,std::vector<const tekla::db1::RawRecord*>> duplicateSetRows;
        for(const auto& occurrence:drawing.duplicateStraightDimensionSetRecords)
        {
            if(!occurrence.recordId || !duplicateSetOffsets.insert(occurrence.rawRecordOffset).second ||
               std::count(drawing.unhandledDimensionSetRecordIds.begin(),drawing.unhandledDimensionSetRecordIds.end(),occurrence.recordId)<2)
                throw std::runtime_error("duplicate dimension set occurrence lost its identity boundary");
            const auto row=std::find_if(rawSetTable->records.begin(),rawSetTable->records.end(),[&](const auto& r){return r.fileOffset==occurrence.rawRecordOffset;});
            if(row==rawSetTable->records.end() || row->payload.size()!=136)
                throw std::runtime_error("duplicate dimension set occurrence is not anchored to its raw row");
            const auto readWord=[&](std::size_t at){return std::uint32_t(row->payload[at])|(std::uint32_t(row->payload[at+1])<<8)|
                (std::uint32_t(row->payload[at+2])<<16)|(std::uint32_t(row->payload[at+3])<<24);};
            const auto id=readWord(4),context=readWord(8);
            if(id!=occurrence.recordId || context!=occurrence.contextId)throw std::runtime_error("duplicate dimension set fields differ from raw row");
            duplicateSetRows[id].push_back(&*row);
            ++duplicateSetCount;
            hash.number(occurrence.rawRecordOffset);hash.number(occurrence.recordId);hash.number(occurrence.contextId);
        }
        for(const auto& group:duplicateSetRows)
        {
            duplicateSetIdCount++;
            const auto& rows=group.second;
            if(rows.size()<2)throw std::runtime_error("duplicate dimension set group has one physical row");
            const bool identical=std::all_of(rows.begin()+1,rows.end(),[&](const auto* row){return row->payload==rows.front()->payload;});
            const auto decoded=drawing.straightDimensionSets.find(group.first);
            if(identical)
            {
                const auto& first=rows.front()->payload;
                const auto context=std::uint32_t(first[8])|(std::uint32_t(first[9])<<8)|(std::uint32_t(first[10])<<16)|(std::uint32_t(first[11])<<24);
                if(decoded==drawing.straightDimensionSets.end() || decoded->second.contextId!=context)
                    throw std::runtime_error("identical duplicate dimension set was not safely indexed");
            }
            else if(decoded!=drawing.straightDimensionSets.end())
                throw std::runtime_error("conflicting duplicate dimension set entered ID map");
        }
        for(const auto& entry:drawing.straightDimensions)
        {
            const auto& d=entry.second;
            const auto set=drawing.straightDimensionSets.find(d.dimensionSetId);
            const bool hasSet=set!=drawing.straightDimensionSets.end() && set->second.contextId==d.contextId;
            const auto members=hasSet?std::count(set->second.dimensionIds.begin(),set->second.dimensionIds.end(),d.recordId):0;
            if((hasSet && members!=1) || (!hasSet && members!=0))
                throw std::runtime_error("decoded legacy dimension has an invalid set/context link");
            if(hasSet)++linked;else ++unlinked;
            if(set==drawing.straightDimensionSets.end())++missingSet;
            else if(set->second.contextId!=d.contextId)++contextMismatch;
            else if(members==0)++missingMember;
            if(!hasSet)
            {
                const auto rawSets=rawSetContexts.find(d.dimensionSetId);
                if(rawSets==rawSetContexts.end())
                {
                    ++missingSetNoRow;
                    orphanSetIdsInFile.insert(d.dimensionSetId);
                }
                else if(std::find(rawSets->second.begin(),rawSets->second.end(),d.contextId)!=rawSets->second.end())++missingSetContextMatch;
                else ++missingSetContextMismatch;
            }
            if(d.subtypeCode==0)++subtype0;
            else if(d.subtypeCode==1)++subtype1;
            else throw std::runtime_error("unverified legacy dimension subtype was decoded");
            if(d.projectedLength)++projected;
            hash.number(d.recordId);hash.number(d.contextId);hash.number(d.dimensionSetId);hash.number(d.subtypeCode);
            for(auto point:{d.startPoint,d.endPoint,d.upDirection})for(auto value:point)hash.real(value);
            hash.real(d.distance);hash.number(bool(d.projectedLength));if(d.projectedLength)hash.real(*d.projectedLength);
        }
        std::set<std::size_t> duplicateOffsets;
        std::set<std::uint32_t> duplicateIds;
        for(const auto& occurrence:drawing.duplicateStraightDimensionRecords)
        {
            const auto& d=occurrence.dimension;
            duplicateIds.insert(d.recordId);
            if(!duplicateOffsets.insert(occurrence.rawRecordOffset).second || drawing.straightDimensions.count(d.recordId) ||
               (d.subtypeCode!=0 && d.subtypeCode!=1) ||
               std::count(drawing.unhandledDimensionRecordIds.begin(),drawing.unhandledDimensionRecordIds.end(),d.recordId)<2)
                throw std::runtime_error("ambiguous 7.82 dimension occurrence lost its identity boundary");
            const auto raw=std::find_if(drawing.raw.tables.begin(),drawing.raw.tables.end(),[&](const auto& t){return t.ordinal==256;});
            if(raw==drawing.raw.tables.end())throw std::runtime_error("duplicate dimension raw table missing");
            const auto row=std::find_if(raw->records.begin(),raw->records.end(),[&](const auto& r){return r.fileOffset==occurrence.rawRecordOffset;});
            const auto word=[&](std::size_t offset){
                return std::uint32_t(row->payload[offset]) | (std::uint32_t(row->payload[offset+1])<<8) |
                       (std::uint32_t(row->payload[offset+2])<<16) | (std::uint32_t(row->payload[offset+3])<<24);
            };
            if(row==raw->records.end() || row->payload.size()!=1400 ||
               word(4)!=d.recordId || word(8)!=d.contextId ||
               word(12)!=d.dimensionSetId || word(16)!=d.subtypeCode)
                throw std::runtime_error("duplicate dimension occurrence is not anchored to its raw record");
            const auto set=drawing.straightDimensionSets.find(d.dimensionSetId);
            if(set!=drawing.straightDimensionSets.end() && set->second.contextId!=d.contextId)
                throw std::runtime_error("duplicate dimension occurrence context disagrees with set");
            ++duplicateDimensionCount;
            hash.number(occurrence.rawRecordOffset);hash.number(d.recordId);hash.number(d.contextId);
            hash.number(d.dimensionSetId);hash.number(d.subtypeCode);
            for(auto point:{d.startPoint,d.endPoint,d.upDirection})for(auto value:point)hash.real(value);
            hash.real(d.distance);hash.number(bool(d.projectedLength));if(d.projectedLength)hash.real(*d.projectedLength);
        }
        duplicateDimensionIdCount+=duplicateIds.size();
        orphanSetIdCount+=orphanSetIdsInFile.size();
        for(auto id:drawing.unhandledDimensionRecordIds)hash.number(id);
        for(auto id:drawing.unhandledDimensionSetRecordIds)hash.number(id);
    }
    if(files.size()!=293 || setCount!=11568 || duplicateSetCount!=28 || duplicateSetIdCount!=14 || dimensionCount!=22782 || unhandledSetCount!=28 ||
       unhandledCount!=62 || subtype0!=20724 || subtype1!=2058 || linked!=22671 || unlinked!=111 ||
       missingSet!=111 || contextMismatch!=0 || missingMember!=0 || missingSetNoRow!=111 ||
       missingSetContextMatch!=0 || missingSetContextMismatch!=0 ||
       dimensionCount+unhandledCount!=22844 || duplicateDimensionCount!=62 || duplicateDimensionIdCount!=31)
        throw std::runtime_error("legacy drawing dimension corpus counts changed: files="+std::to_string(files.size())+
            " sets="+std::to_string(setCount)+" unhandled_sets="+std::to_string(unhandledSetCount)+
            " dimensions="+std::to_string(dimensionCount)+" subtype0="+std::to_string(subtype0)+
            " subtype1="+std::to_string(subtype1)+" unhandled="+std::to_string(unhandledCount)+
            " linked="+std::to_string(linked)+" unlinked="+std::to_string(unlinked));
    std::cout<<"files="<<files.size()<<" sets="<<setCount<<" duplicate_set_ids="<<duplicateSetIdCount
             <<" duplicate_set_records="<<duplicateSetCount<<" unhandled_sets="<<unhandledSetCount
             <<" dimensions="<<dimensionCount<<" subtype0="<<subtype0<<" subtype1="<<subtype1
             <<" duplicate_ids="<<duplicateDimensionIdCount<<" duplicate_records="<<duplicateDimensionCount
             <<" unhandled="<<unhandledCount<<" linked="<<linked<<" unlinked="<<unlinked
             <<" missing_set="<<missingSet<<" context_mismatch="<<contextMismatch<<" missing_member="<<missingMember
             <<" missing_set_no_row="<<missingSetNoRow<<" missing_set_context_match="<<missingSetContextMatch
             <<" missing_set_context_mismatch="<<missingSetContextMismatch<<" orphan_set_ids="<<orphanSetIdCount
             <<" projected="<<projected<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
    return 0;
}

int validateDrawingDimensions730Directory(const std::filesystem::path& path)
{
    std::vector<std::filesystem::path> files;
    for(const auto& entry:std::filesystem::directory_iterator(path))
        if(entry.is_regular_file() && entry.path().extension()==".dg")files.push_back(entry.path());
    std::sort(files.begin(),files.end());
    Fingerprint hash;
    std::size_t sets=0,dimensions=0,subtype0=0,subtype1=0,unhandled=0,linked=0,projected=0;
    for(const auto& file:files)
    {
        tekla::Drawing drawing;std::string error;
        if(!tekla::parseDrawing(file,drawing,error))throw std::runtime_error(file.filename().string()+": "+error);
        if(drawing.raw.storageVersion!="7.30")throw std::runtime_error("non-7.30 drawing in dimension corpus");
        sets+=drawing.straightDimensionSets.size();
        dimensions+=drawing.straightDimensions.size();
        unhandled+=drawing.unhandledDimensionRecordIds.size();
        for(const auto& entry:drawing.straightDimensionSets)
        {
            const auto& set=entry.second;hash.number(set.recordId);hash.number(set.contextId);
            for(const auto id:set.dimensionIds)hash.number(id);
        }
        for(const auto& entry:drawing.straightDimensions)
        {
            const auto& dimension=entry.second;
            const auto set=drawing.straightDimensionSets.find(dimension.dimensionSetId);
            const bool linkedSet=set!=drawing.straightDimensionSets.end() && set->second.contextId==dimension.contextId;
            const auto owners=linkedSet?std::count(set->second.dimensionIds.begin(),set->second.dimensionIds.end(),dimension.recordId):0;
            if(!linkedSet || owners!=1)throw std::runtime_error("7.30 decoded dimension set/context link is invalid");
            ++linked;
            if(dimension.subtypeCode==0)++subtype0;
            else if(dimension.subtypeCode==1)
            {
                ++subtype1;
                const auto& up=dimension.upDirection;
                const double norm=std::hypot(std::hypot(up[0],up[1]),up[2]);
                if(std::abs(norm-1.0)>1e-7)throw std::runtime_error("7.30 subtype-1 direction is not a unit vector");
            }
            else throw std::runtime_error("unverified 7.30 dimension subtype was decoded");
            if(dimension.projectedLength)++projected;
            hash.number(dimension.recordId);hash.number(dimension.contextId);
            hash.number(dimension.dimensionSetId);hash.number(dimension.subtypeCode);
            for(const auto point:{dimension.startPoint,dimension.endPoint,dimension.upDirection})
                for(const auto value:point)hash.real(value);
            hash.real(dimension.distance);hash.number(bool(dimension.projectedLength));
            if(dimension.projectedLength)hash.real(*dimension.projectedLength);
        }
        for(const auto id:drawing.unhandledDimensionRecordIds)hash.number(id);
    }
    if(files.size()!=35 || sets!=187 || dimensions!=341 || subtype0!=313 || subtype1!=28 || unhandled!=0 || linked!=341)
        throw std::runtime_error("7.30 drawing dimension corpus counts changed: files="+std::to_string(files.size())+
            " sets="+std::to_string(sets)+" dimensions="+std::to_string(dimensions)+
            " subtype0="+std::to_string(subtype0)+" subtype1="+std::to_string(subtype1)+
            " unhandled="+std::to_string(unhandled)+" linked="+std::to_string(linked));
    std::cout<<"files="<<files.size()<<" sets="<<sets<<" dimensions="<<dimensions
             <<" subtype0="<<subtype0<<" subtype1="<<subtype1<<" unhandled="<<unhandled
             <<" linked="<<linked<<" projected="<<projected<<" fingerprint="
             <<std::hex<<hash.value<<std::dec<<'\n';
    return 0;
}

int drawingDimensionEvidence(const std::filesystem::path& evidenceDirectory)
{
    // Values were transcribed from Appendix 2, PDF pages 58 and 61. The
    // source URL and SHA-256 are documented separately; downloading that PDF
    // is not a prerequisite for running parser regressions.
    const auto& root=evidenceDirectory;
    tekla::db1::Model model;std::string error;
    if(!tekla::db1::parseModelDirectory(root,model,error))throw std::runtime_error(error);
    struct Evidence {const char* file;std::uint32_t object;const char* prefix;std::uint32_t number;std::map<std::uint32_t,double> values;};
    const std::array<Evidence,2> expected{{
        {"D2ba7aa95-d28a-4e9b-8f59-03f48f1ce3f2.dg",509825,"",1010,{{2131,105},{2128,130},{2048,70},{2047,35},{2045,30},{2044,70},{2043,30}}},
        {"D0af4f427-b208-4018-b750-6bb0549b6dda.dg",467268,"C",2,{{9832,10000},{9811,450},{9808,450},{9665,9980},{9571,4760},{9568,4840},
            {10599,155},{10598,80},{10597,155},{10589,210},{9668,390},{9572,20}}}
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
            if(!d.projectedLength || std::abs(*d.projectedLength-item.second)>1e-4 ||
               !drawing.viewsByContext.count(d.contextId))
                throw std::runtime_error("independent printed dimension mismatch: record="+std::to_string(item.first)+
                    " expected="+std::to_string(item.second)+" actual="+
                    (d.projectedLength?std::to_string(*d.projectedLength):"unavailable"));
            const auto& set=drawing.straightDimensionSets.at(d.dimensionSetId);
            if(set.contextId!=d.contextId || std::count(set.dimensionIds.begin(),set.dimensionIds.end(),d.recordId)!=1)
                throw std::runtime_error("dimension set ownership mismatch");
            ++checked;
        }
        if(source.object==509825)
        {
            const auto paperView=std::find_if(drawing.viewsByRecordId.begin(),drawing.viewsByRecordId.end(),
                [](const auto& entry){return entry.second.contextId==12;});
            if(paperView==drawing.viewsByRecordId.end() || !paperView->second.storedPaperPosition ||
               !paperView->second.storedScaleValue || *paperView->second.storedPaperPosition!=std::array<double,2>{83.5,105.0} ||
               *paperView->second.storedScaleValue!=1.0)
                throw std::runtime_error("independent 9.54 stored paper-field evidence mismatch");
            if(drawing.straightDimensions.size()!=7 || drawing.straightDimensionSets.size()!=4 ||
               drawing.straightDimensionSets.at(2042).dimensionIds.size()!=3 ||
               drawing.straightDimensionSets.at(2046).dimensionIds.size()!=2)
                throw std::runtime_error("independent plate dimension chains differ");
        }
    }
    std::cout<<"independent_dimension_values="<<checked<<" drawings=2 plate_chains=4\n";
    return 0;
}
