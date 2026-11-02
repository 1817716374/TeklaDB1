int reinforcementNumberingLinkValidation(const std::filesystem::path& path, const std::string& mode)
{
    tekla::Project project;
    tekla::ProjectOptions options;
    options.mainDatabase = path.filename();
    std::string error;
    if (!tekla::readProject(path.parent_path(), project, error, options))
        throw std::runtime_error(error);

    const bool w3 = mode == "rebar_numbering_link_w3";
    const std::size_t expectedCount = w3 ? 2U : 1U;
    if (project.reinforcementNumberingSeriesAssociations.size() != expectedCount)
        throw std::runtime_error("unexpected reinforcement numbering raw-equality association count");

    std::vector<std::uint32_t> values;
    for (const auto& association : project.reinforcementNumberingSeriesAssociations)
    {
        if (association.pairingEvidence != tekla::NumberingPairEvidence::DatabaseGuid ||
            association.database.filename() != path.filename() ||
            association.numberingDatabase.stem() != path.stem())
            throw std::runtime_error("reinforcement numbering association lost its file-pair scope");
        if (association.db1RawField != association.db2RawField)
            throw std::runtime_error("reinforcement numbering association is not byte-exact");

        const auto& record = project.model.objectNumberingRecords.at(association.numberingRecordId);
        if (record.kind != tekla::db1::ObjectNumberingKind::Reinforcement || record.rawPayload.size() < 12)
            throw std::runtime_error("association does not point to a reinforcement numbering row");
        const auto& database = project.numbering.at(association.numberingDatabase);
        const auto& series = database.series.at(association.seriesIndex);
        if (record.prefix != series.prefix || record.startNumber != series.startNumber ||
            series.additionalFields.size() != 5 || series.additionalFields[3] != association.db2RawField)
            throw std::runtime_error("association key or DB2 raw field does not match");
        values.push_back(association.db1RawField);
    }
    std::sort(values.begin(), values.end());
    if (w3)
    {
        if (values != std::vector<std::uint32_t>{4, 47})
            throw std::runtime_error("W3 reinforcement numbering raw values changed");
    }
    else if (values != std::vector<std::uint32_t>{0})
        throw std::runtime_error("9.08 reinforcement numbering raw value changed");

    std::cout << "reinforcement_series_raw_equalities=" << values.size() << " values=";
    for (std::size_t i = 0; i < values.size(); ++i)
        std::cout << (i ? "," : "") << values[i];
    std::cout << " meaning=uninterpreted\n";
    return 0;
}
