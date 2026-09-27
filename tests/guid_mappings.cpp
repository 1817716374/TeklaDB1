#include <tekla/GuidMappings.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& text)
{ std::ofstream file(path, std::ios::binary); file.write(text.data(), static_cast<std::streamsize>(text.size())); check(bool(file), "write mapper fixture"); }
const std::string a = "01234567-0000-0000-0000-000000000001";
const std::string b = "abcdefab-0000-0000-0000-000000000002";
const std::string c = "abcdefab-0000-0000-0000-000000000003";
}
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    try
    {
        const auto dir = std::filesystem::u8path(argv[1]); std::filesystem::create_directories(dir);
        const std::string mode = argv[2]; const auto path = dir / (mode + ".mapper");
        tekla::GuidMappingFile result; std::string error;
        const auto header = std::string("!Guid mapping for model.db1\n");
        if (mode == "mapper_text")
        {
            const auto text = "\xef\xbb\xbf" + header + "ID" + a + " IDABCDEFAB-0000-0000-0000-000000000002\r\n\n"
                "!Guid mapping for ../literal path.db1\r" + b + "\t" + c;
            write(path, text); const bool ok = tekla::parseGuidMappingFile(path, result, error);
            check(ok, error.c_str()); check(result.rawText == text && result.sourcePath == path, "source lost");
            check(result.batches.size() == 2 && result.batches[0].lineNumber == 1 && result.batches[1].lineNumber == 4,
                  "batch order/line number");
            check(result.batches[1].modelName == "../literal path.db1" && result.batches[1].mappings[0].lineNumber == 5,
                  "literal model name or row source");
            check(result.batches[0].mappings[0].sourceGuid == a && result.batches[0].mappings[0].targetGuid == b,
                  "GUID direction/canonical form");
        }
        else if (mode == "mapper_duplicates")
        {
            write(path, header + a + " " + b + "\n" + a + " " + c + "\n" + c + " " + c + "\n");
            const bool ok = tekla::parseGuidMappingFile(path, result, error); check(ok, error.c_str());
            check(result.batches[0].mappings.size() == 3 && result.diagnostics.size() == 2, "duplicate candidates silently selected");
        }
        else if (mode == "mapper_failure")
        {
            for (const auto& text : std::vector<std::string>{"", a + " " + b, "!Guid mapping for \n",
                header + a, header + a + " " + b + " extra", header + "IDbad " + b,
                header + a + " " + b.substr(1), header + "!unknown directive\n", header + std::string(1, '\0'),
                std::string("\xff\xfe", 2)})
            {
                write(path, text); result.batches.push_back({}); result.rawText = "stale";
                check(!tekla::parseGuidMappingFile(path, result, error) && !error.empty(), "invalid mapper accepted");
                check(result.batches.empty() && result.rawText.empty() && result.sourcePath.empty(), "partial output retained");
            }
            const auto text = header + a + " " + b;
            write(path, text); check(!tekla::parseGuidMappingFile(path, result, error, text.size() - 1), "limit ignored");
            check(tekla::parseGuidMappingFile(path, result, error, text.size()) && error.empty(), "exact limit rejected");
            check(!tekla::parseGuidMappingFile(dir / "missing.mapper", result, error) && result.batches.empty(), "missing file accepted");
        }
        else if (mode == "mapper_targets")
        {
            write(path, header + a + " " + b + "\n!Guid mapping for older.db1\n" + b + " " + c + "\n");
            const bool ok = tekla::parseGuidMappingFile(path, result, error); check(ok, error.c_str());
            tekla::db1::Model model;
            model.identities[10].guid = "IDABCDEFAB-0000-0000-0000-000000000002";
            model.identities[11].guid = c; model.identities[12].guid = "ID" + c;
            model.identities[13].guid = a;
            const auto links = tekla::matchGuidMappingTargets(result, model);
            check(links.size() == 1 && links[0].batchIndex == 0 && links[0].mappingIndex == 0 && links[0].modelObjectId == 10,
                  "ambiguous, reversed or transitive GUID match");
            check(model.identities[13].guid == a && result.batches.size() == 2, "matching mutated source");
            tekla::db1::Model source; source.identities[20].guid = a; source.identities[21].guid = b;
            const auto pairs = tekla::matchGuidMappingObjects(result, source, model);
            check(pairs.size() == 1 && pairs[0].sourceObjectId == 20 && pairs[0].targetObjectId == 10 &&
                  pairs[0].batchIndex == 0 && pairs[0].mappingIndex == 0, "cross-file object pairing");
            source.identities[22].guid = a;
            check(tekla::matchGuidMappingObjects(result, source, model).empty(), "ambiguous source GUID paired");
        }
        else throw std::runtime_error("unknown mapper test");
        std::cout << mode << " passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
