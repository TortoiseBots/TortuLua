// Guards: the reply and config helpers the host uses (they decide what a GM sees after .lua reload).
#include "Support.h"

#include "engine/Text.h"

TEST_CASE("text: splitWords splits config lists on any whitespace")
{
    CHECK(fl::splitWords("tt  other\tthird") == std::vector<std::string>{"tt", "other", "third"});
    CHECK(fl::splitWords("").empty());
    CHECK(fl::splitWords("   ").empty());
}

TEST_CASE("text: splitLines drops empty lines and carriage returns")
{
    std::vector<std::string> out;
    fl::splitLines("a\r\nb\n\n\nc", out);
    CHECK(out == std::vector<std::string>{"a", "b", "c"});
    out.clear();
    fl::splitLines("", out);
    CHECK(out.empty());
    fl::splitLines("one", out);
    CHECK(out == std::vector<std::string>{"one"});
}

TEST_CASE("text: describeLoad names the files and every error")
{
    fl::LoadReport r;
    r.loaded = {"init.lua", "b.lua"};
    r.errors = {"c.lua: boom"};
    auto lines = fl::describeLoad(r);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0] == "Tortulua: loaded 2 file(s): init.lua, b.lua");
    CHECK(lines[1] == "Tortulua error: c.lua: boom");
    fl::LoadReport missing;
    missing.folderMissing = true;
    auto l2 = fl::describeLoad(missing);
    REQUIRE(l2.size() == 2);
    CHECK(l2[0].find("not found") != std::string::npos);
    CHECK(l2[1] == "Tortulua: loaded 0 file(s)");
}
