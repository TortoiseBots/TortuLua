// Guards: Lua sending SQL that makes the core abort (unknown table/column, parse error) or that is not a plain SELECT.
// At the pinned core MySQLConnection calls ABORT() on those errors, so every refusal must happen before any query.
#include "Support.h"

#include "engine/SqlGuard.h"

#include <limits>

using namespace fltest;

namespace
{
fl::SelectRequest req(std::string table, std::vector<std::string> cols)
{
    fl::SelectRequest r;
    r.db = "world";
    r.table = std::move(table);
    r.columns = std::move(cols);
    return r;
}
}  // namespace

TEST_CASE("sql guard: builds a plain, quoted SELECT from checked names")
{
    FakeHost host;
    fl::SqlGuard g(host);
    auto r = req("creature_template", {"entry", "NAME"});
    r.where.push_back({"minlevel", fl::WhereValue(int64_t(60))});
    r.where.push_back({"faction", fl::WhereValue(std::string("O'Brien"))});
    r.order = "entry";
    r.desc = true;
    r.limit = 5;
    auto b = g.build(r);
    REQUIRE(b.error == fl::SqlError::None);
    CHECK(b.sql == "SELECT /*+ MAX_EXECUTION_TIME(500) */ `entry`,`name` FROM `creature_template` WHERE `faction` = 'O\\'Brien' AND `minlevel` = 60 "
                   "ORDER BY `entry` DESC LIMIT 5");
    CHECK(b.maxRows == 5);
    CHECK(host.queries.empty());   // the guard never sends anything itself
}

TEST_CASE("sql guard: limits default to what the spec says and clamp at 500")
{
    FakeHost host;
    fl::SqlGuard g(host);
    auto r = req("creature_template", {"entry"});
    CHECK(g.build(r).sql.find("LIMIT 100") != std::string::npos);
    r.limit = 100000;
    auto b = g.build(r);
    CHECK(b.sql.find("LIMIT 500") != std::string::npos);
    CHECK(b.maxRows == 500);
    r.limit = 0;
    CHECK(g.build(r).error == fl::SqlError::BadValue);
    r.limit = -3;
    CHECK(g.build(r).error == fl::SqlError::BadValue);
}

TEST_CASE("sql guard: every refusal happens before a query and names the reason")
{
    FakeHost host;
    fl::SqlGuard g(host);
    struct Case
    {
        char const* what;
        fl::SelectRequest r;
        fl::SqlError expect;
    };
    auto withCol = [](std::string col) { return req("creature_template", {std::move(col)}); };
    std::vector<Case> cases;
    cases.push_back({"unknown table", req("no_such_table", {"entry"}), fl::SqlError::NoTable});
    cases.push_back({"unknown column", withCol("no_such_column"), fl::SqlError::NoColumn});
    cases.push_back({"known column of another table", req("creature", {"minlevel"}), fl::SqlError::NoColumn});
    cases.push_back({"backtick", withCol("entry`"), fl::SqlError::BadName});
    cases.push_back({"backtick table", req("creature_template`", {"entry"}), fl::SqlError::BadName});
    cases.push_back({"injected column", withCol("entry, (SELECT 1)"), fl::SqlError::BadName});
    cases.push_back({"injected table", req("creature_template; DROP TABLE creature", {"entry"}), fl::SqlError::BadName});
    cases.push_back({"star", withCol("*"), fl::SqlError::BadName});
    cases.push_back({"function call", withCol("count(*)"), fl::SqlError::BadName});
    cases.push_back({"comment", withCol("entry--"), fl::SqlError::BadName});
    cases.push_back({"space", withCol("en try"), fl::SqlError::BadName});
    cases.push_back({"unicode", withCol("entr\xc3\xa9"), fl::SqlError::BadName});
    cases.push_back({"empty name", withCol(""), fl::SqlError::BadName});
    cases.push_back({"too long", withCol(std::string(65, 'a')), fl::SqlError::BadName});
    cases.push_back({"dotted name", req("world.creature_template", {"entry"}), fl::SqlError::BadName});
    cases.push_back({"newline", withCol("entry\n"), fl::SqlError::BadName});
    cases.push_back({"NUL in a name", withCol(std::string("entry\0x", 7)), fl::SqlError::BadName});
    cases.push_back({"no columns", req("creature_template", {}), fl::SqlError::BadValue});
    {
        auto r = withCol("entry");
        r.db = "mysql";
        cases.push_back({"bad database", r, fl::SqlError::BadDb});
        r.db = "information_schema";
        cases.push_back({"information_schema itself", r, fl::SqlError::BadDb});
        r.db = "world`";
        cases.push_back({"backtick database", r, fl::SqlError::BadDb});
    }
    {
        auto r = withCol("entry");
        r.where.push_back({"nope", fl::WhereValue(int64_t(1))});
        cases.push_back({"unknown where column", r, fl::SqlError::NoColumn});
        r.where.clear();
        r.where.push_back({"entry` OR 1=1 --", fl::WhereValue(int64_t(1))});
        cases.push_back({"injected where column", r, fl::SqlError::BadName});
        r.where.clear();
        r.where.push_back({"entry", fl::WhereValue(std::numeric_limits<double>::quiet_NaN())});
        cases.push_back({"NaN value", r, fl::SqlError::BadValue});
        r.where.clear();
        r.where.push_back({"entry", fl::WhereValue(std::numeric_limits<double>::infinity())});
        cases.push_back({"infinite value", r, fl::SqlError::BadValue});
        r.where.clear();
        r.where.push_back({"name", fl::WhereValue(std::string("a\0b", 3))});
        cases.push_back({"NUL in a value", r, fl::SqlError::BadValue});
        r.where.clear();
        r.where.push_back({"name", fl::WhereValue(std::string(5000, 'x'))});
        cases.push_back({"huge value", r, fl::SqlError::BadValue});
        r.where.clear();
        for (int i = 0; i < 17; ++i)
            r.where.push_back({"entry", fl::WhereValue(int64_t(i))});
        cases.push_back({"too many conditions", r, fl::SqlError::BadValue});
    }
    {
        auto r = withCol("entry");
        r.order = "nope";
        cases.push_back({"unknown order column", r, fl::SqlError::NoColumn});
        r.order = "entry; DROP TABLE x";
        cases.push_back({"injected order", r, fl::SqlError::BadName});
    }
    {
        auto r = withCol("entry");
        r.db = "auth";   // no schema for it in the fake world
        cases.push_back({"database without schema", r, fl::SqlError::BadDb});
    }
    for (auto const& c : cases)
    {
        INFO(c.what);
        auto b = g.build(c.r);
        CHECK(b.error == c.expect);
        CHECK(b.sql.empty());
    }
    CHECK(host.queries.empty());
}

TEST_CASE("sql guard: a host whose escape lets a quote through makes the request fail")
{
    FakeHost host;
    host.badEscape = true;
    fl::SqlGuard g(host);
    auto r = req("creature_template", {"entry"});
    r.where.push_back({"name", fl::WhereValue(std::string("x' OR '1'='1"))});
    CHECK(g.build(r).error == fl::SqlError::BadValue);
    r.where.clear();
    r.where.push_back({"name", fl::WhereValue(std::string("fine"))});
    CHECK(g.build(r).error == fl::SqlError::None);   // plain text needs no escaping
}

TEST_CASE("sql guard: a hostile string value is only ever escaped inside quotes")
{
    FakeHost host;
    fl::SqlGuard g(host);
    auto r = req("creature_template", {"entry"});
    r.where.push_back({"name", fl::WhereValue(std::string("'; DROP TABLE creature; --"))});
    auto b = g.build(r);
    REQUIRE(b.error == fl::SqlError::None);
    CHECK(b.sql.find("= '\\'; DROP TABLE creature; --'") != std::string::npos);
    r.where.clear();
    r.where.push_back({"name", fl::WhereValue(std::string("trailing backslash\\"))});
    b = g.build(r);
    REQUIRE(b.error == fl::SqlError::None);
    CHECK(b.sql.find("'trailing backslash\\\\'") != std::string::npos);
}

TEST_CASE("Db.select from Lua: rows come back, refusals are nil plus a reason, nothing is sent on refusal")
{
    Rig rig;
    fl::DbRows rows;
    rows.columns = {"entry", "name", "minlevel", "faction"};
    rows.rows = {{fl::DbValue(int64_t(1)), fl::DbValue(std::string("Boar")), fl::DbValue(2.5), fl::DbValue()},
                 {fl::DbValue(int64_t(2)), fl::DbValue(std::string("Wolf")), fl::DbValue(int64_t(3)), fl::DbValue(int64_t(14))}};
    rig.host.results["SELECT /*+ MAX_EXECUTION_TIME(500) */ `entry`,`name`,`minlevel`,`faction` FROM `creature_template` WHERE `minlevel` = 3 LIMIT 100"] = rows;
    // the answer arrives in the callback on a later tick, never inside the call
    CHECK(rig.ok("got = 'none' Db.select('world', 'creature_template', {'entry', 'name', 'minlevel', 'faction'}, {minlevel = 3}, "
                 "function(rows) got = rows end) return got") == "none");
    rig.e().tick();
    CHECK(rig.ok("local r = got return #r .. r[1].name .. r[1].minlevel .. tostring(r[1].faction) .. r[2].entry .. r[2].faction") ==
          "2Boar2.5nil214");
    CHECK(rig.host.queries.size() == 1);

    struct Refusal
    {
        char const* code;
        char const* call;
    };
    for (Refusal const& r : std::vector<Refusal>{
             {"no_table", "Db.select('world', 'nope', {'entry'}, function() end)"},
             {"no_column", "Db.select('world', 'creature_template', {'nope'}, function() end)"},
             {"no_column", "Db.select('world', 'creature_template', {'entry'}, {nope = 1}, function() end)"},
             {"bad_name", "Db.select('world', 'creature_template`', {'entry'}, function() end)"},
             {"bad_name", "Db.select('world', 'creature_template', {'entry; DROP TABLE x'}, function() end)"},
             {"bad_name", "Db.select('world', 'creature_template', {'entry'}, {['a b'] = 1}, function() end)"},
             {"bad_name", "Db.select('world', 'creature_template', {'entry'}, {1, 2}, function() end)"},
             {"bad_value", "Db.select('world', 'creature_template', {}, function() end)"},
             {"bad_value", "Db.select('world', 'creature_template', 'entry', function() end)"},
             {"bad_value", "Db.select('world', 'creature_template', {'entry'}, {entry = {}}, function() end)"},
             {"bad_value", "Db.select('world', 'creature_template', {'entry'}, {entry = print}, function() end)"},
             {"bad_value", "Db.select('world', 'creature_template', {'entry'}, {entry = 0/0}, function() end)"},
             {"bad_value", "Db.select('world', 'creature_template', {'entry'}, nil, {limit = 0}, function() end)"},
             {"bad_value", "Db.select('world', 'creature_template', {'entry'}, nil, {limit = 1.5}, function() end)"},
             {"bad_value", "Db.select('world', 'creature_template', {'entry'}, nil, {limit = 'x'}, function() end)"},
             {"bad_value", "Db.select(1, 2, 3, function() end)"},
             {"bad_db", "Db.select('mysql', 'user', {'user'}, function() end)"},
             {"bad_db", "Db.select('auth', 'account', {'id'}, function() end)"},
             {"no_column", "Db.select('world', 'creature_template', {'entry'}, nil, {order = 'nope'}, function() end)"},
         })
    {
        INFO(r.call);
        size_t before = rig.host.queries.size();
        CHECK(rig.ok(std::string("local r, why = ") + r.call + " return tostring(r) .. '|' .. tostring(why)") ==
              std::string("nil|") + r.code);
        CHECK(rig.host.queries.size() == before);
        CHECK(rig.host.queries.size() == before);   // refused without sending a query
    }
}

TEST_CASE("Db: there is no raw SQL entry point and the guard is the only way in")
{
    Rig rig;
    CHECK(rig.ok("local n = 0 for k in pairs(Db) do n = n + 1 end return n .. '|' .. tostring(Db.query) .. tostring(Db.execute) .. tostring(Db.raw)") ==
          "2|nilnilnil");
    CHECK(rig.ok("return tostring(Db.select('world', 'creature_template', {'entry'}, nil, {limit = 100000}, function() end) ~= nil)") == "true");
    REQUIRE(rig.host.queries.size() == 1);
    CHECK(rig.host.queries[0].rfind("SELECT ", 0) == 0);
    CHECK(rig.host.queries[0].find("LIMIT 500") != std::string::npos);
}

TEST_CASE("Db.select: case-insensitive columns, ordering, and a failing database")
{
    Rig rig;
    rig.ok("Db.select('world', 'creature_template', {'ENTRY', 'Name'}, {MinLevel = 1}, {order = 'ENTRY', desc = true, limit = 3}, "
           "function() end)");
    REQUIRE(rig.host.queries.size() == 1);
    CHECK(rig.host.queries[0] == "SELECT /*+ MAX_EXECUTION_TIME(500) */ `entry`,`name` FROM `creature_template` WHERE `minlevel` = 1 ORDER BY `entry` DESC LIMIT 3");
    rig.host.failSelect = true;
    CHECK(rig.dbSelect("'world', 'creature_template', {'entry'}") == "nil|query_failed");
    CHECK(rig.host.logContains("query_failed"));
}

TEST_CASE("Db.has: table and column checks against the snapshot")
{
    Rig rig;
    CHECK(rig.ok("return tostring(Db.has('world', 'creature_template')) .. tostring(Db.has('world', 'nope')) .. "
                 "tostring(Db.has('world', 'creature_template', 'entry')) .. tostring(Db.has('world', 'creature_template', 'nope'))") ==
          "truefalsetruefalse");
    CHECK(rig.ok("local r, why = Db.has('world', 'bad name') return tostring(r) .. why") == "nilbad_name");
    CHECK(rig.ok("local r, why = Db.has('mysql', 'user') return tostring(r) .. why") == "nilbad_db");
    CHECK(rig.host.queries.empty());
}

TEST_CASE("sql guard: the schema is asked once per database until reload")
{
    Rig rig;
    for (int i = 0; i < 5; ++i)
        rig.ok("Db.select('world', 'creature_template', {'entry'}, function() end)");
    rig.ok("Db.select('characters', 'characters', {'guid'}, function() end)");
    CHECK(rig.host.schemaCalls == 2);
}
