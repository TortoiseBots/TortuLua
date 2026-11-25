// Parsing of a forwarded command's tail ("sub arg "quoted arg" #123") and the GM-bridge guard.
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fl
{
struct ParsedTail
{
    std::vector<std::string> args;   // whitespace separated; "double quoted" tokens keep spaces; \" and \\ escape
    std::optional<std::string> id;   // the first token of the form #<1..40 letters, digits or _>
    std::string error;               // non-empty when the tail is malformed (unterminated quote, control char)
};

// A token that starts with # followed by 1..40 letters, digits or underscores (and nothing else) is the id and is not part of args.
// A quoted token is never an id. Later #id tokens stay ordinary arguments.
ParsedTail parseTail(std::string const& tail);

// The command root of a command line: trims, strips one leading '.' or '!', takes the first word, lower-cases it.
// Returns an empty string when there is none.
std::string commandRoot(std::string const& line);

// The GM bridge (Gm.run) refuses a line when it is empty, holds a control character (a second command), has a root in
// `deny`, or has a forwarded root, and when it is already inside a bridge call (depth >= 1). Returns the reason, or an
// empty string when the line is allowed.
std::string bridgeRefusal(std::string const& line, std::set<std::string> const& deny,
                          std::set<std::string> const& forwardedRoots, int depth);
}  // namespace fl
