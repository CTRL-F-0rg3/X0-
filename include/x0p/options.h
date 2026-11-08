#pragma once
#include <string>
#include <vector>

namespace x0p {

struct EmitOptions {
    bool lib         = false;   // tryb biblioteczny (brak main wrappera w Rust)
    bool no_std      = false;   // freestanding: brak stdlib, extern runtime
    bool multi_file  = false;   // .c/.cpp tylko ciała, deklaracje w headerze
    std::string source_stem;    // "foo" z "foo.x0p"
    std::string header_name;    // np. "x0p.h" / "x0p.hpp"
    std::vector<std::string> imports;
};

} // namespace x0p
