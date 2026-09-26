#pragma once
// Literal values from pinned model-local options.ini files, independently of
// the binary decoder. Repeated project templates are regression, not new truth.
int optionSettingsEvidence(const std::filesystem::path& path,bool projectCheck)
{
    using namespace tekla;
    const auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    OptionSettingsFile settings;std::string error;
    require(parseOptionSettingsFile(path,settings,error),error.c_str());
    require(settings.uninterpretedLines.empty() && settings.diagnostics.empty() && !settings.settings.empty(),"unexpected INI syntax");
    struct Expected {std::string text;StoredValueKind kind;std::uint32_t flags;std::array<StoredValue,2> slots;};
    const std::map<std::string,Expected> expected{
        {"XS_CONSIDER_REBAR_HOOK_LOCATION_IN_CAST_UNIT_NUMBERING",{"TRUE",StoredValueKind::Boolean,0,{true,false}}},
        {"XS_SMALL_TUBE_ROUND_SEGMENTS",{"16",StoredValueKind::Integer,1,{std::int32_t{16},std::int32_t{32}}}},
        {"XS_ROUND_SEGMENTS",{"40",StoredValueKind::Integer,1,{std::int32_t{40},std::int32_t{40}}}},
        {"XS_SHORTENING_SYMBOL_COLOR",{"160",StoredValueKind::Integer,1,{std::int32_t{160},std::int32_t{0}}}},
        {"XS_CONNECT_UPLOAD_MODEL_FOLDER",{"Structural\\Tekla models",StoredValueKind::String,0,{std::string("Structural\\Tekla models"),std::string{}}}}
    };
    std::map<std::filesystem::path,OptionsDatabase> databases;
    for(const char* name:{"options_model.db","options_drawings.db"})
    {
        const auto file=path.parent_path()/name;OptionsDatabase database;
        require(parseOptionsDatabase(file,database,error),error.c_str());databases.emplace(std::filesystem::absolute(file).lexically_normal(),std::move(database));
    }
    Fingerprint hash;std::size_t matches=0;std::set<std::size_t> seen;
    for(const auto& item:databases)
        for(const auto& match:matchOptionSettings(settings,item.second))
        {
            const auto& setting=settings.settings.at(match.settingIndex);const auto& literal=expected.at(setting.name);
            const auto& option=item.second.options.at(match.optionId);
            require(seen.insert(match.settingIndex).second,"INI name has ambiguous DBV evidence");
            require(setting.value==literal.text && !setting.setPrefix,"literal INI value differs");
            require(option.kind==literal.kind && option.flags==literal.flags && option.valueSlots==literal.slots,"DBV slots differ from independent literal evidence");
            require(match.valueParsed && match.matchingSlots[0] && match.matchingSlots[1]==(literal.slots[0]==literal.slots[1]),"INI/DBV association differs");
            hash.text(setting.name);hash.text(setting.value);hash.number(setting.lineNumber);hash.number(match.optionId);++matches;
        }
    require(matches==settings.settings.size(),"INI assignment missing DBV evidence");
    if(projectCheck)
    {
        Project project;ProjectOptions options;options.readComponentLibrary=false;options.readDrawings=false;
        options.readNumbering=false;options.readEnvironment=false;options.readRawCompanions=false;options.strictCompanions=true;
        require(readProject(path.parent_path(),project,error,options),error.c_str());
        require(project.optionSettings && project.optionSettings->rawText==settings.rawText && project.optionSettingAssociations.size()==matches,"project INI associations missing");
        for(const auto& link:project.optionSettingAssociations)
        {
            require(databases.count(link.optionsDatabase)==1 && link.match.valueParsed && link.match.matchingSlots[0],"project INI association provenance");
            require(project.optionsDatabases.at(link.optionsDatabase).options.count(link.match.optionId)==1,"project INI option ID");
        }
        require(project.rawCompanions.empty(),"INI forced raw DBV parsing");
    }
    std::cout<<"ini_assignments="<<settings.settings.size()<<" dbv_matches="<<matches<<" project="<<projectCheck<<" fingerprint="<<std::hex<<hash.value<<std::dec<<'\n';
    return 0;
}
