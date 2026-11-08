#include "x0p/parser.h"
#include <cstdlib>

namespace x0p {

Parser::Parser(std::vector<Token> tokens, uint32_t file_id,
               Arena& arena, DiagnosticEngine& diag)
    : tokens_(std::move(tokens)), file_id_(file_id), arena_(arena), diag_(diag) {}

const Token& Parser::peek(size_t ahead) const {
    size_t p = pos_ + ahead;
    if (p >= tokens_.size()) return tokens_.back(); // Eof
    return tokens_[p];
}
const Token& Parser::advance() {
    const Token& t = tokens_[pos_];
    if (pos_ + 1 < tokens_.size()) ++pos_;
    return t;
}
bool Parser::check(TokenKind k) const { return peek().kind == k; }
bool Parser::match(TokenKind k) {
    if (check(k)) { advance(); return true; }
    return false;
}
const Token& Parser::expect(TokenKind k, const char* what) {
    if (check(k)) return advance();
    diag_.report({Severity::Error, "P0001",
                  {peek().span.start, peek().span.end, file_id_},
                  std::string("expected ") + what, {}});
    return peek();
}

void Parser::synchronize() {
    while (!check(TokenKind::Eof)) {
        switch (peek().kind) {
            case TokenKind::Semi: advance(); return;
            case TokenKind::RBrace: return;
            case TokenKind::Kw_class:
            case TokenKind::Kw_struct:
            case TokenKind::Kw_enum:
            case TokenKind::Kw_interface:
            case TokenKind::Kw_fun:
            case TokenKind::Kw_public:
            case TokenKind::Kw_private:
            case TokenKind::Kw_protected:
                return;
            default: advance();
        }
    }
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------
Type* Parser::parse_type() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    Type* t = arena_.make<Type>();
    t->span = sp;

    // prymitywy
    switch (peek().kind) {
        case TokenKind::Kw_void:   advance(); t->name = "void";   return t;
        case TokenKind::Kw_bool:   advance(); t->name = "bool";   return t;
        case TokenKind::Kw_char:   advance(); t->name = "char";   return t;
        case TokenKind::Kw_int:    advance(); t->name = "int";    return t;
        case TokenKind::Kw_float:  advance(); t->name = "float";  return t;
        case TokenKind::Kw_double: advance(); t->name = "double"; return t;
        default: break;
    }
    // nazwa użytkownika
    if (check(TokenKind::Ident)) {
        t->name = advance().lexeme;
        return t;
    }
    diag_.report({Severity::Error, "P0002",
                  {peek().span.start, peek().span.end, file_id_},
                  "expected type", {}});
    t->name = "<error>";
    return t;
}

// ---------------------------------------------------------------------------
// Modifiers
// ---------------------------------------------------------------------------
Modifiers Parser::parse_modifiers() {
    Modifiers m;
    for (;;) {
        switch (peek().kind) {
            case TokenKind::Kw_public:    advance(); m.is_public    = true; break;
            case TokenKind::Kw_private:   advance(); m.is_private   = true; break;
            case TokenKind::Kw_protected: advance(); m.is_protected = true; break;
            case TokenKind::Kw_internal:  advance(); m.is_internal  = true; break;
            case TokenKind::Kw_static:    advance(); m.is_static    = true; break;
            case TokenKind::Kw_abstract:  advance(); m.is_abstract  = true; break;
            case TokenKind::Kw_final:     advance(); m.is_final     = true; break;
            case TokenKind::Kw_sealed:    advance(); m.is_sealed    = true; break;
            case TokenKind::Kw_open:      advance(); m.is_open      = true; break;
            case TokenKind::Kw_override:  advance(); m.is_override  = true; break;
            case TokenKind::Kw_virtual:   advance(); m.is_virtual   = true; break;
            case TokenKind::Kw_async:     advance(); m.is_async     = true; break;
            case TokenKind::Kw_extern:    advance(); m.is_extern    = true; break;
            default: return m;
        }
    }
}

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------
Program Parser::parse_program() {
    Program prog;
    while (!check(TokenKind::Eof)) {
        try {
            Decl* d = parse_decl();
            if (d) prog.decls.push_back(d);
        } catch (...) {
            synchronize();
        }
        if (check(TokenKind::Eof)) break;
        // jeśli nic nie zjedliśmy, przesuń by nie zapętlić
        if (pos_ < tokens_.size() && tokens_[pos_].kind != TokenKind::Eof
            && prog.decls.empty()) {
            advance();
        }
    }
    return prog;
}

Decl* Parser::parse_decl() {
    if (check(TokenKind::Kw_package)) return parse_package();
    if (check(TokenKind::Kw_import) || check(TokenKind::Kw_use)) return parse_import();

    Modifiers m = parse_modifiers();
    SourceSpan start{peek().span.start, peek().span.end, file_id_};

    switch (peek().kind) {
        case TokenKind::Kw_enum:      return parse_enum(m, start);
        case TokenKind::Kw_struct:    return parse_struct(m, start);
        case TokenKind::Kw_class:     return parse_class(m, start);
        case TokenKind::Kw_interface: return parse_interface(m, start);
        case TokenKind::Kw_fun:       return parse_fun(m, start);
        default:
            diag_.report({Severity::Error, "P0003",
                          {peek().span.start, peek().span.end, file_id_},
                          "expected declaration (class/struct/enum/interface/fun)", {}});
            advance();
            return nullptr;
    }
}

Decl* Parser::parse_package() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    advance(); // package
    auto* d = arena_.make<PackageDecl>();
    d->span = sp;
    // prosty ident lub ścieżka a.b.c — łączymy w jeden string
    std::string path;
    while (check(TokenKind::Ident)) {
        path += std::string(advance().lexeme);
        if (!match(TokenKind::Dot)) break;
        path += '.';
    }
    d->name = arena_.make<std::string>(std::move(path))->c_str();
    return d;
}

Decl* Parser::parse_import() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    advance();
    auto* d = arena_.make<ImportDecl>();
    d->span = sp;
    std::string path;
    while (check(TokenKind::Ident)) {
        path += std::string(advance().lexeme);
        if (!match(TokenKind::Dot)) break;
        path += '.';
    }
    d->path = arena_.make<std::string>(std::move(path))->c_str();
    return d;
}

Decl* Parser::parse_enum(Modifiers m, SourceSpan span) {
    advance(); // enum
    auto* d = arena_.make<EnumDecl>();
    d->span = span;
    d->mods = m;
    d->name = expect(TokenKind::Ident, "enum name").lexeme;
    expect(TokenKind::LBrace, "'{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        EnumMember mem;
        mem.name = expect(TokenKind::Ident, "enum member").lexeme;
        mem.span = {peek().span.start, peek().span.end, file_id_};
        d->members.push_back(mem);
        if (!match(TokenKind::Comma)) break;
    }
    expect(TokenKind::RBrace, "'}'");
    return d;
}

Decl* Parser::parse_struct(Modifiers m, SourceSpan span) {
    advance();
    auto* d = arena_.make<StructDecl>();
    d->span = span;
    d->mods = m;
    d->name = expect(TokenKind::Ident, "struct name").lexeme;
    expect(TokenKind::LBrace, "'{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        SourceSpan fsp{peek().span.start, peek().span.end, file_id_};
        Modifiers fm = parse_modifiers();
        if (match(TokenKind::Kw_val) || match(TokenKind::Kw_var)) {
            FieldDecl f;
            f.span = fsp;
            f.mods = fm;
            f.name = expect(TokenKind::Ident, "field name").lexeme;
            expect(TokenKind::Colon, "':'");
            f.type = parse_type();
            if (match(TokenKind::Eq)) f.init = parse_expr();
            d->fields.push_back(f);
            match(TokenKind::Semi);
        } else {
            advance();
        }
    }
    expect(TokenKind::RBrace, "'}'");
    return d;
}

Decl* Parser::parse_class(Modifiers m, SourceSpan span) {
    advance();
    auto* d = arena_.make<ClassDecl>();
    d->span = span;
    d->mods = m;
    d->name = expect(TokenKind::Ident, "class name").lexeme;

    if (match(TokenKind::Kw_extends)) {
        d->extends = expect(TokenKind::Ident, "superclass name").lexeme;
    }
    if (match(TokenKind::Kw_implements)) {
        do {
            d->implements.push_back(expect(TokenKind::Ident, "interface name").lexeme);
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::LBrace, "'{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        SourceSpan mspan{peek().span.start, peek().span.end, file_id_};
        Modifiers mm = parse_modifiers();
        if (check(TokenKind::Kw_fun)) {
            d->methods.push_back(parse_fun(mm, mspan));
        } else if (check(TokenKind::Kw_val) || check(TokenKind::Kw_var)) {
            d->fields.push_back(parse_field(mm, mspan));
        } else {
            advance();
        }
    }
    expect(TokenKind::RBrace, "'}'");
    return d;
}

Decl* Parser::parse_interface(Modifiers m, SourceSpan span) {
    advance();
    auto* d = arena_.make<InterfaceDecl>();
    d->span = span;
    d->mods = m;
    d->name = expect(TokenKind::Ident, "interface name").lexeme;
    expect(TokenKind::LBrace, "'{'");
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        SourceSpan mspan{peek().span.start, peek().span.end, file_id_};
        Modifiers mm = parse_modifiers();
        if (check(TokenKind::Kw_fun)) {
            d->methods.push_back(parse_fun(mm, mspan));
        } else {
            advance();
        }
    }
    expect(TokenKind::RBrace, "'}'");
    return d;
}

FunDecl* Parser::parse_fun(Modifiers m, SourceSpan span) {
    advance(); // fun
    auto* fn = arena_.make<FunDecl>();
    fn->span = span;
    fn->mods = m;
    fn->name = expect(TokenKind::Ident, "function name").lexeme;
    expect(TokenKind::LParen, "'('");
    if (!check(TokenKind::RParen)) {
        do {
            Param p;
            p.span = {peek().span.start, peek().span.end, file_id_};
            p.name = expect(TokenKind::Ident, "param name").lexeme;
            expect(TokenKind::Colon, "':'");
            p.type = parse_type();
            fn->params.push_back(p);
        } while (match(TokenKind::Comma));
    }
    expect(TokenKind::RParen, "')'");
    fn->return_type = parse_type();

    if (match(TokenKind::Semi)) { fn->body = nullptr; return fn; }
    fn->body = parse_block();
    return fn;
}

FieldDecl Parser::parse_field(Modifiers m, SourceSpan span) {
    bool mut = match(TokenKind::Kw_var);
    if (!mut) match(TokenKind::Kw_val);
    FieldDecl f;
    f.span = span;
    f.mods = m;
    f.name = expect(TokenKind::Ident, "field name").lexeme;
    expect(TokenKind::Colon, "':'");
    f.type = parse_type();
    if (match(TokenKind::Eq)) f.init = parse_expr();
    match(TokenKind::Semi);
    return f;
}

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
Stmt* Parser::parse_block() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    expect(TokenKind::LBrace, "'{'");
    auto* b = arena_.make<BlockStmt>();
    b->span = sp;
    while (!check(TokenKind::RBrace) && !check(TokenKind::Eof)) {
        size_t before = pos_;
        Stmt* s = parse_stmt();
        if (s) b->stmts.push_back(s);
        if (pos_ == before && !check(TokenKind::Eof)) advance(); // safety
    }
    expect(TokenKind::RBrace, "'}'");
    return b;
}

Stmt* Parser::parse_stmt() {
    switch (peek().kind) {
        case TokenKind::LBrace:      return parse_block();
        case TokenKind::Kw_val:
        case TokenKind::Kw_var:
        case TokenKind::Kw_let:      return parse_val_or_var();
        case TokenKind::Kw_if:       return parse_if();
        case TokenKind::Kw_while:    return parse_while();
        case TokenKind::Kw_do:       return parse_do_while();
        case TokenKind::Kw_for:      return parse_for();
        case TokenKind::Kw_return:   return parse_return();
        case TokenKind::Kw_defer:    return parse_defer();
        case TokenKind::Kw_break: {
            auto* s = arena_.make<BreakStmt>();
            s->span = {peek().span.start, peek().span.end, file_id_};
            advance(); match(TokenKind::Semi);
            return s;
        }
        case TokenKind::Kw_continue: {
            auto* s = arena_.make<ContinueStmt>();
            s->span = {peek().span.start, peek().span.end, file_id_};
            advance(); match(TokenKind::Semi);
            return s;
        }
        default: break;
    }
    // expression statement
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    auto* s = arena_.make<ExprStmt>();
    s->span = sp;
    s->expr = parse_expr();
    match(TokenKind::Semi);
    return s;
}

Stmt* Parser::parse_val_or_var() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    bool mut = check(TokenKind::Kw_var);
    advance(); // val/var/let
    auto* d = arena_.make<ValDeclStmt>();
    d->span = sp;
    d->is_mutable = mut;
    d->name = expect(TokenKind::Ident, "variable name").lexeme;
    if (match(TokenKind::Colon)) d->type = parse_type();
    if (match(TokenKind::Eq))    d->init = parse_expr();
    match(TokenKind::Semi);
    return d;
}

Stmt* Parser::parse_if() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    advance(); // if
    auto* s = arena_.make<IfStmt>();
    s->span = sp;
    s->cond = parse_expr();
    s->then = parse_block();
    if (match(TokenKind::Kw_else)) {
        if (check(TokenKind::Kw_if)) s->els = parse_if();
        else                         s->els = parse_block();
    }
    return s;
}

Stmt* Parser::parse_while() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    advance();
    auto* s = arena_.make<WhileStmt>();
    s->span = sp;
    s->cond = parse_expr();
    s->body = parse_block();
    return s;
}

Stmt* Parser::parse_do_while() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    advance();
    auto* s = arena_.make<DoWhileStmt>();
    s->span = sp;
    s->body = parse_block();
    expect(TokenKind::Kw_while, "'while'");
    s->cond = parse_expr();
    match(TokenKind::Semi);
    return s;
}

Stmt* Parser::parse_for() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    advance(); // for

    // for x in iter { ... }
    if (check(TokenKind::Ident) && peek(1).kind == TokenKind::Kw_in) {
        auto* s = arena_.make<ForInStmt>();
        s->span = sp;
        s->var = advance().lexeme;
        advance(); // in
        s->iter = parse_expr();
        s->body = parse_block();
        return s;
    }
    // for init; cond; post { ... }  (C-style, val/var init)
    auto* s = arena_.make<ForStmt>();
    s->span = sp;
    s->init = parse_stmt(); // zwykle ValDeclStmt lub ExprStmt
    s->cond = parse_expr();
    expect(TokenKind::Semi, "';'");
    s->post = parse_expr();
    s->body = parse_block();
    return s;
}

Stmt* Parser::parse_return() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    advance();
    auto* s = arena_.make<ReturnStmt>();
    s->span = sp;
    if (!check(TokenKind::Semi) && !check(TokenKind::RBrace) && !check(TokenKind::Eof))
        s->value = parse_expr();
    match(TokenKind::Semi);
    return s;
}

Stmt* Parser::parse_defer() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    advance();
    auto* s = arena_.make<DeferStmt>();
    s->span = sp;
    // `defer { ... }`  lub  `defer stmt` (Go-style, bez klamer)
    if (check(TokenKind::LBrace)) s->body = parse_block();
    else                          s->body = parse_stmt();
    return s;
}

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
static int prec_of(TokenKind k) {
    switch (k) {
        case TokenKind::QuestionQuestion: return 1;
        case TokenKind::PipePipe:         return 2;
        case TokenKind::AmpAmp:           return 3;
        case TokenKind::Pipe:             return 4;
        case TokenKind::Caret:            return 5;
        case TokenKind::Amp:              return 6;
        case TokenKind::EqEq:
        case TokenKind::BangEq:           return 7;
        case TokenKind::Lt: case TokenKind::Gt:
        case TokenKind::LtEq: case TokenKind::GtEq: return 8;
        case TokenKind::Shl: case TokenKind::Shr:   return 9;
        case TokenKind::Plus: case TokenKind::Minus: return 10;
        case TokenKind::Star: case TokenKind::Slash: case TokenKind::Percent: return 11;
        default: return -1;
    }
}
int Parser::precedence(TokenKind k) { return prec_of(k); }

Expr* Parser::parse_expr() { return parse_assignment(); }

Expr* Parser::parse_assignment() {
    Expr* lhs = parse_binary(1);
    switch (peek().kind) {
        case TokenKind::Eq: case TokenKind::PlusEq: case TokenKind::MinusEq:
        case TokenKind::StarEq: case TokenKind::SlashEq: case TokenKind::PercentEq: {
            auto* a = arena_.make<AssignExpr>();
            a->span = lhs->span;
            a->op = advance().kind;
            a->target = lhs;
            a->value = parse_assignment();
            return a;
        }
        default: return lhs;
    }
}

Expr* Parser::parse_binary(int min_prec) {
    Expr* lhs = parse_unary();
    for (;;) {
        int prec = precedence(peek().kind);
        if (prec < min_prec) break;
        TokenKind op = advance().kind;
        Expr* rhs = parse_binary(prec + 1);
        auto* b = arena_.make<BinaryExpr>();
        b->span = lhs->span;
        b->op = op;
        b->lhs = lhs;
        b->rhs = rhs;
        lhs = b;
    }
    // range `..` / `..=` (traktujemy jako postfix po lhs)
    if (check(TokenKind::DotDot) || check(TokenKind::DotDotDot)) {
        bool inclusive = advance().kind == TokenKind::DotDotDot;
        auto* r = arena_.make<RangeExpr>();
        r->span = lhs->span;
        r->lo = lhs;
        r->hi = parse_binary(min_prec);
        r->inclusive = inclusive;
        return r;
    }
    return lhs;
}

Expr* Parser::parse_unary() {
    switch (peek().kind) {
        case TokenKind::Minus:
        case TokenKind::Bang:
        case TokenKind::Tilde:
        case TokenKind::Plus: {
            SourceSpan sp{peek().span.start, peek().span.end, file_id_};
            TokenKind op = advance().kind;
            auto* u = arena_.make<UnaryExpr>();
            u->span = sp;
            u->op = op;
            u->operand = parse_unary();
            return u;
        }
        default: return parse_postfix();
    }
}

Expr* Parser::parse_postfix() {
    Expr* e = parse_primary();
    for (;;) {
        if (match(TokenKind::LParen)) {
            auto* c = arena_.make<CallExpr>();
            c->span = e->span;
            c->callee = e;
            if (!check(TokenKind::RParen)) {
                do { c->args.push_back(parse_expr()); } while (match(TokenKind::Comma));
            }
            expect(TokenKind::RParen, "')'");
            e = c;
        } else if (match(TokenKind::Dot)) {
            auto* f = arena_.make<FieldExpr>();
            f->span = e->span;
            f->object = e;
            f->name = expect(TokenKind::Ident, "field name").lexeme;
            e = f;
        } else if (match(TokenKind::LBracket)) {
            auto* ix = arena_.make<IndexExpr>();
            ix->span = e->span;
            ix->object = e;
            ix->index = parse_expr();
            expect(TokenKind::RBracket, "']'");
            e = ix;
        } else break;
    }
    return e;
}

Expr* Parser::parse_primary() {
    SourceSpan sp{peek().span.start, peek().span.end, file_id_};
    switch (peek().kind) {
        case TokenKind::IntLit: {
            auto* e = arena_.make<IntLitExpr>();
            e->span = sp;
            e->value = std::strtoll(std::string(advance().lexeme).c_str(), nullptr, 10);
            return e;
        }
        case TokenKind::FloatLit: {
            auto* e = arena_.make<FloatLitExpr>();
            e->span = sp;
            e->value = std::strtod(std::string(advance().lexeme).c_str(), nullptr);
            return e;
        }
        case TokenKind::StringLit: {
            auto* e = arena_.make<StrLitExpr>();
            e->span = sp;
            e->value = advance().lexeme;
            return e;
        }
        case TokenKind::CharLit: {
            auto* e = arena_.make<CharLitExpr>();
            e->span = sp;
            advance();
            e->value = '?';
            return e;
        }
        case TokenKind::Kw_true:
        case TokenKind::Kw_false: {
            auto* e = arena_.make<BoolLitExpr>();
            e->span = sp;
            e->value = peek().kind == TokenKind::Kw_true;
            advance();
            return e;
        }
        case TokenKind::Kw_null: {
            auto* e = arena_.make<NullLitExpr>();
            e->span = sp;
            advance();
            return e;
        }
        case TokenKind::Kw_this: {
            auto* e = arena_.make<ThisExpr>();
            e->span = sp;
            advance();
            return e;
        }
        case TokenKind::Kw_new: {
            advance();
            auto* e = arena_.make<NewExpr>();
            e->span = sp;
            e->type = parse_type();
            if (match(TokenKind::LParen)) {
                if (!check(TokenKind::RParen))
                    do { e->args.push_back(parse_expr()); } while (match(TokenKind::Comma));
                expect(TokenKind::RParen, "')'");
            }
            return e;
        }
        case TokenKind::LParen: {
            advance();
            auto* e = arena_.make<ParenExpr>();
            e->span = sp;
            e->inner = parse_expr();
            expect(TokenKind::RParen, "')'");
            return e;
        }
        case TokenKind::Ident: {
            auto* e = arena_.make<IdentExpr>();
            e->span = sp;
            e->name = advance().lexeme;
            return e;
        }
        default: {
            diag_.report({Severity::Error, "P0004",
                          {peek().span.start, peek().span.end, file_id_},
                          "expected expression", {}});
            auto* e = arena_.make<IntLitExpr>();
            e->span = sp; e->value = 0;
            advance();
            return e;
        }
    }
}

} // namespace x0p




