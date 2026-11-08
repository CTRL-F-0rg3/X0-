#pragma once
#include "x0p/arena.h"
#include "x0p/diagnostic.h"
#include "x0p/token.h"
#include <string_view>
#include <vector>

namespace x0p {

class Lexer {
public:
    Lexer(std::string_view source, uint32_t file_id,
          Arena& arena, DiagnosticEngine& diag);

    std::vector<Token> tokenize();

private:
    Token next();
    void  skip_whitespace_and_comments();
    Token make(TokenKind k, size_t start);

    Token lex_number();
    Token lex_ident_or_keyword();
    Token lex_string();
    Token lex_char();
    Token lex_punct();

    char peek(size_t ahead = 0) const;
    char advance();
    bool match(char c);
    SourceLocation loc() const;

    std::string_view    src_;
    size_t              pos_            = 0;
    uint32_t            line_           = 1;
    uint32_t            col_            = 1;
    // pozycja startowa aktualnie leksowanego tokenu
    size_t              tok_start_pos_  = 0;
    uint32_t            tok_start_line_ = 1;
    uint32_t            tok_start_col_  = 1;
    uint32_t            file_id_;
    Arena&              arena_;
    DiagnosticEngine&   diag_;
};

} // namespace x0p
