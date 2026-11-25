#include "engine/CommandTail.h"

#include <cctype>

namespace fl
{
namespace
{
bool isSpace(char c) { return c == ' ' || c == '\t'; }
bool isControl(unsigned char c) { return c < 0x20 || c == 0x7f; }
}  // namespace

ParsedTail parseTail(std::string const& tail)
{
    ParsedTail r;
    size_t i = 0, n = tail.size();
    while (i < n)
    {
        while (i < n && isSpace(tail[i]))
            ++i;
        if (i >= n)
            break;

        std::string token;
        bool quoted = false;
        if (tail[i] == '"')
        {
            quoted = true;
            ++i;
            bool closed = false;
            while (i < n)
            {
                char c = tail[i++];
                if (c == '\\' && i < n && (tail[i] == '"' || tail[i] == '\\'))
                    token += tail[i++];
                else if (c == '"')
                {
                    closed = true;
                    break;
                }
                else
                {
                    if (isControl(static_cast<unsigned char>(c)))
                    {
                        r.error = "control character in the command";
                        return r;
                    }
                    token += c;
                }
            }
            if (!closed)
            {
                r.error = "unterminated quote";
                return r;
            }
        }
        else
        {
            while (i < n && !isSpace(tail[i]))
            {
                if (isControl(static_cast<unsigned char>(tail[i])))
                {
                    r.error = "control character in the command";
                    return r;
                }
                token += tail[i++];
            }
        }

        // A request id looks like "ta1x" (letters and digits): a token # + 1..40 of [A-Za-z0-9_] is the request id.
        if (!quoted && !r.id && token.size() >= 2 && token.size() <= 41 && token[0] == '#')
        {
            bool plain = true;
            for (size_t k = 1; k < token.size(); ++k)
            {
                unsigned char c = static_cast<unsigned char>(token[k]);
                if (!(std::isalnum(c) || c == '_'))
                {
                    plain = false;
                    break;
                }
            }
            if (plain)
            {
                r.id = token.substr(1);
                continue;
            }
        }
        r.args.push_back(std::move(token));
    }
    return r;
}

std::string commandRoot(std::string const& line)
{
    size_t i = 0, n = line.size();
    while (i < n && isSpace(line[i]))
        ++i;
    if (i < n && (line[i] == '.' || line[i] == '!'))
        ++i;
    size_t start = i;
    while (i < n && !isSpace(line[i]))
        ++i;
    std::string root = line.substr(start, i - start);
    for (auto& c : root)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return root;
}

std::string bridgeRefusal(std::string const& line, std::set<std::string> const& deny,
                          std::set<std::string> const& forwardedRoots, int depth)
{
    if (depth >= 1)
        return "bridge depth";
    if (line.empty() || line.size() > 1024)
        return "bad length";
    for (unsigned char c : line)
        if (isControl(c))
            return "control character";
    std::string root = commandRoot(line);
    if (root.empty())
        return "empty command";
    // Every character of the root must be a letter, digit or underscore: no ".lua" smuggled in behind odd syntax.
    for (unsigned char c : root)
        if (!(std::isalnum(c) || c == '_'))
            return "bad command root";
    // The core resolves a unique prefix of a command name ("acc" is "account", "serv" is "server"), so a root is
    // refused when it is a prefix of a denied or forwarded root, or when a denied or forwarded root is a prefix of it.
    for (auto const* set : {&deny, &forwardedRoots})
        for (std::string const& d : *set)
            if (!d.empty() && (d.compare(0, root.size(), root) == 0 || root.compare(0, d.size(), d) == 0))
                return "command refused: " + root;
    return {};
}
}  // namespace fl
