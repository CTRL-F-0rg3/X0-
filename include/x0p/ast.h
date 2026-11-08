#pragma once
#include "x0p/token.h"
#include <cstdint>
#include <string_view>
#include <vector>

namespace x0p {

struct Expr;
struct Stmt;
struct Decl;
struct Type;

// ---------------------------------------------------------------------------
// Type — na razie prosta reprezentacja, rozbudujemy przy typach generycznych.
// ---------------------------------------------------------------------------
struct Type {
    enum class Kind { Builtin, Named, Pointer, Array } kind = Kind::Builtin;
    std::string_view name;          // "int", "String", "Foo"
    Type*            elem = nullptr; // dla Pointer / Array
    SourceSpan       span;
};

// ---------------------------------------------------------------------------
// Modifiers — flagi (public/private/static/final/...)
// ---------------------------------------------------------------------------
struct Modifiers {
    bool is_public    = false;
    bool is_private   = false;
    bool is_protected = false;
    bool is_internal  = false;
    bool is_static    = false;
    bool is_abstract  = false;
    bool is_final     = false;
    bool is_sealed    = false;
    bool is_open      = false;
    bool is_override  = false;
    bool is_virtual   = false;
    bool is_async     = false;
    bool is_extern    = false;
    bool any() const {
        return is_public|is_private|is_protected|is_internal|is_static
             |is_abstract|is_final|is_sealed|is_open|is_override|is_virtual
             |is_async|is_extern;
    }
};

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------
struct Expr {
    SourceSpan span;
    virtual ~Expr() = default;
    virtual void dump(int indent) const = 0;
};

struct IntLitExpr  : Expr { int64_t value = 0;                void dump(int i) const override; };
struct FloatLitExpr: Expr { double  value = 0;                void dump(int i) const override; };
struct StrLitExpr  : Expr { std::string_view value;           void dump(int i) const override; };
struct CharLitExpr : Expr { char    value = 0;                void dump(int i) const override; };
struct BoolLitExpr : Expr { bool    value = false;            void dump(int i) const override; };
struct NullLitExpr : Expr {                                   void dump(int i) const override; };
struct ThisExpr    : Expr {                                   void dump(int i) const override; };
struct IdentExpr   : Expr { std::string_view name;            void dump(int i) const override; };

struct UnaryExpr   : Expr { TokenKind op; Expr* operand;      void dump(int i) const override; };
struct BinaryExpr  : Expr { TokenKind op; Expr* lhs; Expr* rhs; void dump(int i) const override; };
struct AssignExpr  : Expr { TokenKind op; Expr* target; Expr* value; void dump(int i) const override; };
struct CallExpr    : Expr { Expr* callee; std::vector<Expr*> args; void dump(int i) const override; };
struct FieldExpr   : Expr { Expr* object; std::string_view name; void dump(int i) const override; };
struct IndexExpr   : Expr { Expr* object; Expr* index;         void dump(int i) const override; };
struct RangeExpr   : Expr { Expr* lo; Expr* hi; bool inclusive; void dump(int i) const override; };
struct NewExpr     : Expr { Type* type; std::vector<Expr*> args; void dump(int i) const override; };
struct ParenExpr   : Expr { Expr* inner;                       void dump(int i) const override; };

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------
struct Stmt {
    SourceSpan span;
    virtual ~Stmt() = default;
    virtual void dump(int indent) const = 0;
};

struct BlockStmt   : Stmt { std::vector<Stmt*> stmts;         void dump(int i) const override; };
struct ExprStmt    : Stmt { Expr* expr;                        void dump(int i) const override; };
struct ReturnStmt  : Stmt { Expr* value = nullptr;             void dump(int i) const override; };
struct IfStmt      : Stmt { Expr* cond; Stmt* then; Stmt* els = nullptr; void dump(int i) const override; };
struct WhileStmt   : Stmt { Expr* cond; Stmt* body;            void dump(int i) const override; };
struct DoWhileStmt : Stmt { Stmt* body; Expr* cond;            void dump(int i) const override; };
struct ForInStmt   : Stmt { std::string_view var; Expr* iter; Stmt* body; void dump(int i) const override; };
struct ForStmt     : Stmt { Stmt* init; Expr* cond; Expr* post; Stmt* body; void dump(int i) const override; };
struct BreakStmt   : Stmt {                                    void dump(int i) const override; };
struct ContinueStmt: Stmt {                                    void dump(int i) const override; };
struct DeferStmt   : Stmt { Stmt* body;                        void dump(int i) const override; };
struct ValDeclStmt : Stmt {
    bool             is_mutable;   // true = var, false = val
    std::string_view name;
    Type*            type = nullptr;  // może być nullptr (inferencja)
    Expr*            init = nullptr;
    void dump(int i) const override;
};

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------
struct Decl {
    SourceSpan span;
    Modifiers  mods;
    virtual ~Decl() = default;
    virtual void dump(int indent) const = 0;
};

struct PackageDecl  : Decl { std::string_view name;                                    void dump(int i) const override; };
struct ImportDecl   : Decl { std::string_view path;                                    void dump(int i) const override; };

struct Param {
    std::string_view name;
    Type*            type;
    SourceSpan       span;
};

struct FunDecl      : Decl {
    std::string_view       name;
    std::vector<Param>     params;
    Type*                  return_type;
    Stmt*                  body = nullptr;
    void dump(int i) const override;
};

struct EnumMember   { std::string_view name; SourceSpan span; };
struct EnumDecl     : Decl {
    std::string_view            name;
    std::vector<EnumMember>     members;
    void dump(int i) const override;
};

struct FieldDecl {
    Modifiers        mods;
    std::string_view name;
    Type*            type;
    Expr*            init = nullptr;
    SourceSpan       span;
};

struct StructDecl   : Decl {
    std::string_view        name;
    std::vector<FieldDecl>  fields;
    void dump(int i) const override;
};

struct ClassDecl    : Decl {
    std::string_view          name;
    std::string_view          extends;       // "" = brak
    std::vector<std::string_view> implements;
    std::vector<FieldDecl>    fields;
    std::vector<FunDecl*>     methods;
    void dump(int i) const override;
};

struct InterfaceDecl: Decl {
    std::string_view        name;
    std::vector<FunDecl*>   methods;
    void dump(int i) const override;
};

// ---------------------------------------------------------------------------
// Program — korzeń AST
// ---------------------------------------------------------------------------
struct Program {
    std::vector<Decl*> decls;
    void dump() const;
};

} // namespace x0p
