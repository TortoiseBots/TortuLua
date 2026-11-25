#include "Support.h"

using namespace fltest;

TEST_CASE("json: encode scalars, arrays and sorted objects")
{
    Rig rig;
    CHECK(rig.ok("return Json.encode({cmd = 'selftest', ok = true, n = 3, f = 1.5, list = {1, 2, 3}, nested = {a = {}}})") ==
          R"({"cmd":"selftest","f":1.5,"list":[1,2,3],"n":3,"nested":{"a":{}},"ok":true})");
    CHECK(rig.ok("return Json.encode('a\"b\\\\c\\n\\t\\1 \\xc3\\xa9')") == "\"a\\\"b\\\\c\\n\\t\\u0001 \xc3\xa9\"");
    CHECK(rig.ok("return Json.encode(nil)") == "null");
    CHECK(rig.ok("return Json.encode({1, Json.null, 3})") == "[1,null,3]");
    CHECK(rig.ok("return Json.encode(2^53)") == "9007199254740992");
    CHECK(rig.ok("return Json.encode(-7)") == "-7");
    CHECK(rig.ok("return Json.encode({})") == "{}");
}

TEST_CASE("json: encode refuses what it cannot represent, with a reason")
{
    Rig rig;
    for (char const* bad : {"print", "{f = print}", "0/0", "math.huge", "{[1.5] = 1}", "{[true] = 1}", "{1, 2, nil, 4, x = 1}",
                            "coroutine.create(print)", "'\\xff\\xfe'", "{[{}] = 1}"})
    {
        INFO(bad);
        CHECK(rig.ok(std::string("local s, err = Json.encode(") + bad + ") return tostring(s) .. '|' .. tostring(err ~= nil)") == "nil|true");
    }
    // a cycle and a too deep structure: an error, not a stack overflow
    CHECK(rig.ok("local t = {} t.self = t local s, err = Json.encode(t) return tostring(s) .. tostring(err)").find("nil") == 0);
    CHECK(rig.ok("local t = {} local p = t for i = 1, 100 do p.x = {} p = p.x end local s = Json.encode(t) return tostring(s)") == "nil");
}

TEST_CASE("json: decode values, escapes, surrogate pairs and big numbers")
{
    Rig rig;
    CHECK(rig.ok("local t = Json.decode('{\"a\":[1,2.5,\"x\",true,false,null],\"b\":{\"c\":-12}}') "
                 "return t.a[1] .. '|' .. t.a[2] .. '|' .. t.a[3] .. '|' .. tostring(t.a[4]) .. tostring(t.a[5]) .. "
                 "tostring(t.a[6] == Json.null) .. '|' .. t.b.c .. '|' .. math.type(t.a[1]) .. math.type(t.a[2])") ==
          "1|2.5|x|truefalsetrue|-12|integerfloat");
    CHECK(rig.ok("return Json.decode('\"\\\\u00e9\\\\ud83d\\\\ude00\\\\n\"')") == "\xc3\xa9\xf0\x9f\x98\x80\n");
    CHECK(rig.ok("return math.type(Json.decode('9223372036854775807')) .. math.type(Json.decode('9223372036854775808')) .. "
                 "math.type(Json.decode('1e3')) .. math.type(Json.decode('0.5'))") == "integerfloatfloatfloat");
    CHECK(rig.ok("return Json.decode(' [ ] ') ~= nil") == "true");
    CHECK(rig.ok("local r = Json.decode(Json.encode({k = {1, 2, {z = 'deep'}}})) return r.k[3].z") == "deep");
}

TEST_CASE("json: decode is strict and returns nil plus a reason")
{
    Rig rig;
    for (char const* bad : {"", "{", "[1,]", "{\"a\":}", "{'a':1}", "01", "-", "1.", "1e", "[1 2]", "{\"a\" 1}", "nul",
                            "\"unterminated", "\"bad\\q\"", "\"\\ud800\"", "\"\\udc00\"", "\"\\u12\"", "[1] x", "\"tab\there\"",
                            "[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]]", "NaN", "Infinity",
                            "{\"a\":1,}", "\"\\xc3\""})
    {
        INFO(bad);
        auto r = rig.run(std::string("local v, err = Json.decode([==[") + bad + "]==]) return tostring(v) .. '|' .. tostring(err ~= nil)");
        REQUIRE(r.ok);
        CHECK(r.result == "nil|true");
    }
}

TEST_CASE("json: the toolkit reply line round trips")
{
    Rig rig;
    std::string line = rig.ok("return 'TTJSON ' .. Json.encode({cmd = 'selftest', ok = true, id = 42, units = {{guid = 'Creature-0-1', x = 1.25}}})");
    CHECK(line == R"(TTJSON {"cmd":"selftest","id":42,"ok":true,"units":[{"guid":"Creature-0-1","x":1.25}]})");
}
