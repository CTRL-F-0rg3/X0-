#pragma once
#include "x0p/ast.h"
#include "x0p/options.h"
#include <string>

namespace x0p {

// Emituje kod C11 z AST. Konwencje:
//   - class Foo -> struct Foo + wolne funkcje Foo_method(Foo* self, ...)
//   - new Foo() -> Foo_new()  (calloc + zero-init)
//   - String    -> x0p_str (const char*)
//   - print(x)  -> x0p_print_int/str/bool/float(x)  (po typie)
//   - enum Color { A, B } -> typedef enum { Color_A, Color_B } Color
//   - interface -> pomijane z warningiem (v0.1)
//   - defer     -> LIFO na koniec bloku, przed return/break/continue
std::string emit_c(Program& p, const EmitOptions& opts = {});
std::string emit_c_header(Program& p, const EmitOptions& opts = {});

} // namespace x0p
