// E5: the WorldPacket class and CreatePacket (docs: WorldPacket/index.html, Global/CreatePacket.html). A packet is an
// owned value of the Lua state (ObjKind::Packet, guid.lo = the engine's local id), never a world object: it lives until
// the state is closed or the state's packet table drops the oldest entry.
#include "engine/elu/EluBind.h"

#include <lauxlib.h>
#include <lua.h>

#include <cstring>
#include <optional>
#include <string>

namespace flu
{
namespace
{
fl::Engine::PacketValue& packetSelf(lua_State* L, int idx = 1)
{
    fl::HandleData const& h = kindArg(L, idx, kPacket, "packet");
    fl::Engine::PacketValue* p = eng(L).packet(h.guid.lo);
    if (!p)
        luaL_error(L, "WorldPacket no longer exists");
    return *p;
}

// Bounds: a read past the written bytes raises instead of reading uninitialised memory.
void need(lua_State* L, fl::Engine::PacketValue const& p, size_t n)
{
    if (p.readPos + n > p.bytes.size())
        luaL_error(L, "WorldPacket: read past the end of the packet");
}

template <typename T>
int readInt(lua_State* L)
{
    fl::Engine::PacketValue& p = packetSelf(L);
    need(L, p, sizeof(T));
    T v = 0;
    std::memcpy(&v, p.bytes.data() + p.readPos, sizeof(T));
    p.readPos += sizeof(T);
    lua_pushinteger(L, static_cast<lua_Integer>(v));
    return 1;
}

int readString(lua_State* L)
{
    fl::Engine::PacketValue& p = packetSelf(L);
    size_t end = p.readPos;
    while (end < p.bytes.size() && p.bytes[end] != 0)
        ++end;
    lua_pushlstring(L, reinterpret_cast<char const*>(p.bytes.data() + p.readPos), end - p.readPos);
    p.readPos = end < p.bytes.size() ? end + 1 : end;
    return 1;
}

int readFloat(lua_State* L)
{
    fl::Engine::PacketValue& p = packetSelf(L);
    need(L, p, sizeof(float));
    float v = 0;
    std::memcpy(&v, p.bytes.data() + p.readPos, sizeof(v));
    p.readPos += sizeof(v);
    lua_pushnumber(L, v);
    return 1;
}

int readDouble(lua_State* L)
{
    fl::Engine::PacketValue& p = packetSelf(L);
    need(L, p, sizeof(double));
    double v = 0;
    std::memcpy(&v, p.bytes.data() + p.readPos, sizeof(v));
    p.readPos += sizeof(v);
    lua_pushnumber(L, v);
    return 1;
}

void grow(lua_State* L, fl::Engine::PacketValue& p, size_t n)
{
    std::string error;
    if (!eng(L).packetGrow(p, n, error))
        luaL_error(L, "%s", error.c_str());
}

template <typename T>
int writeInt(lua_State* L)
{
    fl::Engine::PacketValue& p = packetSelf(L);
    T const v = static_cast<T>(luaL_checkinteger(L, 2));
    grow(L, p, sizeof(T));
    size_t const at = p.bytes.size();
    p.bytes.resize(at + sizeof(T));
    std::memcpy(p.bytes.data() + at, &v, sizeof(T));
    return 0;
}

int writeFloat(lua_State* L)
{
    fl::Engine::PacketValue& p = packetSelf(L);
    float const v = argFloat(L, 2, "value");
    grow(L, p, sizeof(v));
    size_t const at = p.bytes.size();
    p.bytes.resize(at + sizeof(v));
    std::memcpy(p.bytes.data() + at, &v, sizeof(v));
    return 0;
}

int writeDouble(lua_State* L)
{
    fl::Engine::PacketValue& p = packetSelf(L);
    double const v = luaL_checknumber(L, 2);
    grow(L, p, sizeof(v));
    size_t const at = p.bytes.size();
    p.bytes.resize(at + sizeof(v));
    std::memcpy(p.bytes.data() + at, &v, sizeof(v));
    return 0;
}

int writeString(lua_State* L)
{
    fl::Engine::PacketValue& p = packetSelf(L);
    size_t n = 0;
    char const* s = luaL_checklstring(L, 2, &n);
    grow(L, p, n + 1);
    size_t const at = p.bytes.size();
    p.bytes.resize(at + n + 1);
    std::memcpy(p.bytes.data() + at, s, n);
    p.bytes[at + n] = 0;
    return 0;
}

int getOpcode(lua_State* L)
{
    lua_pushinteger(L, packetSelf(L).opcode);
    return 1;
}

int setOpcode(lua_State* L)
{
    packetSelf(L).opcode = argUInt(L, 2, "opcode");
    return 0;
}

int getSize(lua_State* L)
{
    lua_pushinteger(L, static_cast<lua_Integer>(packetSelf(L).bytes.size()));
    return 1;
}

int create(lua_State* L)
{
    uint32_t const opcode = argUInt(L, 1, "opcode");
    uint32_t const size = static_cast<uint32_t>(optUInt(L, 2, 0));
    std::string error;
    std::optional<fl::HandleData> h = eng(L).addPacket(opcode, size, error);
    if (!h)
        luaL_error(L, "CreatePacket: %s", error.c_str());
    fl::eluPushHandle(L, *h);
    return 1;
}

// M3: a script packet lives as long as its Lua value: collecting the value frees the bytes (and their memory charge).
// Never raises (a finalizer). An event copy is already gone when its value is collected: nothing to do then.
int collect(lua_State* L)
{
    if (auto const* h = static_cast<fl::HandleData const*>(lua_touserdata(L, 1)))
        if (h->kind == fl::ObjKind::Packet)
            eng(L).dropPacket(h->guid.lo);
    return 0;
}
}  // namespace

int createPacket(lua_State* L) { return create(L); }

void registerPacketMethods(lua_State* L)
{
    static luaL_Reg const regs[] = {
        {"GetOpcode", fl::safe<getOpcode>},   {"SetOpcode", fl::safe<setOpcode>},
        {"GetSize", fl::safe<getSize>},       {"ReadByte", fl::safe<readInt<int8_t>>},
        {"ReadUByte", fl::safe<readInt<uint8_t>>}, {"ReadShort", fl::safe<readInt<int16_t>>},
        {"ReadUShort", fl::safe<readInt<uint16_t>>}, {"ReadLong", fl::safe<readInt<int32_t>>},
        {"ReadULong", fl::safe<readInt<uint32_t>>}, {"ReadFloat", fl::safe<readFloat>},
        {"ReadDouble", fl::safe<readDouble>}, {"ReadGUID", fl::safe<readInt<uint64_t>>},
        {"ReadString", fl::safe<readString>}, {"WriteByte", fl::safe<writeInt<int8_t>>},
        {"WriteUByte", fl::safe<writeInt<uint8_t>>}, {"WriteShort", fl::safe<writeInt<int16_t>>},
        {"WriteUShort", fl::safe<writeInt<uint16_t>>}, {"WriteLong", fl::safe<writeInt<int32_t>>},
        {"WriteULong", fl::safe<writeInt<uint32_t>>}, {"WriteFloat", fl::safe<writeFloat>},
        {"WriteDouble", fl::safe<writeDouble>}, {"WriteGUID", fl::safe<writeInt<uint64_t>>},
        {"WriteString", fl::safe<writeString>}, {nullptr, nullptr}};
    addMethods(L, "WorldPacket", regs);
    if (fl::eluPushClassMetatable(L, fl::ObjKind::Packet))
    {
        lua_pushcfunction(L, collect);
        lua_setfield(L, -2, "__gc");
        lua_pop(L, 1);
    }
}
}  // namespace flu
