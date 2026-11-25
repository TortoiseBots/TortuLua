#include "engine/Text.h"

#include <sstream>

namespace fl
{
std::vector<std::string> splitWords(std::string const& s)
{
    std::vector<std::string> out;
    std::istringstream in(s);
    std::string w;
    while (in >> w)
        out.push_back(w);
    return out;
}

void splitLines(std::string_view text, std::vector<std::string>& out)
{
    size_t start = 0;
    while (start < text.size())
    {
        size_t nl = text.find('\n', start);
        std::string_view part = text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start);
        while (!part.empty() && (part.back() == '\r' || part.back() == '\n'))
            part.remove_suffix(1);
        if (!part.empty())
            out.emplace_back(part);
        if (nl == std::string_view::npos)
            break;
        start = nl + 1;
    }
}

std::vector<std::string> describeLoad(LoadReport const& r)
{
    std::vector<std::string> out;
    if (r.folderMissing)
        out.push_back("Tortulua: script folder not found");
    std::string files;
    for (auto const& f : r.loaded)
        files += (files.empty() ? "" : ", ") + f;
    out.push_back("Tortulua: loaded " + std::to_string(r.loaded.size()) + " file(s)" + (files.empty() ? "" : ": " + files));
    for (auto const& e : r.errors)
        out.push_back("Tortulua error: " + e);
    return out;
}
}  // namespace fl
