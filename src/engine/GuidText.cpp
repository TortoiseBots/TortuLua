#include "engine/GuidText.h"

namespace fl
{
std::string guidText(Guid const& g)
{
    static char const* const digits = "0123456789abcdef";
    std::string s(32, '0');
    for (int i = 0; i < 16; ++i)
    {
        s[15 - i] = digits[(g.hi >> (4 * i)) & 0xF];
        s[31 - i] = digits[(g.lo >> (4 * i)) & 0xF];
    }
    return s;
}

bool parseGuidText(std::string_view text, Guid& out)
{
    if (text.size() != 32)
        return false;
    uint64_t halves[2] = {0, 0};
    for (size_t i = 0; i < 32; ++i)
    {
        char c = text[i];
        uint64_t v;
        if (c >= '0' && c <= '9')
            v = static_cast<uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = static_cast<uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = static_cast<uint64_t>(c - 'A' + 10);
        else
            return false;
        halves[i / 16] = (halves[i / 16] << 4) | v;
    }
    out.hi = halves[0];
    out.lo = halves[1];
    return true;
}
}  // namespace fl
