// Guards: scripts reaching the OS, the file system or bytecode; require leaving the script folder.
#include "Support.h"

#include <filesystem>

using namespace fltest;

TEST_CASE("sandbox: io, os, debug and package are not opened by default")
{
    Rig rig;
    CHECK(rig.ok("return tostring(io) .. tostring(os) .. tostring(debug) .. tostring(package)") == "nilnilnilnil");
    CHECK(rig.ok("return tostring(dofile) .. tostring(loadfile)") == "nilnil");
    CHECK(rig.ok("return tostring(package and package.loadlib)") == "nil");
    // the safe libraries are there
    CHECK(rig.ok("return type(string.format) .. type(table.sort) .. type(math.floor) .. type(utf8.char) .. type(coroutine.wrap)") ==
          "functionfunctionfunctionfunctionfunction");
}

TEST_CASE("sandbox: the unsafe libraries open only when the host asks for them")
{
    Rig rig([](fl::Config& c) { c.allowOs = true; c.allowDebug = true; });
    CHECK(rig.ok("return type(os.time) .. type(debug.traceback)") == "functionfunction");
    CHECK(rig.ok("return tostring(io)") == "nil");
}

TEST_CASE("sandbox: load takes text only, never bytecode")
{
    Rig rig;
    CHECK(rig.ok("return tostring(string.dump)") == "nil");
    // a binary chunk is refused even though the script asked for mode "b" or "bt"
    CHECK(rig.ok("local f, err = load('\\27Lua\\84\\0\\25\\147\\13\\10\\26\\10', 'x', 'b') return tostring(f) .. '|' .. tostring(err)")
              .find("nil|") == 0);
    // the mode argument cannot unlock bytecode, and text still loads whatever the mode says
    CHECK(rig.ok("return load('return 41 + 1', 'c', 'b')()") == "42");
    // chunk functions and an environment still work
    CHECK(rig.ok("local env = {x = 5} return load('return x', 'c', 't', env)()") == "5");
    CHECK(rig.ok("local parts = {'return ', '7'} local i = 0 return load(function() i = i + 1 return parts[i] end)()") == "7");
}

TEST_CASE("sandbox: print goes to the server log")
{
    Rig rig;
    rig.ok("print('hello', 42, nil)");
    CHECK(rig.host.logContains("hello\t42\tnil"));
}

TEST_CASE("require: only script-folder modules with plain names")
{
    Rig rig;
    rig.dir.write("mod.lua", "return {answer = 42}");
    rig.dir.write("sub/inner.lua", "return 'inner'");
    rig.dir.write("noreturn.lua", "loaded_noreturn = true");
    rig.dir.write("a.lua", "return require('b')");
    rig.dir.write("b.lua", "return require('a')");
    rig.dir.write("broken.lua", "this is not lua");

    CHECK(rig.ok("return require('mod').answer") == "42");
    CHECK(rig.ok("return require('mod') == require('mod')") == "true");   // cached
    CHECK(rig.ok("return require('sub/inner')") == "inner");
    CHECK(rig.ok("require('noreturn') return tostring(require('noreturn'))") == "true");

    for (char const* bad : {"../outside", "..", "a.b", "/etc/passwd", "sub//inner", "sub/", "", "C:/x", "a\\\\b", "mod.lua"})
    {
        auto r = rig.run(std::string("return require('") + bad + "')");
        INFO("require name: " << bad << " -> " << r.error);
        CHECK_FALSE(r.ok);
    }
    // circular require is an error, not an endless loop
    auto circ = rig.run("return require('a')");
    CHECK_FALSE(circ.ok);
    CHECK(circ.error.find("circular") != std::string::npos);
    // a module that fails to load can be required again once fixed (no stale "loading" mark)
    CHECK_FALSE(rig.run("return require('broken')").ok);
    rig.dir.write("broken.lua", "return 'fixed'");
    CHECK(rig.ok("return require('broken')") == "fixed");
}

TEST_CASE("require: a module over the size limit is refused, a module at the limit still loads")
{
    Rig rig;
    rig.dir.write("big.lua", std::string((1u << 20) + 1, '-'));
    auto r = rig.run("return require('big')");
    CHECK_FALSE(r.ok);
    CHECK(r.error.find("too large") != std::string::npos);
    rig.dir.write("edge.lua", "return 7" + std::string((1u << 20) - 8, ' '));   // exactly 1 MB
    CHECK(rig.ok("return require('edge')") == "7");
}

TEST_CASE("require and the folder loader refuse a symlink that leaves the script folder")
{
    Rig rig;
    TempDir outside;
    outside.write("secret.lua", "leaked = true return 'secret'");
    std::filesystem::create_symlink(outside._path / "secret.lua", rig.dir._path / "link.lua");
    auto r = rig.run("return require('link')");
    CHECK_FALSE(r.ok);
    CHECK(rig.ok("return tostring(leaked)") == "nil");

    fl::LoadReport rep = rig.e().load();
    CHECK(rep.loaded.empty());
    REQUIRE(rep.errors.size() == 1);
    CHECK(rep.errors[0].find("outside the script folder") != std::string::npos);
    CHECK(rig.ok("return tostring(leaked)") == "nil");
}
