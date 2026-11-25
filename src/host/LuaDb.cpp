#include "LuaDb.h"

#include "Config.h"
#include "Log.h"

#include <mysql.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace flh
{
namespace
{
std::vector<std::string> split(char const* text, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char const* p = text; *p; ++p)
    {
        if (*p == sep)
        {
            out.push_back(cur);
            cur.clear();
        }
        else
            cur += *p;
    }
    out.push_back(cur);
    return out;
}

// The core's "host;port;user;pass;db[;ssl]" connection string.
struct ConnInfo
{
    std::string host, port, user, password, database, ssl;
};

ConnInfo parse(std::string const& text)
{
    ConnInfo c;
    std::vector<std::string> t = split(text.c_str(), ';');
    if (t.size() >= 5)
    {
        c.host = t[0];
        c.port = t[1];
        c.user = t[2];
        c.password = t[3];
        c.database = t[4];
        if (t.size() >= 6)
            c.ssl = t[5];
    }
    return c;
}
}  // namespace

namespace
{
constexpr unsigned kServerGone = 2006;   // CR_SERVER_GONE_ERROR
constexpr unsigned kServerLost = 2013;   // CR_SERVER_LOST
constexpr unsigned long kMaxPacket = 4ul * 1024 * 1024;   // no single row (or statement) larger than this

uint64_t steadyMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()).count());
}

// The driver's unbuffered result as a RowSource: one row at a time, nothing buffered by us.
class MysqlSource final : public RowSource
{
public:
    explicit MysqlSource(MYSQL_RES* res) : _res(res), _fields(mysql_num_fields(res)), _defs(mysql_fetch_fields(res)) { }
    std::vector<std::string> columns() override
    {
        std::vector<std::string> out;
        out.reserve(_fields);
        for (unsigned int i = 0; i < _fields; ++i)
            out.emplace_back(_defs[i].name);
        return out;
    }
    bool next(std::vector<Cell>& row) override
    {
        MYSQL_ROW r = mysql_fetch_row(_res);
        if (!r)
            return false;
        unsigned long* lengths = mysql_fetch_lengths(_res);
        row.assign(_fields, Cell());
        for (unsigned int i = 0; i < _fields; ++i)
        {
            Cell& c = row[i];
            if (!r[i])
                continue;
            switch (_defs[i].type)
            {
                case MYSQL_TYPE_TINY:
                case MYSQL_TYPE_SHORT:
                case MYSQL_TYPE_INT24:
                case MYSQL_TYPE_LONG:
                case MYSQL_TYPE_LONGLONG:
                case MYSQL_TYPE_YEAR:
                    c.kind = Cell::Int;
                    c.i = static_cast<int64_t>(std::strtoll(r[i], nullptr, 10));
                    break;
                case MYSQL_TYPE_FLOAT:
                case MYSQL_TYPE_DOUBLE:
                case MYSQL_TYPE_DECIMAL:
                case MYSQL_TYPE_NEWDECIMAL:
                    c.kind = Cell::Double;
                    c.d = std::strtod(r[i], nullptr);
                    break;
                default:
                    c.kind = Cell::Text;
                    c.text = std::string_view(r[i], lengths ? lengths[i] : std::strlen(r[i]));
                    break;
            }
        }
        return true;
    }

private:
    MYSQL_RES* _res;
    unsigned int _fields;
    MYSQL_FIELD* _defs;
};
}  // namespace

LuaDb::LuaDb(uint64_t timeoutMs) : _timeoutMs(timeoutMs)
{
    _info[World] = sConfigMgr->GetStringDefault("WorldDatabaseInfo", "");
    _info[Characters] = sConfigMgr->GetStringDefault("CharacterDatabaseInfo", "");
    _info[Auth] = sConfigMgr->GetStringDefault("LoginDatabaseInfo", "");
}

LuaDb::~LuaDb()
{
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        _stop = true;
    }
    _queueCv.notify_all();
    if (_worker.joinable())
        _worker.join();
    {
        std::lock_guard<std::mutex> lock(_asyncMutex);
        _asyncStop = true;
    }
    _asyncCv.notify_all();
    if (_asyncWorker.joinable())
        _asyncWorker.join();
    closeAll();
}

int LuaDb::whichOf(std::string const& db)
{
    if (db == "world")
        return World;
    if (db == "characters")
        return Characters;
    if (db == "auth")
        return Auth;
    return -1;
}

void LuaDb::drop(Conn& c)
{
    if (c.handle)
        mysql_close(c.handle);
    c.handle = nullptr;
    c.selectLimit = 0;
    c.policy.closedOnPurpose();   // a lost connection is reopened at once; only failed connects back off
}

MYSQL* LuaDb::connect(Conn& c, int which, std::string& error)
{
    if (c.handle)
        return c.handle;
    uint64_t const now = steadyMs();
    if (!c.policy.mayAttempt(now))
    {
        error = "the database connection is not available (next attempt in " +
                std::to_string((c.policy.retryAtMs() - now + 999) / 1000) + " s)";
        return nullptr;
    }
    if (_info[which].empty())
    {
        error = "the connection string is empty";
        c.policy.failed(now);
        return nullptr;
    }
    ConnInfo ci = parse(_info[which]);
    MYSQL* m = mysql_init(nullptr);
    if (!m)
    {
        error = "mysql_init failed";
        return nullptr;
    }
    mysql_options(m, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    unsigned int timeout = static_cast<unsigned int>(std::max<uint64_t>(1, (_timeoutMs + 999) / 1000));
    mysql_options(m, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);   // an unreachable host must not hold a world/map thread
    mysql_options(m, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(m, MYSQL_OPT_WRITE_TIMEOUT, &timeout);
    unsigned long maxPacket = kMaxPacket;
    mysql_options(m, MYSQL_OPT_MAX_ALLOWED_PACKET, &maxPacket);
    int port = 0;
    char const* socket = nullptr;
    if (ci.host != ".")
        port = std::atoi(ci.port.c_str());
    else
    {
        ci.host = "localhost";
        socket = ci.port.c_str();
        unsigned int protocol = MYSQL_PROTOCOL_SOCKET;
        mysql_options(m, MYSQL_OPT_PROTOCOL, &protocol);
    }
    if (!ci.ssl.empty())
    {
#if !defined(MARIADB_VERSION_ID) && MYSQL_VERSION_ID >= 80000
        mysql_ssl_mode mode = ci.ssl == "ssl" ? SSL_MODE_REQUIRED : SSL_MODE_DISABLED;
        mysql_options(m, MYSQL_OPT_SSL_MODE, &mode);
#else
        unsigned int ssl = ci.ssl == "ssl" ? 1u : 0u;
        mysql_options(m, MYSQL_OPT_SSL_ENFORCE, &ssl);
#endif
    }
    if (!mysql_real_connect(m, ci.host.c_str(), ci.user.c_str(), ci.password.c_str(), ci.database.c_str(), port, socket,
                            0))
    {
        error = std::string("connect failed: ") + mysql_error(m);
        mysql_close(m);
        c.policy.failed(now);   // the next attempt waits 1 s, 2 s, 4 s ... up to a minute
        return nullptr;
    }
    c.policy.succeeded();
    c.handle = m;
    c.selectLimit = 0;
    return m;
}

bool LuaDb::run(Conn& c, int which, std::string const& sql, bool retryLost, std::string& error)
{
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        MYSQL* m = connect(c, which, error);
        if (!m)
            return false;
        if (!mysql_real_query(m, sql.data(), static_cast<unsigned long>(sql.size())))
            return true;
        unsigned const err = mysql_errno(m);
        bool const lost = err == kServerGone || (retryLost && err == kServerLost);
        error = mysql_error(m);
        if (!lost || attempt == 1)
        {
            if (err == kServerGone || err == kServerLost)
                drop(c);   // a dead connection is not kept: the next call reconnects
            return false;
        }
        drop(c);   // reconnect once and send the statement again
    }
    return false;
}

bool LuaDb::query(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, fl::DbRows& out,
                  std::string& error)
{
    // libmysqlclient keeps per-thread state; the world thread, the map threads and the workers all use it.
    static thread_local bool threadReady = [] { mysql_thread_init(); return true; }();
    (void)threadReady;
    if (sql.size() > 256 * 1024)
    {
        error = "query text is longer than 256 KB";
        return false;
    }
    int which = whichOf(db);
    if (which < 0)
    {
        error = "unknown database";
        return false;
    }
    Conn& c = _sync[which];
    std::lock_guard<std::mutex> lock(c.mutex);
    return readInto(c, which, sql, maxRows, maxBytes, out, error);
}

bool LuaDb::readInto(Conn& c, int which, std::string const& sql, size_t maxRows, size_t maxBytes, fl::DbRows& out,
                     std::string& error)
{
    // The server stops a SELECT after maxRows + 1 rows (one more than we keep, to tell "exactly maxRows" from "more"):
    // the session limit bounds the stream before it reaches us.
    if (maxRows)
    {
        if (MYSQL* m = connect(c, which, error))
        {
            if (c.selectLimit != maxRows + 1)
            {
                std::string set = "SET SESSION sql_select_limit = " + std::to_string(maxRows + 1);
                if (mysql_real_query(m, set.data(), static_cast<unsigned long>(set.size())) == 0)
                    c.selectLimit = maxRows + 1;
            }
        }
    }
    if (!run(c, which, sql, true, error))
        return false;
    MYSQL* m = c.handle;
    // Unbuffered: rows are pulled one at a time and the cap is applied while reading, so a huge result is never held.
    MYSQL_RES* res = mysql_use_result(m);
    if (!res)
    {
        if (mysql_field_count(m) == 0)
            return true;   // a statement without a result set (the *Execute forms use the worker instead)
        error = mysql_error(m);
        return false;
    }
    MysqlSource source(res);
    ReadOutcome outcome = readRowsCapped(source, maxRows, maxBytes, out);
    (void)outcome;
    mysql_free_result(res);   // an unfinished stream is drained here: bounded by sql_select_limit and the packet cap
    return true;
}

bool LuaDb::execute(std::string const& db, std::string const& sql)
{
    if (whichOf(db) < 0 || sql.size() > 256 * 1024)
        return false;
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        if (_queue.size() >= 256)
            return false;   // fire and forget: a flood of writes is dropped, not queued without bound
        _queue.push_back(Queued{db, sql});
        if (!_worker.joinable())
            _worker = std::thread(&LuaDb::workerLoop, this);
    }
    _queueCv.notify_one();
    return true;
}

bool LuaDb::queryAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, uint64_t id,
                       std::string& error)
{
    if (sql.size() > 256 * 1024)
    {
        error = "query text is longer than 256 KB";
        return false;
    }
    int which = whichOf(db);
    if (which < 0)
    {
        error = "unknown database";
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(_asyncMutex);
        if (_asyncStop)
        {
            error = "shutting down";
            return false;
        }
        if (_asyncOutstanding >= 256)
        {
            error = "too many asynchronous queries in flight";
            return false;
        }
        ++_asyncOutstanding;
        AsyncJob job;
        job.id = id;
        job.db = db;
        job.sql = sql;
        job.maxRows = maxRows;
        job.maxBytes = maxBytes;
        _asyncQueue.push_back(std::move(job));
        if (!_asyncWorker.joinable())
            _asyncWorker = std::thread(&LuaDb::asyncLoop, this);
    }
    _asyncCv.notify_one();
    return true;
}

bool LuaDb::takeAsyncResult(uint64_t& id, fl::DbRows& out, std::string& error)
{
    std::lock_guard<std::mutex> lock(_asyncMutex);
    if (_asyncDone.empty())
        return false;
    AsyncDone done = std::move(_asyncDone.front());
    _asyncDone.pop_front();
    --_asyncOutstanding;
    id = done.id;
    out = std::move(done.rows);
    error = std::move(done.error);
    return true;
}

void LuaDb::cancelAsync(uint64_t id)
{
    std::lock_guard<std::mutex> lock(_asyncMutex);
    for (auto it = _asyncQueue.begin(); it != _asyncQueue.end(); ++it)
        if (it->id == id)
        {
            _asyncQueue.erase(it);
            --_asyncOutstanding;
            return;
        }
    for (auto it = _asyncDone.begin(); it != _asyncDone.end(); ++it)
        if (it->id == id)
        {
            _asyncDone.erase(it);
            --_asyncOutstanding;
            return;
        }
    if (_asyncRunning == id)
        _asyncCancelled.insert(id);   // running: the result is dropped when it ends
}

void LuaDb::asyncLoop()
{
    mysql_thread_init();
    for (;;)
    {
        AsyncJob job;
        {
            std::unique_lock<std::mutex> lock(_asyncMutex);
            _asyncCv.wait(lock, [this] { return _asyncStop || !_asyncQueue.empty(); });
            if (_asyncStop && _asyncQueue.empty())
                break;
            job = std::move(_asyncQueue.front());
            _asyncQueue.pop_front();
            _asyncRunning = job.id;
        }
        int const which = whichOf(job.db);
        AsyncDone done;
        done.id = job.id;
        Conn& c = _reader[which];
        {
            std::lock_guard<std::mutex> lock(c.mutex);
            readInto(c, which, job.sql, job.maxRows, job.maxBytes, done.rows, done.error);
        }
        std::lock_guard<std::mutex> lock(_asyncMutex);
        _asyncRunning = 0;
        if (_asyncCancelled.erase(job.id))
        {
            --_asyncOutstanding;
            continue;   // the caller gave up while the query ran
        }
        // Never dropped: at most 256 queries are outstanding (queued, running or finished), so this queue is bounded and
        // every callback gets its answer (or its own deadline).
        _asyncDone.push_back(std::move(done));
    }
    mysql_thread_end();
}

bool LuaDb::executeConfirmed(std::string const& db, std::string const& sql, uint64_t token)
{
    if (whichOf(db) < 0 || sql.size() > 256 * 1024)
        return false;
    {
        std::lock_guard<std::mutex> lock(_queueMutex);
        if (_confOutstanding >= 1024)
            return false;
        ++_confOutstanding;
        _confQueue.push_back(Queued{db, sql, token});
        if (!_worker.joinable())
            _worker = std::thread(&LuaDb::workerLoop, this);
    }
    _queueCv.notify_one();
    return true;
}

void LuaDb::takeConfirmed(std::vector<fl::DataWriteResult>& out)
{
    std::lock_guard<std::mutex> lock(_queueMutex);
    _confOutstanding -= _confDone.size();
    for (auto& r : _confDone)
        out.push_back(std::move(r));
    _confDone.clear();
}

void LuaDb::workerLoop()
{
    mysql_thread_init();
    for (;;)
    {
        Queued job;
        {
            std::unique_lock<std::mutex> lock(_queueMutex);
            _queueCv.wait(lock, [this] { return _stop || !_queue.empty() || !_confQueue.empty(); });
            if (_stop && _queue.empty() && _confQueue.empty())
                break;
            if (!_queue.empty())
            {
                job = std::move(_queue.front());
                _queue.pop_front();
            }
            else
            {
                job = std::move(_confQueue.front());
                _confQueue.pop_front();
            }
        }
        int which = whichOf(job.db);
        Conn& c = _writer[which];   // its own connection: a slow write never holds a read
        std::string error;
        bool ok;
        {
            std::lock_guard<std::mutex> lock(c.mutex);
            // A write is repeated only when the server was gone before it could run (not when it was lost mid-statement).
            ok = run(c, which, job.sql, false, error);
            if (ok)
                if (MYSQL_RES* res = mysql_store_result(c.handle))
                    mysql_free_result(res);
        }
        if (!ok && !job.token)   // a confirmed write reports through its result (the engine logs the failures together)
            TC_LOG_ERROR("forever.lua", "ForeverLua: {} write failed: {}", job.db, error);
        if (job.token)
        {
            fl::DataWriteResult r;
            r.token = job.token;
            r.ok = ok;
            r.error = error;
            std::lock_guard<std::mutex> lock(_queueMutex);
            _confDone.push_back(std::move(r));
        }
    }
    mysql_thread_end();
}

void LuaDb::closeAll()
{
    for (auto* set : {_sync, _writer, _reader})
        for (int i = 0; i < 3; ++i)
            if (set[i].handle)
            {
                mysql_close(set[i].handle);
                set[i].handle = nullptr;
            }
}
}  // namespace flh
