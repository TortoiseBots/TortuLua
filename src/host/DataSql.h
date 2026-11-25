// ForeverLua host side: the SQL of the ForeverData table, game-free so the statements are unit-tested
// (tests/test_host_commands.cpp). Keys are byte strings: the columns are VARBINARY / MEDIUMBLOB (binary collation: "Gold"
// and "gold", or "a" and "a ", are different rows, and no byte is refused by a character set), and every value goes into the
// statement as a hex literal, so there is no escaping and no NUL truncation.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace flh
{
// X'..' hex literal of the bytes.
std::string sqlHex(std::string const& bytes);
std::string dataTableDdl();
// information_schema: the (column, data type) pairs of the table, to decide whether an older table needs the migration.
std::string dataColumnsSql();
bool dataTableNeedsMigration(std::vector<std::pair<std::string, std::string>> const& columns);
std::string dataMigrateSql();
std::string dataSelectAllSql();
std::string dataUpsertSql(std::string const& script, std::string const& key, std::string const& value);
std::string dataEraseSql(std::string const& script, std::string const& key);
}  // namespace flh
