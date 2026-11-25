#include "ForeverHost.h"

#include "LuaDb.h"

#include "GameAccess.h"

#include "engine/Text.h"

#include "Battleground.h"
#include "Chat.h"
#include "Corpse.h"
#include "Creature.h"
#include "DatabaseEnv.h"
#include "GameObject.h"
#include "GameTime.h"
#include "GroupMgr.h"
#include "GuildMgr.h"
#include "Item.h"
#include "ObjectMgr.h"
#include "Log.h"
#include "Map.h"
#include "Player.h"
#include "QueryResult.h"
#include "QueryCallback.h"
#include "RBAC.h"
#include "RealmList.h"
#include "Unit.h"

#include <chrono>
#include <deque>
#include <optional>
#include <list>
#include <set>
#include <ctime>
#include <exception>

namespace flh
{
namespace
{
// The pools differ in type; this picks the right one for a database name and runs `f(pool)`.
template <typename F>
bool withPool(std::string const& db, F&& f)
{
    if (db == "world")
        return f(WorldDatabase);
    if (db == "characters")
        return f(CharacterDatabase);
    if (db == "auth")
        return f(LoginDatabase);
    if (db == "hotfixes")
        return f(HotfixDatabase);
    return false;
}

fl::DbValue readField(Field const& f, DatabaseFieldTypes type)
{
    if (f.IsNull())
        return std::monostate{};
    switch (type)
    {
        case DatabaseFieldTypes::Null:
            return std::monostate{};
        case DatabaseFieldTypes::UInt8: return int64_t(f.GetUInt8());
        case DatabaseFieldTypes::Int8: return int64_t(f.GetInt8());
        case DatabaseFieldTypes::UInt16: return int64_t(f.GetUInt16());
        case DatabaseFieldTypes::Int16: return int64_t(f.GetInt16());
        case DatabaseFieldTypes::UInt32: return int64_t(f.GetUInt32());
        case DatabaseFieldTypes::Int32: return int64_t(f.GetInt32());
        case DatabaseFieldTypes::UInt64: return int64_t(f.GetUInt64());
        case DatabaseFieldTypes::Int64: return int64_t(f.GetInt64());
        case DatabaseFieldTypes::Float: return double(f.GetFloat());
        case DatabaseFieldTypes::Double:
        case DatabaseFieldTypes::Decimal: return f.GetDouble();
        case DatabaseFieldTypes::Date:
        case DatabaseFieldTypes::Time:
        case DatabaseFieldTypes::Binary:
            break;
    }
    return std::string(f.GetStringView());
}
}  // namespace

struct SchemaLoad
{
    // QueryCallback owns a std::future: it runs its callback when the query has an answer, and must live until then.
    std::list<QueryCallback> schemas;
    struct Select
    {
        uint64_t id = 0;
        QueryCallback cb;
        size_t maxRows = 0;
        size_t maxBytes = 0;
    };
    std::list<Select> selects;
    struct Done
    {
        uint64_t id = 0;
        fl::DbRows rows;
        std::string error;
    };
    std::deque<Done> done;
    std::map<std::string, std::shared_ptr<fl::SchemaSnapshot const>> snapshots;
    std::set<std::string> loading;
};

uint64_t ForeverHost::nowMs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
}

int64_t ForeverHost::unixTime() { return int64_t(std::time(nullptr)); }
int64_t ForeverHost::gameTime() { return int64_t(GameTime::GetGameTime()); }

void ForeverHost::log(fl::LogLevel level, std::string const& text)
{
    switch (level)
    {
        case fl::LogLevel::Debug: TC_LOG_DEBUG("forever.lua", "{}", text); break;
        case fl::LogLevel::Info: TC_LOG_INFO("forever.lua", "{}", text); break;
        case fl::LogLevel::Warn: TC_LOG_WARN("forever.lua", "{}", text); break;
        case fl::LogLevel::Error: TC_LOG_ERROR("forever.lua", "{}", text); break;
    }
}

std::string ForeverHost::guidToString(fl::Guid const& guid) { return toGuid(guid).ToString(); }

bool ForeverHost::lookup(fl::HandleData const& h, fl::ObjectInfo* out)
{
    fl::ObjectInfo info;
    switch (h.kind)
    {
        case fl::ObjKind::Player:
        {
            Player* p = findPlayer(h);
            if (!p)
                return false;
            info.name = p->GetName();
            info.entry = 0;
            info.alive = p->IsAlive();
            info.x = p->GetPositionX();
            info.y = p->GetPositionY();
            info.z = p->GetPositionZ();
            info.o = p->GetOrientation();
            break;
        }
        case fl::ObjKind::Creature:
        {
            Creature* c = findCreature(h);
            if (!c)
                return false;
            info.name = c->GetName();
            info.entry = c->GetEntry();
            info.alive = c->IsAlive();
            info.x = c->GetPositionX();
            info.y = c->GetPositionY();
            info.z = c->GetPositionZ();
            info.o = c->GetOrientation();
            break;
        }
        case fl::ObjKind::GameObject:
        {
            GameObject* g = findGameObject(h);
            if (!g)
                return false;
            info.name = g->GetName();
            info.entry = g->GetEntry();
            info.alive = g->isSpawned();
            info.x = g->GetPositionX();
            info.y = g->GetPositionY();
            info.z = g->GetPositionZ();
            info.o = g->GetOrientation();
            break;
        }
        case fl::ObjKind::Map:
        {
            Map* m = findMap(h.mapId, h.instanceId);
            if (!m)
                return false;
            info.name = m->GetMapName();
            info.entry = m->GetId();
            break;
        }
        case fl::ObjKind::BattleGround:
        {
            Map* m = findMap(h.mapId, h.instanceId);
            BattlegroundMap* bm = m ? m->ToBattlegroundMap() : nullptr;
            Battleground* bg = bm ? bm->GetBG() : nullptr;
            if (!bg)
                return false;
            info.name = bg->GetName();
            info.entry = static_cast<uint32>(bg->GetTypeID());
            break;
        }
        case fl::ObjKind::Corpse:
        {
            Corpse* c = findCorpse(h);
            if (!c)
                return false;
            info.name = c->GetName();
            info.entry = 0;
            info.alive = false;
            info.x = c->GetPositionX();
            info.y = c->GetPositionY();
            info.z = c->GetPositionZ();
            info.o = c->GetOrientation();
            break;
        }
        case fl::ObjKind::Item:
        {
            Item* item = findItem(h);
            if (!item)
                return false;
            info.entry = item->GetEntry();
            break;
        }
        case fl::ObjKind::Quest:
            if (!sObjectMgr->GetQuestTemplate(static_cast<uint32>(h.guid.lo)))
                return false;
            info.entry = static_cast<uint32_t>(h.guid.lo);
            break;
        case fl::ObjKind::Guild:
            if (!sGuildMgr->GetGuildById(static_cast<ObjectGuid::LowType>(h.guid.lo)))
                return false;
            break;
        case fl::ObjKind::Group:
            if (!sGroupMgr->GetGroupByGUID(toGuid(h.guid)))
                return false;
            break;
        default:
            return false;   // spells, auras, queries and packets are not world objects: nothing to find again
    }
    if (out)
        *out = std::move(info);
    return true;
}

bool ForeverHost::despawn(fl::HandleData const& h)
{
    if (h.kind == fl::ObjKind::Creature)
    {
        Creature* c = findCreature(h);
        if (!c)
            return false;
        c->DespawnOrUnsummon();
        return true;
    }
    if (h.kind == fl::ObjKind::GameObject)
    {
        GameObject* g = findGameObject(h);
        if (!g)
            return false;
        g->Delete();
        return true;
    }
    return false;
}

// H1 (E4/E5 review): `_loads` (schema loads, select callbacks, finished results, snapshots) has one owner, the world
// thread. The engine only calls these from the world state (Db.select exists in test mode only, which has one state);
// a call from any other thread is refused here as well, never run on the shared containers.
bool ForeverHost::loadsOwner(char const* what)
{
    if (flh::worldThread())
        return true;
    if (!_loadsOffThreadLogged.exchange(true))
        TC_LOG_ERROR("forever.lua", "ForeverLua: {} refused off the world thread (the database structures are the world thread's)",
                     what);
    return false;
}

std::shared_ptr<fl::SchemaSnapshot const> ForeverHost::schema(std::string const& db)
{
    if (!loadsOwner("schema"))
        return nullptr;
    // A cached snapshot only: never a query on the world thread. The first ask starts the load and answers nullptr, so
    // the caller fails closed (Db.select reports "no_schema") and a later call finds the snapshot (review: schema
    // loading must not block the world thread).
    auto it = _loads->snapshots.find(db);
    if (it != _loads->snapshots.end())
        return it->second;
    startSchemaLoad(db);
    return nullptr;
}

std::string ForeverHost::escape(std::string const& db, std::string const& text)
{
    std::string s = text;
    bool ok = withPool(db, [&](auto& pool) { pool.EscapeString(s); return true; });
    if (!ok)
        return std::string();
    return s;
}

void ForeverHost::startSchemaLoad(std::string const& db)
{
    if (!_loads->loading.insert(db).second)
        return;   // already in flight
    bool posted = withPool(db, [&](auto& pool)
    {
        QueryCallback cb = pool.AsyncQuery("SELECT TABLE_NAME, COLUMN_NAME FROM information_schema.COLUMNS "
                                           "WHERE TABLE_SCHEMA = DATABASE() ORDER BY TABLE_NAME, ORDINAL_POSITION");
        cb.WithCallback([this, db](QueryResult res) { finishSchemaLoad(db, res); });
        _loads->schemas.push_back(std::move(cb));
        return true;
    });
    if (!posted)
    {
        _loads->loading.erase(db);
        TC_LOG_ERROR("forever.lua", "ForeverLua: no database pool for the '{}' schema", db);
    }
}

void ForeverHost::finishSchemaLoad(std::string const& db, std::shared_ptr<ResultSet> const& res)
{
    _loads->loading.erase(db);
    if (!res)
    {
        // The database is not reachable right now: a later schema() asks again instead of caching the failure.
        TC_LOG_WARN("forever.lua", "ForeverLua: the '{}' schema snapshot could not be read", db);
        return;
    }
    auto snap = std::make_shared<fl::SchemaSnapshot>();
    std::string table;
    std::vector<std::string> cols;
    do
    {
        Field* f = res->Fetch();
        std::string t = f[0].GetString();
        if (t != table)
        {
            if (!table.empty())
                snap->addTable(table, cols);
            table = std::move(t);
            cols.clear();
        }
        cols.push_back(f[1].GetString());
    } while (res->NextRow());
    if (!table.empty())
        snap->addTable(table, cols);
    _loads->snapshots[db] = snap;
}

bool ForeverHost::selectAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes,
                              uint64_t id, std::string& error)
{
    if (!loadsOwner("Db.select"))
    {
        error = "busy";
        return false;
    }
    if (_loads->selects.size() >= 64)
    {
        error = "busy";
        return false;
    }
    std::optional<QueryCallback> cb;   // QueryCallback has no default constructor: it owns a future
    bool posted = withPool(db, [&](auto& pool)
    {
        cb.emplace(pool.AsyncQuery(sql.c_str()));
        cb->WithCallback([this, id, maxRows, maxBytes](QueryResult res) { collectSelect(id, res, maxRows, maxBytes); });
        return true;
    });
    if (!posted)
    {
        error = "bad_db";
        return false;
    }
    _loads->selects.push_back(SchemaLoad::Select{id, std::move(*cb), maxRows, maxBytes});
    return true;
}

void ForeverHost::collectSelect(uint64_t id, std::shared_ptr<ResultSet> const& res, size_t maxRows, size_t maxBytes)
{
    // Runs on the world thread inside pollDb(): the driver has the result buffered, only the copy into DbRows is left.
    SchemaLoad::Done done;
    done.id = id;
    try
    {
        if (res)
        {
            uint32 n = res->GetFieldCount();
            std::vector<DatabaseFieldTypes> types(n);
            for (uint32 i = 0; i < n; ++i)
            {
                auto const& meta = res->GetFieldMetadata(i);
                done.rows.columns.emplace_back(meta.Alias && *meta.Alias ? meta.Alias : (meta.Name ? meta.Name : ""));
                types[i] = meta.Type;
            }
            // The byte budget is enforced while the rows are copied: the Lua memory cap cannot see this C++ copy.
            size_t bytes = 0;
            do
            {
                Field* f = res->Fetch();
                std::vector<fl::DbValue> row;
                row.reserve(n);
                for (uint32 i = 0; i < n; ++i)
                {
                    fl::DbValue v = readField(f[i], types[i]);
                    if (auto const* str = std::get_if<std::string>(&v))
                    {
                        bytes += str->size();
                        if (maxBytes && bytes > maxBytes)
                        {
                            done.rows.tooLarge = true;
                            break;
                        }
                    }
                    row.push_back(std::move(v));
                }
                if (done.rows.tooLarge)
                    break;
                done.rows.rows.push_back(std::move(row));
            } while (done.rows.rows.size() < maxRows && res->NextRow());
            if (done.rows.tooLarge)
                done.rows.rows.clear();
        }
    }
    catch (std::exception const& e)
    {
        done.rows = fl::DbRows{};
        done.error = e.what();
    }
    _loads->done.push_back(std::move(done));
}

void ForeverHost::pollDb()
{
    if (!loadsOwner("pollDb"))
        return;
    // Every callback runs here, on the world thread: QueryCallback::InvokeIfReady() only fires when its answer is in.
    for (auto it = _loads->schemas.begin(); it != _loads->schemas.end();)
    {
        if (it->InvokeIfReady())
            it = _loads->schemas.erase(it);
        else
            ++it;
    }
    for (auto it = _loads->selects.begin(); it != _loads->selects.end();)
    {
        if (it->cb.InvokeIfReady())
            it = _loads->selects.erase(it);
        else
            ++it;
    }
}

bool ForeverHost::takeDbResult(uint64_t& id, fl::DbRows& out, std::string& error)
{
    if (!loadsOwner("takeDbResult"))
        return false;
    if (_loads->done.empty())
        return false;
    SchemaLoad::Done done = std::move(_loads->done.front());
    _loads->done.pop_front();
    id = done.id;
    out = std::move(done.rows);
    error = std::move(done.error);
    return true;
}

void ForeverHost::cancelSelect(uint64_t id)
{
    if (!loadsOwner("cancelSelect"))
        return;
    for (auto it = _loads->selects.begin(); it != _loads->selects.end(); ++it)
        if (it->id == id)
        {
            _loads->selects.erase(it);   // the callback carries the id: a late answer is not collected
            break;
        }
    for (auto it = _loads->done.begin(); it != _loads->done.end(); ++it)
        if (it->id == id)
        {
            _loads->done.erase(it);
            break;
        }
}

rbac::RBACData const* ForeverHost::bridgePermissions()
{
    if (!_rbac)
    {
        auto data = std::make_unique<rbac::RBACData>(0, "ForeverLua bridge", sRealmList->GetCurrentRealmId().Realm,
                                                     static_cast<uint8>(_bridgeSecurity));
        data->LoadFromDB();   // account 0 has no rows: the default permissions of the level decide
        _rbac = std::move(data);
    }
    return _rbac.get();
}

namespace
{
// A console handler whose rights are the configured bridge level, not console rights: every command's RBAC
// permission is checked against that level. The output capture is the same as CliHandler's.
class BridgeHandler final : public CliHandler
{
public:
    BridgeHandler(void* arg, Print* print, rbac::RBACData const* rbac) : CliHandler(arg, print), _rbac(rbac) { }

    bool HasPermission(uint32 permission) const override
    {
        return _rbac && _rbac->HasPermission(permission);
    }

private:
    rbac::RBACData const* _rbac;
};
}  // namespace

bool ForeverHost::runGmLine(std::string const& line, std::vector<std::string>& out)
{
    struct Capture
    {
        std::vector<std::string>* lines;
    } cap{&out};
    BridgeHandler handler(&cap, [](void* arg, std::string_view text)
    {
        auto* c = static_cast<Capture*>(arg);
        if (c->lines->size() < 1000)
            fl::splitLines(text, *c->lines);
    }, bridgePermissions());
    try
    {
        return handler.ParseCommands(line);
    }
    catch (std::exception const& e)
    {
        out.push_back(std::string("error: ") + e.what());
        return false;
    }
}
ForeverHost::ForeverHost(int bridgeSecurity, uint64_t syncDbTimeoutMs)
    : _bridgeSecurity(bridgeSecurity), _dbTimeoutMs(syncDbTimeoutMs), _loads(std::make_unique<SchemaLoad>())
{
    // Posted, not queried: the module can start on the world thread without waiting for three information_schema reads.
    for (char const* db : {"world", "characters", "hotfixes"})
        startSchemaLoad(db);
}

ForeverHost::~ForeverHost() = default;
}  // namespace flh
