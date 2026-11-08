#include "x0p/ast.h"
#include <cstdio>
#include <string>

namespace x0p {

static void ind(int n) { for (int i = 0; i < n; ++i) std::fputc(' ', stderr); }

static const char* builtin_name(const Type* t) {
    if (!t) return "?";
    return std::string(t->name).c_str();
}

// --- Expr -------------------------------------------------------------------
void IntLitExpr::dump(int i) const   { ind(i); std::fprintf(stderr, "(int %lld)\n", (long long)value); }
void FloatLitExpr::dump(int i) const { ind(i); std::fprintf(stderr, "(float %f)\n", value); }
void StrLitExpr::dump(int i) const   { ind(i); std::fprintf(stderr, "(str '%.*s')\n", (int)value.size(), value.data()); }
void CharLitExpr::dump(int i) const  { ind(i); std::fprintf(stderr, "(char '%c')\n", value); }
void BoolLitExpr::dump(int i) const  { ind(i); std::fprintf(stderr, "(bool %s)\n", value ? "true" : "false"); }
void NullLitExpr::dump(int i) const  { ind(i); std::fprintf(stderr, "(null)\n"); }
void ThisExpr::dump(int i) const     { ind(i); std::fprintf(stderr, "(this)\n"); }
void IdentExpr::dump(int i) const    { ind(i); std::fprintf(stderr, "(id %.*s)\n", (int)name.size(), name.data()); }

void UnaryExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(unary %s\n", token_kind_name(op));
    operand->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void BinaryExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(binop %s\n", token_kind_name(op));
    lhs->dump(i + 2);
    rhs->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void AssignExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(assign %s\n", token_kind_name(op));
    target->dump(i + 2);
    value->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void CallExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(call\n");
    callee->dump(i + 2);
    for (auto* a : args) a->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void FieldExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(field %.*s\n", (int)name.size(), name.data());
    object->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void IndexExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(index\n");
    object->dump(i + 2);
    index->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void RangeExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(range %s\n", inclusive ? "inclusive" : "exclusive");
    lo->dump(i + 2);
    hi->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void NewExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(new %s\n", builtin_name(type));
    for (auto* a : args) a->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void ParenExpr::dump(int i) const {
    ind(i); std::fprintf(stderr, "(paren\n");
    inner->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}

// --- Stmt -------------------------------------------------------------------
void BlockStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(block\n");
    for (auto* s : stmts) s->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void ExprStmt::dump(int i) const   { expr->dump(i); }
void ReturnStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(return\n");
    if (value) value->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void IfStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(if\n");
    cond->dump(i + 2);
    then->dump(i + 2);
    if (els) { ind(i); std::fprintf(stderr, "(else\n"); els->dump(i + 2); ind(i); std::fprintf(stderr, ")\n"); }
    ind(i); std::fprintf(stderr, ")\n");
}
void WhileStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(while\n");
    cond->dump(i + 2); body->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void DoWhileStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(do-while\n");
    body->dump(i + 2); cond->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void ForInStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(for-in %.*s\n", (int)var.size(), var.data());
    iter->dump(i + 2); body->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void ForStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(for\n");
    if (init) init->dump(i + 2); else { ind(i + 2); std::fprintf(stderr, "(nil)\n"); }
    if (cond) cond->dump(i + 2); else { ind(i + 2); std::fprintf(stderr, "(nil)\n"); }
    if (post) post->dump(i + 2); else { ind(i + 2); std::fprintf(stderr, "(nil)\n"); }
    body->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void BreakStmt::dump(int i) const    { ind(i); std::fprintf(stderr, "(break)\n"); }
void ContinueStmt::dump(int i) const { ind(i); std::fprintf(stderr, "(continue)\n"); }
void DeferStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(defer\n");
    body->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}
void ValDeclStmt::dump(int i) const {
    ind(i); std::fprintf(stderr, "(%s %.*s\n",
                          is_mutable ? "var" : "val",
                          (int)name.size(), name.data());
    if (type) { ind(i + 2); std::fprintf(stderr, "(type %s)\n", builtin_name(type)); }
    if (init) init->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}

// --- Decl -------------------------------------------------------------------
static void dump_mods(const Modifiers& m, int i) {
    if (!m.any()) return;
    ind(i); std::fprintf(stderr, "(mods");
    if (m.is_public)    std::fprintf(stderr, " public");
    if (m.is_private)   std::fprintf(stderr, " private");
    if (m.is_protected) std::fprintf(stderr, " protected");
    if (m.is_internal)  std::fprintf(stderr, " internal");
    if (m.is_static)    std::fprintf(stderr, " static");
    if (m.is_abstract)  std::fprintf(stderr, " abstract");
    if (m.is_final)     std::fprintf(stderr, " final");
    if (m.is_sealed)    std::fprintf(stderr, " sealed");
    if (m.is_open)      std::fprintf(stderr, " open");
    if (m.is_override)  std::fprintf(stderr, " override");
    if (m.is_virtual)   std::fprintf(stderr, " virtual");
    if (m.is_async)     std::fprintf(stderr, " async");
    if (m.is_extern)    std::fprintf(stderr, " extern");
    std::fprintf(stderr, ")\n");
}

void PackageDecl::dump(int i) const {
    ind(i); std::fprintf(stderr, "(package %.*s)\n", (int)name.size(), name.data());
}
void ImportDecl::dump(int i) const {
    ind(i); std::fprintf(stderr, "(import %.*s)\n", (int)path.size(), path.data());
}

void FunDecl::dump(int i) const {
    ind(i); std::fprintf(stderr, "(fun %.*s\n", (int)name.size(), name.data());
    dump_mods(mods, i + 2);
    for (const auto& p : params) {
        ind(i + 2); std::fprintf(stderr, "(param %.*s %s)\n",
                                  (int)p.name.size(), p.name.data(),
                                  builtin_name(p.type));
    }
    ind(i + 2); std::fprintf(stderr, "(return-type %s)\n", builtin_name(return_type));
    if (body) body->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}

void EnumDecl::dump(int i) const {
    ind(i); std::fprintf(stderr, "(enum %.*s\n", (int)name.size(), name.data());
    dump_mods(mods, i + 2);
    for (const auto& m : members) {
        ind(i + 2); std::fprintf(stderr, "(member %.*s)\n", (int)m.name.size(), m.name.data());
    }
    ind(i); std::fprintf(stderr, ")\n");
}

void StructDecl::dump(int i) const {
    ind(i); std::fprintf(stderr, "(struct %.*s\n", (int)name.size(), name.data());
    dump_mods(mods, i + 2);
    for (const auto& f : fields) {
        ind(i + 2); std::fprintf(stderr, "(field %.*s %s)\n",
                                  (int)f.name.size(), f.name.data(), builtin_name(f.type));
    }
    ind(i); std::fprintf(stderr, ")\n");
}

void ClassDecl::dump(int i) const {
    ind(i); std::fprintf(stderr, "(class %.*s\n", (int)name.size(), name.data());
    dump_mods(mods, i + 2);
    if (!extends.empty()) {
        ind(i + 2); std::fprintf(stderr, "(extends %.*s)\n", (int)extends.size(), extends.data());
    }
    for (const auto& iface : implements) {
        ind(i + 2); std::fprintf(stderr, "(implements %.*s)\n", (int)iface.size(), iface.data());
    }
    for (const auto& f : fields) {
        ind(i + 2); std::fprintf(stderr, "(field %.*s %s)\n",
                                  (int)f.name.size(), f.name.data(), builtin_name(f.type));
    }
    for (auto* m : methods) m->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}

void InterfaceDecl::dump(int i) const {
    ind(i); std::fprintf(stderr, "(interface %.*s\n", (int)name.size(), name.data());
    dump_mods(mods, i + 2);
    for (auto* m : methods) m->dump(i + 2);
    ind(i); std::fprintf(stderr, ")\n");
}

void Program::dump() const {
    std::fprintf(stderr, "(program\n");
    for (auto* d : decls) d->dump(2);
    std::fprintf(stderr, ")\n");
}

} // namespace x0p
