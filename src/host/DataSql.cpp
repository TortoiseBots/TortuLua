#include "DataSql.h"

#include <algorithm>
#include <cctype>

namespace flh
{
std::string sqlHex(std::string const& bytes)
{
    static char const* digits = "0123456789ABCDEF";
    std::string out = "X'";
    out.reserve(bytes.size() * 2 + 3);
    for (unsigned char c : bytes)
    {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    out += '\'';
    return out;
}

std::string dataTableDdl()
{
    return "CREATE TABLE IF NOT EXISTS forever_lua_data ("
           "script VARBINARY(255) NOT NULL, k VARBINARY(255) NOT NULL, v MEDIUMBLOB NOT NULL, "
           "updated BIGINT NOT NULL, PRIMARY KEY (script, k))";
}

std::string dataColumnsSql()
{
    return "SELECT COLUMN_NAME, DATA_TYPE FROM information_schema.COLUMNS "
           "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = 'forever_lua_data'";
}

bool dataTableNeedsMigration(std::vector<std::pair<std::string, std::string>> const& columns)
{
    for (auto const& c : columns)
    {
        std::string name = c.first, type = c.second;
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return std::tolower(ch); });
        std::transform(type.begin(), type.end(), type.begin(), [](unsigned char ch) { return std::tolower(ch); });
        if ((name == "script" || name == "k") && type != "varbinary")
            return true;
        if (name == "v" && type != "mediumblob")
            return true;
    }
    return false;
}

std::string dataMigrateSql()
{
    return "ALTER TABLE forever_lua_data MODIFY script VARBINARY(255) NOT NULL, MODIFY k VARBINARY(255) NOT NULL, "
           "MODIFY v MEDIUMBLOB NOT NULL";
}

std::string dataSelectAllSql() { return "SELECT script, k, v FROM forever_lua_data"; }

std::string dataUpsertSql(std::string const& script, std::string const& key, std::string const& value)
{
    return "INSERT INTO forever_lua_data (script, k, v, updated) VALUES (" + sqlHex(script) + "," + sqlHex(key) + "," +
           sqlHex(value) + ",UNIX_TIMESTAMP()) ON DUPLICATE KEY UPDATE v=VALUES(v), updated=VALUES(updated)";
}

std::string dataEraseSql(std::string const& script, std::string const& key)
{
    return "DELETE FROM forever_lua_data WHERE script=" + sqlHex(script) + " AND k=" + sqlHex(key);
}
}  // namespace flh
