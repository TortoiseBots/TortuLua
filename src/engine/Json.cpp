#include "engine/Json.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include <lauxlib.h>
#include <lua.h>

namespace fl
{
namespace
{
bool validUtf8(char const* s, size_t n)
{
    size_t i = 0;
    while (i < n)
    {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len;
        uint32_t cp = 0;
        if (c < 0x80) { ++i; continue; }
        else if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;
        if (i + len > n)
            return false;
        for (size_t k = 1; k < len; ++k)
        {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += len;
    }
    return true;
}

bool encodeString(char const* s, size_t n, std::string& out, std::string& error)
{
    if (!validUtf8(s, n))
    {
        error = "invalid UTF-8 in a string";
        return false;
    }
    // The cap is counted while the text is written, not only before the next value: escaping grows a string up to six
    // times (a 1 MB string of '\1' is 6 MB of output), and a single long string could otherwise build megabytes of
    // C++ string in one uninterruptible loop (review N6).
    if (out.size() + n > kJsonMaxOutput)
    {
        error = "result too large";
        return false;
    }
    out += '"';
    for (size_t i = 0; i < n; ++i)
    {
        if (out.size() > kJsonMaxOutput)
        {
            error = "result too large";
            return false;
        }
        unsigned char c = static_cast<unsigned char>(s[i]);
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                }
                else
                    out += static_cast<char>(c);
        }
    }
    out += '"';
    return true;
}

bool encodeValue(lua_State* L, int idx, std::string& out, std::string& error, int depth)
{
    idx = lua_absindex(L, idx);
    if (out.size() > kJsonMaxOutput)
    {
        error = "result too large";
        return false;
    }
    if (depth > kJsonMaxDepth)
    {
        error = "nesting too deep (or a cycle)";
        return false;
    }
    switch (lua_type(L, idx))
    {
        case LUA_TNIL:
            out += "null";
            return true;
        case LUA_TBOOLEAN:
            out += lua_toboolean(L, idx) ? "true" : "false";
            return true;
        case LUA_TNUMBER:
            if (lua_isinteger(L, idx))
                out += std::to_string(static_cast<long long>(lua_tointeger(L, idx)));
            else
            {
                double d = lua_tonumber(L, idx);
                if (!std::isfinite(d))
                {
                    error = "NaN or infinity cannot be encoded";
                    return false;
                }
                char buf[40];
                std::snprintf(buf, sizeof buf, "%.17g", d);
                out += buf;
            }
            return true;
        case LUA_TSTRING:
        {
            size_t n;
            char const* s = lua_tolstring(L, idx, &n);
            return encodeString(s, n, out, error);
        }
        case LUA_TLIGHTUSERDATA:
            if (lua_touserdata(L, idx) == nullptr)
            {
                out += "null";
                return true;
            }
            error = "cannot encode a userdata";
            return false;
        case LUA_TTABLE:
        {
            if (!lua_checkstack(L, 6))
            {
                error = "stack overflow";
                return false;
            }
            // Count keys and see whether they are exactly 1..n.
            size_t count = 0;
            lua_Integer maxIndex = 0;
            bool onlyInts = true;
            lua_pushnil(L);
            while (lua_next(L, idx))
            {
                ++count;
                if (lua_type(L, -2) == LUA_TNUMBER && lua_isinteger(L, -2) && lua_tointeger(L, -2) >= 1)
                    maxIndex = std::max<lua_Integer>(maxIndex, lua_tointeger(L, -2));
                else
                    onlyInts = false;
                lua_pop(L, 1);
            }
            if (count > 0 && onlyInts && static_cast<size_t>(maxIndex) == count)
            {
                out += '[';
                for (size_t i = 1; i <= count; ++i)
                {
                    if (i > 1)
                        out += ',';
                    lua_rawgeti(L, idx, static_cast<lua_Integer>(i));
                    bool ok = encodeValue(L, -1, out, error, depth + 1);
                    lua_pop(L, 1);
                    if (!ok)
                        return false;
                }
                out += ']';
                return true;
            }
            std::vector<std::string> keys;
            lua_pushnil(L);
            while (lua_next(L, idx))
            {
                if (lua_type(L, -2) != LUA_TSTRING)
                {
                    lua_pop(L, 2);
                    error = "object keys must be strings";
                    return false;
                }
                size_t n;
                char const* k = lua_tolstring(L, -2, &n);
                keys.emplace_back(k, n);
                lua_pop(L, 1);
            }
            std::sort(keys.begin(), keys.end());
            out += '{';
            bool first = true;
            for (auto const& k : keys)
            {
                if (!first)
                    out += ',';
                first = false;
                if (!encodeString(k.data(), k.size(), out, error))
                    return false;
                out += ':';
                lua_pushlstring(L, k.data(), k.size());
                lua_rawget(L, idx);
                bool ok = encodeValue(L, -1, out, error, depth + 1);
                lua_pop(L, 1);
                if (!ok)
                    return false;
            }
            out += '}';
            return true;
        }
        default:
            error = std::string("cannot encode a ") + luaL_typename(L, idx);
            return false;
    }
}

struct Parser
{
    lua_State* L;
    std::string const& s;
    size_t i = 0;
    std::string error;

    bool fail(char const* what)
    {
        error = std::string(what) + " at byte " + std::to_string(i);
        return false;
    }
    void ws()
    {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
            ++i;
    }
    bool lit(char const* word)
    {
        size_t n = std::strlen(word);
        if (s.compare(i, n, word) != 0)
            return fail("bad literal");
        i += n;
        return true;
    }
    static int hexv(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    bool hex4(uint32_t& out)
    {
        if (i + 4 > s.size())
            return fail("short \\u escape");
        out = 0;
        for (int k = 0; k < 4; ++k)
        {
            int h = hexv(s[i + k]);
            if (h < 0)
                return fail("bad \\u escape");
            out = out * 16 + static_cast<uint32_t>(h);
        }
        i += 4;
        return true;
    }
    static void putUtf8(std::string& o, uint32_t cp)
    {
        if (cp < 0x80) o += static_cast<char>(cp);
        else if (cp < 0x800) { o += static_cast<char>(0xC0 | (cp >> 6)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { o += static_cast<char>(0xE0 | (cp >> 12)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { o += static_cast<char>(0xF0 | (cp >> 18)); o += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); o += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    bool str(std::string& out)
    {
        ++i;  // opening quote
        while (true)
        {
            if (i >= s.size())
                return fail("unterminated string");
            unsigned char c = static_cast<unsigned char>(s[i++]);
            if (c == '"')
                break;
            if (c < 0x20)
                return fail("control character in a string");
            if (c != '\\')
            {
                out += static_cast<char>(c);
                continue;
            }
            if (i >= s.size())
                return fail("unterminated string");
            char e = s[i++];
            switch (e)
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    uint32_t cp = 0;
                    if (!hex4(cp))
                        return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF)
                    {
                        uint32_t lo = 0;
                        if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u')
                            return fail("lone surrogate");
                        i += 2;
                        if (!hex4(lo))
                            return false;
                        if (lo < 0xDC00 || lo > 0xDFFF)
                            return fail("bad surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    else if (cp >= 0xDC00 && cp <= 0xDFFF)
                        return fail("lone surrogate");
                    putUtf8(out, cp);
                    break;
                }
                default:
                    return fail("bad escape");
            }
        }
        if (!validUtf8(out.data(), out.size()))
            return fail("invalid UTF-8 in a string");
        return true;
    }
    bool number()
    {
        size_t start = i;
        bool isInt = true;
        if (s[i] == '-')
            ++i;
        if (i >= s.size())
            return fail("bad number");
        if (s[i] == '0')
            ++i;
        else if (s[i] >= '1' && s[i] <= '9')
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                ++i;
        else
            return fail("bad number");
        if (i < s.size() && s[i] == '.')
        {
            isInt = false;
            ++i;
            size_t d = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                ++i;
            if (i == d)
                return fail("bad number");
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
        {
            isInt = false;
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                ++i;
            size_t d = i;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                ++i;
            if (i == d)
                return fail("bad number");
        }
        std::string tok = s.substr(start, i - start);
        if (isInt)
        {
            errno = 0;
            char* end = nullptr;
            long long v = std::strtoll(tok.c_str(), &end, 10);
            if (errno == 0)
            {
                lua_pushinteger(L, static_cast<lua_Integer>(v));
                return true;
            }
        }
        double d = std::strtod(tok.c_str(), nullptr);
        if (!std::isfinite(d))
            return fail("number out of range");
        lua_pushnumber(L, d);
        return true;
    }
    // Pushes exactly one value on success.
    bool value(int depth)
    {
        if (depth > kJsonMaxDepth)
            return fail("nesting too deep");
        if (!lua_checkstack(L, 6))
            return fail("stack overflow");
        ws();
        if (i >= s.size())
            return fail("unexpected end");
        char c = s[i];
        if (c == '{')
        {
            ++i;
            lua_newtable(L);
            ws();
            if (i < s.size() && s[i] == '}')
            {
                ++i;
                return true;
            }
            while (true)
            {
                ws();
                if (i >= s.size() || s[i] != '"')
                    return fail("object key expected");
                std::string key;
                if (!str(key))
                    return false;
                ws();
                if (i >= s.size() || s[i] != ':')
                    return fail("':' expected");
                ++i;
                lua_pushlstring(L, key.data(), key.size());
                if (!value(depth + 1))
                    return false;
                lua_rawset(L, -3);
                ws();
                if (i < s.size() && s[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}')
                {
                    ++i;
                    return true;
                }
                return fail("',' or '}' expected");
            }
        }
        if (c == '[')
        {
            ++i;
            lua_newtable(L);
            ws();
            if (i < s.size() && s[i] == ']')
            {
                ++i;
                return true;
            }
            lua_Integer n = 0;
            while (true)
            {
                if (!value(depth + 1))
                    return false;
                lua_rawseti(L, -2, ++n);
                ws();
                if (i < s.size() && s[i] == ',')
                {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']')
                {
                    ++i;
                    return true;
                }
                return fail("',' or ']' expected");
            }
        }
        if (c == '"')
        {
            std::string out;
            if (!str(out))
                return false;
            lua_pushlstring(L, out.data(), out.size());
            return true;
        }
        if (c == 't') { if (!lit("true")) return false; lua_pushboolean(L, 1); return true; }
        if (c == 'f') { if (!lit("false")) return false; lua_pushboolean(L, 0); return true; }
        if (c == 'n') { if (!lit("null")) return false; lua_pushlightuserdata(L, nullptr); return true; }
        if (c == '-' || (c >= '0' && c <= '9'))
            return number();
        return fail("unexpected character");
    }
};
}  // namespace

bool jsonEncode(lua_State* L, int idx, std::string& out, std::string& error)
{
    out.clear();
    if (!encodeValue(L, idx, out, error, 0))
    {
        out.clear();
        return false;
    }
    return true;
}

bool jsonDecode(lua_State* L, std::string const& text, std::string& error)
{
    if (text.size() > kJsonMaxInput)
    {
        error = "input too large";
        return false;
    }
    int top = lua_gettop(L);
    Parser p{L, text, 0, std::string()};
    bool ok = p.value(0);
    if (ok)
    {
        p.ws();
        if (p.i != text.size())
            ok = p.fail("trailing characters");
    }
    if (!ok)
    {
        lua_settop(L, top);
        error = p.error;
        return false;
    }
    return true;
}
}  // namespace fl
