#pragma once
#include "x0p/arena.h"
#include "x0p/ast.h"
#include "x0p/diagnostic.h"
#include "x0p/token.h"
#include <vector>

namespace x0p {

class Parser {
public:
    Parser(std::vector<Token> tokens, uint32_t file_id,
           Arena& arena, DiagnosticEngine& diag);

    Program parse_program();

private:
    // helpers
    const Token& peek(size_t ahead = 0) const;
    const Token& advance();
    bool         check(TokenKind k) const;
    bool         match(TokenKind k);
    const Token& expect(TokenKind k, const char* what);
    void         synchronize();

    // types
    Type* parse_type();

    // declarations
    Modifiers        parse_modifiers();
    Decl*            parse_decl();
    Decl*            parse_package();
    Decl*            parse_import();
    Decl*            parse_enum(Modifiers m, SourceSpan span);
    Decl*            parse_struct(Modifiers m, SourceSpan span);
    Decl*            parse_class(Modifiers m, SourceSpan span);
    Decl*            parse_interface(Modifiers m, SourceSpan span);
    FunDecl*         parse_fun(Modifiers m, SourceSpan span);
    FieldDecl        parse_field(Modifiers m, SourceSpan span);

    // statements
    Stmt*            parse_stmt();
    Stmt*            parse_block();
    Stmt*            parse_val_or_var();
    Stmt*            parse_if();
    Stmt*            parse_while();
    Stmt*            parse_do_while();
    Stmt*            parse_for();
    Stmt*            parse_return();
    Stmt*            parse_defer();

    // expressions (precedence climbing)
    Expr* parse_expr();
    Expr* parse_assignment();
    Expr* parse_binary(int min_prec);
    Expr* parse_unary();
    Expr* parse_postfix();
    Expr* parse_primary();
    Expr* parse_range(Expr* lo);

    static int precedence(TokenKind k);

    std::vector<Token> tokens_;
    size_t             pos_ = 0;
    uint32_t           file_id_;
    Arena&             arena_;
    DiagnosticEngine&  diag_;
};

} // namespace x0p
