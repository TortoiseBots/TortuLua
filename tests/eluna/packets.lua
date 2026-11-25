-- E5: CreatePacket and the WorldPacket class (write, then read), and the read-only packet events.
local packet = CreatePacket(0x99, 0)
packet:WriteUByte(5)
packet:WriteULong(70000)
packet:WriteString('forever')
packet_ok = packet:ReadUByte()
packet_long = packet:ReadULong()
packet_text = packet:ReadString()
packet_size = packet:GetSize()
packet_opcode = packet:GetOpcode()

RegisterPacketEvent(0x99, 5, function(event, received, player)
    packet_seen = event .. ':' .. received:ReadUByte() .. ':' .. tostring(player)
end)
