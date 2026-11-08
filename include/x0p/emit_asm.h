#pragma once
#include "x0p/ast.h"
#include <string>

namespace x0p {

// Emituje x86-64 System V assembly (AT&T-compatible, Intel syntax w źródle).
// Wyjście: gotowe do `as out.s -o out.o` lub `gcc out.s -o out`.
// Wersja v0.1: tylko funkcje, int, string, if/while/return, print, call.
// Klasy / defer / for / structs → ostrzeżenie + pominięcie.
std::string emit_asm(Program& p);

} // namespace x0p
