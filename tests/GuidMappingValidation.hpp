int guidMappingValidation(const std::filesystem::path& path, bool evidence)
{
    std::string error;
    tekla::GuidMappingFile mappings;
    if (!tekla::parseGuidMappingFile(evidence ? path / "guid.mapper" : path, mappings, error))
        throw std::runtime_error(error);
    if (!evidence)
    {
        Fingerprint hash; std::size_t count = 0;
        for (const auto& batch : mappings.batches)
        {
            hash.number(batch.lineNumber); hash.text(batch.modelName); hash.number(batch.mappings.size());
            for (const auto& pair : batch.mappings)
            { hash.number(pair.lineNumber); hash.text(pair.sourceGuid); hash.text(pair.targetGuid); ++count; }
        }
        std::cout << "batches=" << mappings.batches.size() << " mappings=" << count << " fingerprint=" << std::hex << hash.value << std::dec << '\n';
        return 0;
    }
    tekla::Project project;
    tekla::ProjectOptions options;
    options.mainDatabase = "AUVENT_CCF_BE_AMCR1_2019_final.db1";
    options.strictCompanions = true;
    if (!tekla::readProject(path, project, error, options)) throw std::runtime_error(error);
    tekla::db1::Model previous;
    if (!tekla::db1::parseModelFile(path / "AUVENT_CCF_BE_AMCR1_2019.db1.bak", previous, error))
        throw std::runtime_error(error);
    if (project.model.storageVersion != "8.44" || previous.storageVersion != "8.44" ||
        project.model.identities.size() != 811 || previous.identities.size() != 811 || !project.guidMappings ||
        project.guidMappings->batches.size() != 6 || project.guidMappingTargets.size() != 811)
        throw std::runtime_error("public GUID mapper/model scope mismatch");
    const auto pairs = tekla::matchGuidMappingObjects(*project.guidMappings, previous, project.model);
    if (pairs.size() != 811) throw std::runtime_error("public GUID mapping direction or coverage mismatch");
    std::set<std::uint32_t> sourceIds, targetIds;
    bool fixedPair = false;
    for (const auto& pair : pairs)
    {
        if (pair.batchIndex != 5 || pair.sourceObjectId == pair.targetObjectId ||
            !sourceIds.insert(pair.sourceObjectId).second || !targetIds.insert(pair.targetObjectId).second)
            throw std::runtime_error("historical GUID pairing lost scope/uniqueness");
        const auto& mapping = project.guidMappings->batches[pair.batchIndex].mappings[pair.mappingIndex];
        if (mapping.sourceGuid == "7998c610-11ca-4333-8d41-1fc4375b943e" &&
            mapping.targetGuid == "9db7f2eb-37a0-4656-b18a-dd7240715047" &&
            pair.sourceObjectId == 254825405 && pair.targetObjectId == 509681756) fixedPair = true;
    }
    if (!fixedPair) throw std::runtime_error("independently read raw identity pair mismatch");
    std::cout << "batches=6 source_identities=811 target_identities=811 pairs=811 changed_ids=811 fixed_pair=1\n";
    return 0;
}
