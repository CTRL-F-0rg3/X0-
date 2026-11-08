#pragma once
#include "x0p/ast.h"
#include <string>

namespace x0p {

// Emituje kod C++17 z AST.
// Konwencje:
//   - typy użytkownika (class/struct) są wskaźnikami (Java-style)
//   - print(x)  ->  x0p_print(x)  (szablon obsługuje int/double/bool/string/enum)
//   - string    ->  std::string
//   - defer     ->  emitowane LIFO na końcu bloku i przed return/break/continue
std::string emit_cpp(Program& p);

} // namespace x0p
