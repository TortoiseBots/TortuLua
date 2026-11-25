#include "CommandGate.h"

#include <algorithm>
#include <cctype>

namespace flh
{
namespace
{
std::string lowered(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}
}  // namespace

void CommandNameSet::assign(std::vector<std::string> roots)
{
    _roots.clear();
    for (auto& r : roots)
        if (!r.empty())
            _roots.push_back(lowered(std::move(r)));
}

bool CommandNameSet::reserves(std::string const& name) const
{
    if (_roots.empty())
        return true;
    std::string const n = lowered(name);
    for (auto const& r : _roots)
        if (r.compare(0, n.size(), n) == 0)   // equal, or a prefix of a root
            return true;
    return false;
}
}  // namespace flh
