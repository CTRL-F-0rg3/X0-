#pragma once
#include "x0p/ast.h"
#include <string>

namespace x0p {

// Emituje kod Rust (edition 2021) z AST.
// Konwencje:
//   - class Foo -> struct Foo + impl Foo { fn method(&mut self, ...) }
//   - new Foo() -> Foo::new()  (Box<Foo>)
//   - String    -> String
//   - print(x)  -> println! (format po typie)
//   - enum Color { A } -> #[derive(Debug)] enum Color { A }
//   - interface -> pomijane z warningiem (v0.1)
//   - defer     -> inline przed return/break/continue i na koncu bloku (Zig-style)
//   - main      -> fn x0p_main() + wrapper fn main()
std::string emit_rust(Program& p);

} // namespace x0p
