#pragma once
#include "x0p/diagnostic.h"
#include <cstdint>
#include <string_view>

namespace x0p {

// X0P_KEYWORDS(X) — JEDNO źródło prawdy dla słów kluczowych.
// X(tekst, NazwaEnum). Dodajesz słowo w jednym miejscu — enum,
// lookup table i token_kind_name generują się same.
//
// Projekt: Java + Kotlin + C# składnia, typy jak w C,
// pamięć jak w Zig (defer, mut, ref), async jak w C#/Kotlin.
#define X0P_KEYWORDS(X)                 \
    /* typy */                          \
    X(void,       Kw_void)              \
    X(bool,       Kw_bool)              \
    X(char,       Kw_char)              \
    X(int,        Kw_int)               \
    X(float,      Kw_float)             \
    X(double,     Kw_double)            \
    /* deklaracje zmiennych */          \
    X(val,        Kw_val)               \
    X(var,        Kw_var)               \
    X(let,        Kw_let)               \
    X(const,      Kw_const)             \
    X(mut,        Kw_mut)               \
    X(ref,        Kw_ref)               \
    X(in,         Kw_in)                \
    X(out,        Kw_out)               \
    /* klasy / typy */                  \
    X(class,      Kw_class)             \
    X(interface,  Kw_interface)         \
    X(struct,     Kw_struct)            \
    X(enum,       Kw_enum)              \
    X(extends,    Kw_extends)           \
    X(implements, Kw_implements)        \
    X(new,        Kw_new)               \
    X(this,       Kw_this)              \
    X(super,      Kw_super)             \
    X(type,       Kw_type)              \
    X(where,      Kw_where)             \
    /* modyfikatory */                  \
    X(public,     Kw_public)            \
    X(private,    Kw_private)           \
    X(protected,  Kw_protected)         \
    X(internal,   Kw_internal)          \
    X(static,     Kw_static)            \
    X(abstract,   Kw_abstract)          \
    X(final,      Kw_final)             \
    X(sealed,     Kw_sealed)            \
    X(open,       Kw_open)              \
    X(override,   Kw_override)          \
    X(virtual,    Kw_virtual)           \
    X(unsafe,     Kw_unsafe)            \
    /* funkcje */                       \
    X(fun,        Kw_fun)               \
    X(return,     Kw_return)            \
    X(async,      Kw_async)             \
    X(await,      Kw_await)             \
    X(yield,      Kw_yield)             \
    /* sterowanie */                    \
    X(if,         Kw_if)                \
    X(else,       Kw_else)              \
    X(while,      Kw_while)             \
    X(for,        Kw_for)               \
    X(do,         Kw_do)                \
    X(break,      Kw_break)             \
    X(continue,   Kw_continue)          \
    X(switch,     Kw_switch)            \
    X(case,       Kw_case)              \
    X(default,    Kw_default)           \
    X(match,      Kw_match)             \
    X(when,       Kw_when)              \
    /* pamięć (Zig-style) */            \
    X(defer,      Kw_defer)             \
    /* moduły */                        \
    X(import,     Kw_import)            \
    X(package,    Kw_package)           \
    X(module,     Kw_module)            \
    X(namespace,  Kw_namespace)         \
    X(use,        Kw_use)               \
    X(extern,     Kw_extern)            \
    X(as,         Kw_as)                \
    X(is,         Kw_is)                \
    /* błędy */                         \
    X(try,        Kw_try)               \
    X(catch,      Kw_catch)             \
    X(finally,    Kw_finally)           \
    X(throw,      Kw_throw)             \
    X(throws,     Kw_throws)            \
    /* literały */                      \
    X(null,       Kw_null)              \
    X(true,       Kw_true)              \
    X(false,      Kw_false)

enum class TokenKind : uint16_t {
    // literały / identyfikatory
    IntLit, FloatLit, StringLit, CharLit, Ident,

    // słowa kluczowe (generowane z X0P_KEYWORDS)
    #define X(str, kind) kind,
    X0P_KEYWORDS(X)
    #undef X

    // interpunkcja
    LParen, RParen, LBrace, RBrace, LBracket, RBracket,
    Semi, Comma, Dot, DotDot, DotDotDot, Colon, ColonColon, Arrow, FatArrow, At,

    // operatory
    Plus, Minus, Star, Slash, Percent,
    Amp, Pipe, Caret, Tilde, Bang,
    Eq, EqEq, BangEq, Lt, Gt, LtEq, GtEq,
    AmpAmp, PipePipe, Shl, Shr,
    PlusEq, MinusEq, StarEq, SlashEq, PercentEq,
    AmpEq, PipeEq, CaretEq, ShlEq, ShrEq,
    Question, QuestionQuestion,
    PlusPlus, MinusMinus,

    Eof,
    Unknown,
};

// Token — struktura MUSI być tutaj, bo lexer.h i parser.h jej używają.
struct Token {
    TokenKind       kind;
    std::string_view lexeme;   // widok w buforze źródła — zero kopii
    SourceSpan      span;
};

const char* token_kind_name(TokenKind k);

} // namespace x0p
