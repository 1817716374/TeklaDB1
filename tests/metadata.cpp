#include <tekla/db1/Catalogs.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream file(path, std::ios::binary);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    check(bool(file), "write metadata fixture");
}
std::string wrap(const std::string& text)
{ return "<TeklaStructuresModels><Model>" + text + "</Model></TeklaStructuresModels>"; }
}
int main(int argc, char** argv)
{
    if (argc != 3) return 2;
    try
    {
        const auto directory = std::filesystem::u8path(argv[1]);
        std::filesystem::create_directories(directory);
        const std::string mode = argv[2]; const auto path = directory / (mode + ".xml");
        tekla::db1::ModelMetadata result; std::string error;
        const auto accept = [&](const std::string& text) {
            write(path, text); error = "stale";
            const bool ok = tekla::db1::parseModelMetadata(path, result, error);
            check(ok, error.c_str());
            check(error.empty() && result.rawXml == text, "raw XML or error reset");
        };
        const auto reject = [&](const std::string& text) {
            write(path, text); result.name = "stale"; result.rawXml = "stale"; result.isTemplate = true;
            if (tekla::db1::parseModelMetadata(path, result, error))
                throw std::runtime_error("accepted invalid metadata: " + text.substr(0, 200));
            check(!error.empty() && result.name.empty() && result.rawXml.empty() && !result.isTemplate,
                  "failure must clear output and report error");
        };
        if (mode == "metadata_scope")
        {
            const auto input = "\xef\xbb\xbf" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
                "<!DOCTYPE TeklaStructuresModels><!-- <Name>Wrong comment</Name> -->" + wrap(
                "<Extension><Name>Wrong nested</Name></Extension><Name>  Correct\r\nname  </Name>"
                "<Designer>A</Designer><Description>B</Description><Version>19.0</Version>"
                "<ProductVersion>2025</ProductVersion><Language>enu</Language><Template/>"
                "<Environment>usimp</Environment><XS_PROJECT>%ROOT%/a</XS_PROJECT>"
                "<XS_FIRM>F</XS_FIRM><XS_SYSTEM>C:\\Original</XS_SYSTEM><ConnectedId>ID</ConnectedId>");
            accept(input);
            check(result.name == "  Correct\nname  " && result.designer == "A" && result.description == "B" &&
                  result.version == "19.0" && result.productVersion == "2025" && result.language == "enu" &&
                  result.templateName.empty() && result.environment == "usimp" && !result.isTemplate &&
                  result.projectSearchPath == "%ROOT%/a" && result.firmSearchPath == "F" &&
                  result.systemSearchPath == "C:\\Original" && result.connectedId == "ID", "direct fields or text changed");
            accept(wrap("<Extension><Name>Wrong nested</Name></Extension>"));
            check(result.name.empty(), "nested name substituted for missing name");
        }
        else if (mode == "metadata_entities")
        {
            accept(wrap("<Name>&amp;lt; &lt;&gt;&quot;&apos; &#65;&#x4e2d;&#x1f600;</Name>"
                        "<Description>before<!--comment-->middle<![CDATA[&foo;<tag>]]>after</Description>"));
            check(result.name == "&lt; <>\"' A\xe4\xb8\xad\xf0\x9f\x98\x80", "entity decoding was not single pass UTF-8");
            check(result.description == "beforemiddle&foo;<tag>after", "mixed text or CDATA lost");
            accept(wrap("<Name>A\n\rB\rC\r\nD&#13;</Name>"));
            check(result.name == "A\n\nB\nC\nD\r", "XML line ending normalization");
            accept(wrap("<Name> \n<!--comment--> \t<![CDATA[X]]> \r\n</Name><Designer> \n </Designer>"));
            check(result.name == " \n \tX \n" && result.designer == " \n ", "whitespace-only XML nodes lost");
            for (const auto* entity : {"&unknown;", "&", "&#;", "&#x;", "&#0;", "&#xD800;", "&#x110000;",
                                      "&#999999999999999;", "&#-1;", "&#X41;", "&#xG;", "&#1;"})
                reject(wrap(std::string("<Name>") + entity + "</Name>"));
            reject(wrap("<Extension value=\"&unknown;\"/>"));
            reject(wrap("<Extension>&unknown;</Extension>"));
        }
        else if (mode == "metadata_boolean")
        {
            for (const auto* value : {"TRUE", "true", "TrUe", "1", " \t\r\nTRUE\n "})
            { accept(wrap(std::string("<IsTemplate>") + value + "</IsTemplate>")); check(result.isTemplate, "true boolean"); }
            for (const auto* value : {"FALSE", "false", "0", "", " \t\r\n "})
            { accept(wrap(std::string("<IsTemplate>") + value + "</IsTemplate>")); check(!result.isTemplate, "false boolean"); }
            for (const auto* value : {"yes", "2", "TRUE FALSE", "not-a-boolean"})
                reject(wrap(std::string("<IsTemplate>") + value + "</IsTemplate>"));
        }
        else if (mode == "metadata_structure")
        {
            for (const auto& input : std::vector<std::string>{"", "<TeklaStructuresModels><Model/></Broken>",
                "<TeklaStructuresModels><Model/>", "<TeklaStructuresModelsFake><Model/></TeklaStructuresModelsFake>",
                "<TeklaStructuresModels><Extension><Model/></Extension></TeklaStructuresModels>",
                "<TeklaStructuresModels><Model/><Model/></TeklaStructuresModels>", wrap("") + wrap(""),
                "junk" + wrap(""), wrap("") + "junk", wrap("<Name>A</Name><Name>B</Name>"),
                wrap("") + "</orphan>", wrap("") + "</orphan><hidden/>",
                "<TeklaStructuresModels>junk<Model/></TeklaStructuresModels>", wrap("junk"),
                wrap("<Name>A<Inner>B</Inner></Name>"), wrap("<Name>A</Name><IsTemplate/><IsTemplate/>")}) reject(input);
            accept("<!--prefix-->" + wrap("<Name/>" ) + "<!--suffix-->");
        }
        else if (mode == "metadata_declarations")
        {
            for (const auto* declaration : {"<!DOCTYPE other>", "<!DOCTYPE TeklaStructuresModels SYSTEM 'missing.dtd'>",
                "<!DOCTYPE TeklaStructuresModels [<!ENTITY x 'expanded'>]>", "<!ENTITY x 'expanded'>"})
                reject(std::string(declaration) + wrap("<Name>&x;</Name>"));
            reject(wrap("") + "<!DOCTYPE TeklaStructuresModels>");
            reject("<!DOCTYPE TeklaStructuresModels><!DOCTYPE TeklaStructuresModels>" + wrap(""));
            reject(wrap("<!DOCTYPE TeklaStructuresModels>"));
            reject("<?xml version='1.0' encoding='ISO-8859-1'?>" + wrap(""));
            reject("<?xml version='1.1'?>" + wrap(""));
            reject("<?other command?>" + wrap(""));
            reject("<!--prefix--><?xml version='1.0'?>" + wrap(""));
        }
        else if (mode == "metadata_encoding")
        {
            accept(wrap("<Name>\xe6\xa8\xa1\xe5\x9e\x8b \xce\x94</Name>")); check(result.name == "\xe6\xa8\xa1\xe5\x9e\x8b \xce\x94", "UTF-8 text");
            for (const auto& invalid : std::vector<std::string>{std::string(1, '\0'), std::string(1, '\x01'),
                "\xc0\xaf", "\xe0\x80\x80", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xc2", "\xff\xfe"})
                reject(wrap("<Name>" + invalid + "</Name>"));
            reject(wrap("") + std::string(1, '\0') + "ignored");
        }
        else if (mode == "metadata_limits")
        {
            reject(wrap(std::string(16 * 1024 * 1024, 'x')));
            std::string deep;
            for (int i = 0; i < 600; ++i) deep += "<Extension>";
            for (int i = 0; i < 600; ++i) deep += "</Extension>";
            reject(wrap(deep));
            result.rawXml = "stale";
            check(!tekla::db1::parseModelMetadata(directory / "absent-metadata.xml", result, error) &&
                  result.rawXml.empty() && !error.empty(), "missing file failure");
        }
        else throw std::runtime_error("unknown metadata test");
        std::cout << mode << " passed\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
