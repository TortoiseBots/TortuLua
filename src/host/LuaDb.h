// The module's own MySQL connections for Eluna's raw SQL (WorldDBQuery/CharDBQuery/AuthDBQuery and the *Execute forms).
// It does not go through the core's pools on purpose: at the pinned core MySQLConnection::_HandleMySQLErrno ABORTs the
// whole server on ER_NO_SUCH_TABLE and ER_PARSE_ERROR, and a script must be able to ask for a table that is not there
// and get nil. The connection strings are the core's own config keys; reads run on the calling (world) thread with
// MYSQL_OPT_READ_TIMEOUT, writes are queued to one worker thread (fire and forget).
#pragma once

#include "DbPolicy.h"
#include "engine/Host.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct MYSQL;

namespace flh
{
class LuaDb
{
public:
    explicit LuaDb(uint64_t timeoutMs);
    ~LuaDb();
    LuaDb(LuaDb const&) = delete;
    LuaDb& operator=(LuaDb const&) = delete;

    // Synchronous query (Eluna: "execution halts until the query has finished"). db: world/characters/auth.
    bool query(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, fl::DbRows& out,
               std::string& error);
    // Queued to the worker thread (Eluna's *Execute: results ignored).
    bool execute(std::string const& db, std::string const& sql);
    // A write the caller wants confirmed (ForeverData): its own bounded queue, so the script *Execute forms and the data
    // writes cannot starve each other. False when 1024 confirmed writes are outstanding. The outcome comes back through
    // takeConfirmed() once the database answered: ok only when the statement ran.
    bool executeConfirmed(std::string const& db, std::string const& sql, uint64_t token);
    void takeConfirmed(std::vector<fl::DataWriteResult>& out);
    // Eluna's *DBQueryAsync (E3): the query runs on the reader worker, the result waits in a bounded queue for the
    // owner thread (takeAsyncResult). `id` is the engine's handle.
    bool queryAsync(std::string const& db, std::string const& sql, size_t maxRows, size_t maxBytes, uint64_t id,
                    std::string& error);
    // Pops one finished asynchronous query; false when none is ready.
    bool takeAsyncResult(uint64_t& id, fl::DbRows& out, std::string& error);
    // The caller gave up: a queued or finished result is dropped, a running one is discarded when it ends.
    void cancelAsync(uint64_t id);

private:
    struct Queued
    {
        std::string db;
        std::string sql;
        uint64_t token = 0;   // a confirmed write (0: fire and forget)
    };
    enum Which
    {
        World = 0,
        Characters = 1,
        Auth = 2
    };
    // One MySQL connection with its own reconnect backoff. The synchronous reads and the write worker use separate
    // ones, so a slow write never holds a read (and the read timeout means what the config says).
    struct Conn
    {
        MYSQL* handle = nullptr;
        ReconnectPolicy policy;
        std::mutex mutex;
        uint64_t selectLimit = 0;   // the session's sql_select_limit (sync connection)
    };
    static int whichOf(std::string const& db);
    // The open connection, or a (re)connect attempt that honours the backoff; null and `error` when unavailable.
    MYSQL* connect(Conn& c, int which, std::string& error);
    void drop(Conn& c);
    // Runs one statement; a lost connection is reopened and the statement retried once (`retryLost`: also on "lost during
    // the query", only for statements that are safe to repeat, i.e. reads).
    bool run(Conn& c, int which, std::string const& sql, bool retryLost, std::string& error);
    void closeAll();
    void workerLoop();
    // One read on `c` (the shared body of query() and of the asynchronous reader).
    bool readInto(Conn& c, int which, std::string const& sql, size_t maxRows, size_t maxBytes, fl::DbRows& out,
                  std::string& error);
    void asyncLoop();

    Conn _sync[3];
    Conn _writer[3];
    Conn _reader[3];   // the asynchronous queries' connection: a slow callback query never holds a sync read
    std::string _info[3];
    uint64_t _timeoutMs;

    std::thread _worker;
    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::deque<Queued> _queue;
    std::deque<Queued> _confQueue;                  // confirmed writes (ForeverData)
    std::deque<fl::DataWriteResult> _confDone;      // their outcomes, waiting for the owner thread
    size_t _confOutstanding = 0;                    // accepted and not yet taken
    bool _stop = false;

    struct AsyncJob
    {
        uint64_t id = 0;
        std::string db;
        std::string sql;
        size_t maxRows = 0;
        size_t maxBytes = 0;
    };
    struct AsyncDone
    {
        uint64_t id = 0;
        fl::DbRows rows;
        std::string error;
    };
    std::thread _asyncWorker;
    std::mutex _asyncMutex;
    std::condition_variable _asyncCv;
    std::deque<AsyncJob> _asyncQueue;
    std::deque<AsyncDone> _asyncDone;
    std::set<uint64_t> _asyncCancelled;
    uint64_t _asyncRunning = 0;       // the id the reader is running now (0: none)
    size_t _asyncOutstanding = 0;     // queued + running + finished and not taken: bounded by 256, so no result is dropped
    bool _asyncStop = false;
};
}  // namespace flh
