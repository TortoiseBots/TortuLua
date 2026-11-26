// WorldPacket (docs: WorldPacket/index.html, Global/CreatePacket.html): full coverage of all 23 documented
// WorldPacket methods — signed and unsigned integer reads/writes, float/double, string, 64-bit GUID, opcode/size,
// EOF/bounds errors, NaN rejection, argument validation, packet eviction, event-scoped copies, and test-mode isolation.
#include "Support.h"

#include "engine/Elu.h"

#include <cmath>
#include <cstdint>
#include <string>

using namespace fltest;

namespace
{
fl::Config packetElunaConfig(fl::Config c)
{
    c.mode = fl::EngineMode::Eluna;
    c.eventBudget = {0, 0};
    c.commandBudget = {0, 0};
    c.loadBudget = {0, 0};
    c.tickBudgetMs = 0;
    c.slowHandlerMs = 0;
    return c;
}

struct PacketRig : Rig
{
    PacketRig() : Rig([](fl::Config& c) { c = packetElunaConfig(c); }) { }

    std::string errorOf(std::string const& code)
    {
        fl::CallResult r = run(code);
        return r.ok ? std::string("no error") : r.error;
    }
};
}  // namespace

TEST_CASE("eluna worldpacket: every documented read and write method round-trips")
{
    PacketRig rig;
    rig.ok("p = CreatePacket(0x1234, 0)\n"
           "p:WriteByte(-128)\n"
           "p:WriteByte(127)\n"
           "p:WriteUByte(0)\n"
           "p:WriteUByte(255)\n"
           "p:WriteShort(-32768)\n"
           "p:WriteShort(32767)\n"
           "p:WriteUShort(0)\n"
           "p:WriteUShort(65535)\n"
           "p:WriteLong(-2147483648)\n"
           "p:WriteLong(2147483647)\n"
           "p:WriteULong(0)\n"
           "p:WriteULong(4294967295)\n"
           "p:WriteFloat(-1.25)\n"
           "p:WriteDouble(3.141592653589793)\n"
           "p:WriteGUID(0x0102030405060708)\n"
           "p:WriteGUID(0xf122334455667788)\n"   // bit 63 set
           "p:WriteString('')\n"
           "p:WriteString('hello world')\n");

    size_t const expectedSize = 1 + 1 + 1 + 1 + 2 + 2 + 2 + 2 + 4 + 4 + 4 + 4 + 4 + 8 + 8 + 8 + 1 + 12;
    REQUIRE(rig.ok("return p:GetSize()") == std::to_string(expectedSize));
    REQUIRE(rig.ok("return p:GetOpcode()") == "4660");
    rig.ok("p:SetOpcode(0x5678)");
    REQUIRE(rig.ok("return p:GetOpcode()") == "22136");

    CHECK(rig.ok("return p:ReadByte()") == "-128");
    CHECK(rig.ok("return p:ReadByte()") == "127");
    CHECK(rig.ok("return p:ReadUByte()") == "0");
    CHECK(rig.ok("return p:ReadUByte()") == "255");
    CHECK(rig.ok("return p:ReadShort()") == "-32768");
    CHECK(rig.ok("return p:ReadShort()") == "32767");
    CHECK(rig.ok("return p:ReadUShort()") == "0");
    CHECK(rig.ok("return p:ReadUShort()") == "65535");
    CHECK(rig.ok("return p:ReadLong()") == "-2147483648");
    CHECK(rig.ok("return p:ReadLong()") == "2147483647");
    CHECK(rig.ok("return p:ReadULong()") == "0");
    CHECK(rig.ok("return p:ReadULong()") == "4294967295");
    CHECK(rig.ok("return string.format('%.2f', p:ReadFloat())") == "-1.25");
    CHECK(rig.ok("return string.format('%.15f', p:ReadDouble())") == "3.141592653589793");
    CHECK(rig.ok("return string.format('0x%016x', p:ReadGUID())") == "0x0102030405060708");
    CHECK(rig.ok("return string.format('0x%016x', p:ReadGUID())") == "0xf122334455667788");
    CHECK(rig.ok("return p:ReadString()") == "");
    CHECK(rig.ok("return p:ReadString()") == "hello world");
    // Now at EOF: every read method must raise "past the end".
    CHECK(rig.errorOf("return p:ReadByte()").find("past the end") != std::string::npos);
    CHECK(rig.errorOf("return p:ReadString()").find("past the end") != std::string::npos);
}

TEST_CASE("eluna worldpacket: little-endian byte order and partial-read EOF checks")
{
    PacketRig rig;
    rig.ok("p = CreatePacket(1, 0)\n"
           "p:WriteUShort(0x1234)\n"
           "p:WriteULong(0x01020304)\n"
           "p:WriteGUID(0x1122334455667788)\n");

    // Verify little-endian encoding byte by byte on a second packet fed the same values.
    CHECK(rig.ok("return string.format('%02x%02x', p:ReadUByte(), p:ReadUByte())") == "3412");
    CHECK(rig.ok("return string.format('%02x%02x%02x%02x', p:ReadUByte(), p:ReadUByte(), p:ReadUByte(), p:ReadUByte())") ==
          "04030201");
    CHECK(rig.ok("return string.format('%02x%02x%02x%02x%02x%02x%02x%02x', "
                 "p:ReadUByte(), p:ReadUByte(), p:ReadUByte(), p:ReadUByte(), "
                 "p:ReadUByte(), p:ReadUByte(), p:ReadUByte(), p:ReadUByte())") == "8877665544332211");

    // Every Read* method refuses an empty packet or a packet with too few remaining bytes.
    char const* const readMethods[] = {
        "ReadByte", "ReadUByte", "ReadShort", "ReadUShort", "ReadLong",
        "ReadULong", "ReadFloat", "ReadDouble", "ReadGUID", "ReadString",
    };
    for (char const* method : readMethods)
    {
        INFO("empty EOF for " << method);
        CHECK(rig.errorOf(std::string("return CreatePacket(1, 0):") + method + "()").find("past the end") !=
              std::string::npos);
    }

    // ReadString on a non-empty packet without a NUL terminator reads to the end, then subsequent reads raise EOF.
    rig.ok("RegisterPacketEvent(0x88, 5, function(ev, pkt)\n"
           "  tail_str = pkt:ReadString()\n"
           "  tail_eof = select(2, pcall(function() return pkt:ReadString() end))\n"
           "end)");
    rig.e().emitPacketEvent(0x88, 5, {'a', 'b', 'c'}, fl::EventValue());
    CHECK(rig.ok("return tail_str") == "abc");
    CHECK(rig.ok("return tail_eof").find("past the end") != std::string::npos);
}

TEST_CASE("eluna worldpacket: argument validation, reserve clamp, eviction and kind checks")
{
    PacketRig rig;

    // NaN is rejected by both WriteFloat and WriteDouble.
    CHECK(rig.errorOf("CreatePacket(1, 0):WriteFloat(0/0)").find("NaN") != std::string::npos);
    CHECK(rig.errorOf("CreatePacket(1, 0):WriteDouble(0/0)").find("NaN") != std::string::npos);

    // Wrong argument types fail before mutating the packet.
    rig.ok("p = CreatePacket(10, 0)");
    CHECK_FALSE(rig.run("p:WriteByte('abc')").ok);
    CHECK_FALSE(rig.run("p:WriteUByte(1.5)").ok);
    CHECK_FALSE(rig.run("p:WriteShort(nil)").ok);
    CHECK_FALSE(rig.run("p:WriteUShort({})").ok);
    CHECK_FALSE(rig.run("p:WriteLong('x')").ok);
    CHECK_FALSE(rig.run("p:WriteULong(true)").ok);
    CHECK_FALSE(rig.run("p:WriteGUID('000000000000000a')").ok);
    CHECK_FALSE(rig.run("p:WriteFloat('x')").ok);
    CHECK_FALSE(rig.run("p:WriteDouble('x')").ok);
    CHECK_FALSE(rig.run("p:WriteString({})").ok);
    CHECK_FALSE(rig.run("p:SetOpcode(-1)").ok);
    CHECK_FALSE(rig.run("p:SetOpcode(4294967296)").ok);
    CHECK_FALSE(rig.run("CreatePacket(-1, 0)").ok);
    CHECK_FALSE(rig.run("CreatePacket(1, -1)").ok);
    CHECK(rig.ok("return p:GetSize() .. ':' .. p:GetOpcode()") == "0:10");

    // Numeric strings remain convertible for WriteGUID (e.g., DB/config GUID strings).
    rig.ok("local g = CreatePacket(10, 0) g:WriteGUID('12345') guid_str_read = g:ReadGUID()");
    CHECK(rig.ok("return guid_str_read") == "12345");

    // Large reserve (> UINT32_MAX) clamps to kMaxPacketBytes (64 KB) instead of wrapping to 0 via uint32 truncation.
    {
        Rig capped([](fl::Config& c) {
            c = packetElunaConfig(c);
            c.memoryCap = 1024u * 1024u;
        });
        capped.ok("hold = {} while pcall(function() hold[#hold + 1] = CreatePacket(1, 32768) end) do end");
        fl::CallResult over = capped.run("return CreatePacket(1, 0x100000000)");
        CHECK_FALSE(over.ok);
        CHECK(over.error.find("not enough memory") != std::string::npos);
        CHECK(capped.ok("return CreatePacket(1, 0):GetSize()") == "0");
    }

    // Ring eviction past 1024 live packets drops the oldest packet handle.
    rig.ok("oldest = CreatePacket(7, 0) oldest:WriteByte(1)\n"
           "hold = {}\n"
           "for i = 1, 1025 do hold[i] = CreatePacket(8, 0) end");
    CHECK(rig.errorOf("return oldest:GetOpcode()").find("WorldPacket no longer exists") != std::string::npos);

    // Calling WorldPacket methods on a non-packet handle fails.
    fl::HandleData const pl = FakeHost::player(5, 0);
    rig.host.add(pl, fl::ObjectInfo{"P", 0, 0, 0, 0, 0, true});
    rig.ok("RegisterPlayerEvent(3, function(ev, player) pl = player end)");
    rig.e().emitEluna(fl::EluKind::Player, 0, 3, {fl::EventValue(pl)});
    CHECK_FALSE(rig.run("return WorldPacket.GetOpcode(pl)").ok);
}

TEST_CASE("eluna worldpacket: packet event reads all types and test mode omits WorldPacket")
{
    PacketRig rig;
    rig.ok("RegisterPacketEvent(0x91, 5, function(ev, pkt)\n"
           "  got = pkt:GetOpcode() .. ':' .. pkt:GetSize() .. ':' .. pkt:ReadByte() .. ':' .. pkt:ReadUShort() .. ':' .. "
           "pkt:ReadLong() .. ':' .. pkt:ReadString()\n"
           "end)");
    // Payload: opcode 0x91, int8(-5 = 0xFB), uint16(0x0102 = 258), int32(-1 = 0xFFFFFFFF), "ok\0"
    std::vector<uint8_t> bytes = {0xFB, 0x02, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 'o', 'k', 0};
    rig.e().emitPacketEvent(0x91, 5, bytes, fl::EventValue());
    CHECK(rig.ok("return got") == "145:10:-5:258:-1:ok");

    // Test mode stays unchanged: no CreatePacket or WorldPacket global.
    Rig testRig([](fl::Config& c) { c.mode = fl::EngineMode::Test; });
    CHECK(testRig.ok("return tostring(CreatePacket) .. ':' .. tostring(WorldPacket)") == "nil:nil");
}
