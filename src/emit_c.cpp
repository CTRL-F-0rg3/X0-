#include "x0p/emit_c.h"
#include "x0p/options.h"
#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace x0p {
namespace {

struct TypeInfo {
    enum Kind { Int, Float, Bool, Char, String, UserPtr, Void, Unknown } kind = Unknown;
    std::string name;  // dla UserPtr
    bool is_ptr() const { return kind == UserPtr; }
};

TypeInfo from_type(Type* t) {
    if (!t) return {TypeInfo::Unknown, ""};
    std::string n(t->name);
    if (n == "int")    return {TypeInfo::Int, ""};
    if (n == "float")  return {TypeInfo::Float, ""};
    if (n == "double") return {TypeInfo::Float, ""};
    if (n == "bool")   return {TypeInfo::Bool, ""};
    if (n == "char")   return {TypeInfo::Char, ""};
    if (n == "String") return {TypeInfo::String, ""};
    if (n == "void")   return {TypeInfo::Void, ""};
    return {TypeInfo::UserPtr, n};
}

std::string c_type(const TypeInfo& t) {
    switch (t.kind) {
        case TypeInfo::Int:    return "int64_t";
        case TypeInfo::Float:  return "double";
        case TypeInfo::Bool:   return "bool";
        case TypeInfo::Char:   return "char";
        case TypeInfo::String: return "x0p_str";
        case TypeInfo::UserPtr:return t.name + "*";
        case TypeInfo::Void:   return "void";
        default:               return "int64_t";
    }
}

struct FieldInfo   { std::string name; Type* type; Expr* init; };
struct ClassInfo   {
    std::string              name;
    std::vector<FieldInfo>   fields;
    std::vector<FunDecl*>    methods;
};
struct EnumInfo    { std::string name; };

struct Emitter {
    const EmitOptions* opts_ = nullptr;
    std::ostringstream                          out;
    std::unordered_map<std::string, ClassInfo>  classes;
    std::unordered_map<std::string, EnumInfo>   enums;
    std::unordered_map<std::string, TypeInfo>   functions;   // free fn: name -> return type
    std::unordered_map<std::string, TypeInfo>   locals;
    std::string                                 current_class;

    std::vector<Stmt*>    active_defers;
    std::vector<size_t>   loop_boundary;
    int                   indent    = 0;

    void line(const std::string& s) {
        for (int i = 0; i < indent; ++i) out << "    ";
        out << s << "\n";
    }
    void raw(const std::string& s) { out << s << "\n"; }
    void inc() { ++indent; }
    void dec() { --indent; }

    ClassInfo* find_class(const std::string& n) {
        auto it = classes.find(n);
        return it == classes.end() ? nullptr : &it->second;
    }
    std::string find_method_owner(const std::string& m) {
        std::string found;
        for (auto& [cn, ci] : classes)
            for (auto* fn : ci.methods)
                if (fn->name == m) {
                    if (!found.empty() && found != cn) return "";
                    found = cn;
                }
        return found;
    }

    // -----------------------------------------------------------------------
    // Type inference
    // -----------------------------------------------------------------------
    TypeInfo infer(Expr* e) {
        if (!e) return {TypeInfo::Unknown, ""};
        if (dynamic_cast<IntLitExpr*>(e))   return {TypeInfo::Int, ""};
        if (dynamic_cast<FloatLitExpr*>(e)) return {TypeInfo::Float, ""};
        if (dynamic_cast<StrLitExpr*>(e))   return {TypeInfo::String, ""};
        if (dynamic_cast<CharLitExpr*>(e))  return {TypeInfo::Char, ""};
        if (dynamic_cast<BoolLitExpr*>(e))  return {TypeInfo::Bool, ""};
        if (dynamic_cast<NullLitExpr*>(e))  return {TypeInfo::Unknown, ""};
        if (dynamic_cast<ThisExpr*>(e))     return {TypeInfo::UserPtr, current_class};

        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            auto it = locals.find(std::string(id->name));
            if (it != locals.end()) return it->second;
            if (auto* ci = find_class(current_class))
                for (auto& f : ci->fields)
                    if (f.name == id->name) return from_type(f.type);
            return {TypeInfo::Unknown, ""};
        }
        if (auto* p = dynamic_cast<ParenExpr*>(e))
            return infer(p->inner);
        if (auto* u = dynamic_cast<UnaryExpr*>(e)) {
            if (u->op == TokenKind::Bang) return {TypeInfo::Bool, ""};
            return infer(u->operand);
        }
        if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
            auto l = infer(b->lhs), r = infer(b->rhs);
            if (b->op == TokenKind::Plus &&
                (l.kind == TypeInfo::String || r.kind == TypeInfo::String))
                return {TypeInfo::String, ""};
            switch (b->op) {
                case TokenKind::EqEq: case TokenKind::BangEq:
                case TokenKind::Lt:   case TokenKind::LtEq:
                case TokenKind::Gt:   case TokenKind::GtEq:
                case TokenKind::AmpAmp: case TokenKind::PipePipe:
                    return {TypeInfo::Bool, ""};
                default: break;
            }
            if (l.kind == TypeInfo::Float || r.kind == TypeInfo::Float)
                return {TypeInfo::Float, ""};
            return l.kind != TypeInfo::Unknown ? l : r;
        }
        if (auto* a = dynamic_cast<AssignExpr*>(e)) return infer(a->value);
        if (auto* f = dynamic_cast<FieldExpr*>(e)) {
            if (auto* oid = dynamic_cast<IdentExpr*>(f->object))
                if (enums.count(std::string(oid->name)))
                    return {TypeInfo::Int, ""};
            auto ot = infer(f->object);
            if (ot.kind == TypeInfo::UserPtr) {
                if (auto* ci = find_class(ot.name))
                    for (auto& fld : ci->fields)
                        if (fld.name == f->name) return from_type(fld.type);
            }
            return {TypeInfo::Unknown, ""};
        }
        if (auto* c = dynamic_cast<CallExpr*>(e)) {
            if (auto* id = dynamic_cast<IdentExpr*>(c->callee)) {
                if (id->name == "print") return {TypeInfo::Void, ""};
                auto it = functions.find(std::string(id->name));
                if (it != functions.end()) return it->second;
            }
            if (auto* f = dynamic_cast<FieldExpr*>(c->callee)) {
                auto owner = find_method_owner(std::string(f->name));
                if (!owner.empty()) {
                    if (auto* ci = find_class(owner))
                        for (auto* m : ci->methods)
                            if (m->name == f->name) return from_type(m->return_type);
                }
            }
            return {TypeInfo::Unknown, ""};
        }
        if (auto* nw = dynamic_cast<NewExpr*>(e))
            return {TypeInfo::UserPtr, nw->type ? std::string(nw->type->name) : ""};
        return {TypeInfo::Unknown, ""};
    }

    // -----------------------------------------------------------------------
    // Expr → string
    // -----------------------------------------------------------------------
    std::string expr(Expr* e) {
        if (auto* n = dynamic_cast<IntLitExpr*>(e))   return std::to_string(n->value);
        if (auto* f = dynamic_cast<FloatLitExpr*>(e)) {
            char b[64]; std::snprintf(b, sizeof b, "%g", f->value); return b;
        }
        if (auto* s = dynamic_cast<StrLitExpr*>(e)) {
            std::string v(s->value);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                v = v.substr(1, v.size() - 2);
            std::string esc;
            for (char ch : v) {
                switch (ch) {
                    case '\\': esc += "\\\\"; break;
                    case '"':  esc += "\\\""; break;
                    case '\n': esc += "\\n";  break;
                    case '\t': esc += "\\t";  break;
                    default:   esc += ch;
                }
            }
            return "\"" + esc + "\"";
        }
        if (auto* c = dynamic_cast<CharLitExpr*>(e)) return "'" + std::string(1, c->value) + "'";
        if (auto* b = dynamic_cast<BoolLitExpr*>(e)) return b->value ? "true" : "false";
        if (dynamic_cast<NullLitExpr*>(e)) return "NULL";
        if (dynamic_cast<ThisExpr*>(e))    return "self";
        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            auto it = locals.find(std::string(id->name));
            if (it != locals.end()) return std::string(id->name);
            // implicit this.field
            if (auto* ci = find_class(current_class))
                for (auto& f : ci->fields)
                    if (f.name == id->name) return "self->" + std::string(id->name);
            return std::string(id->name);
        }
        if (auto* p = dynamic_cast<ParenExpr*>(e))
            return "(" + expr(p->inner) + ")";
        if (auto* u = dynamic_cast<UnaryExpr*>(e)) {
            std::string ops;
            switch (u->op) {
                case TokenKind::Minus: ops = "-"; break;
                case TokenKind::Plus:  ops = "+"; break;
                case TokenKind::Bang:  ops = "!"; break;
                case TokenKind::Tilde: ops = "~"; break;
                default: break;
            }
            return "(" + ops + expr(u->operand) + ")";
        }
        if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
            auto lt = infer(b->lhs), rt = infer(b->rhs);
            if (b->op == TokenKind::Plus &&
                (lt.kind == TypeInfo::String || rt.kind == TypeInfo::String))
                return "x0p_strcat(" + expr(b->lhs) + ", " + expr(b->rhs) + ")";
            std::string ops;
            switch (b->op) {
                case TokenKind::Plus:     ops = "+";   break;
                case TokenKind::Minus:    ops = "-";   break;
                case TokenKind::Star:     ops = "*";   break;
                case TokenKind::Slash:    ops = "/";   break;
                case TokenKind::Percent:  ops = "%";   break;
                case TokenKind::EqEq:     ops = "==";  break;
                case TokenKind::BangEq:   ops = "!=";  break;
                case TokenKind::Lt:       ops = "<";   break;
                case TokenKind::LtEq:     ops = "<=";  break;
                case TokenKind::Gt:       ops = ">";   break;
                case TokenKind::GtEq:     ops = ">=";  break;
                case TokenKind::Amp:      ops = "&";   break;
                case TokenKind::Pipe:     ops = "|";   break;
                case TokenKind::Caret:    ops = "^";   break;
                case TokenKind::Shl:      ops = "<<";  break;
                case TokenKind::Shr:      ops = ">>";  break;
                case TokenKind::AmpAmp:   ops = "&&";  break;
                case TokenKind::PipePipe: ops = "||";  break;
                default:                  ops = "?";   break;
            }
            return "(" + expr(b->lhs) + " " + ops + " " + expr(b->rhs) + ")";
        }
        if (auto* a = dynamic_cast<AssignExpr*>(e)) {
            std::string ops;
            switch (a->op) {
                case TokenKind::Eq:        ops = "=";  break;
                case TokenKind::PlusEq:    ops = "+="; break;
                case TokenKind::MinusEq:   ops = "-="; break;
                case TokenKind::StarEq:    ops = "*="; break;
                case TokenKind::SlashEq:   ops = "/="; break;
                case TokenKind::PercentEq: ops = "%="; break;
                default:                   ops = "=";  break;
            }
            return "(" + expr(a->target) + " " + ops + " " + expr(a->value) + ")";
        }
        if (auto* c = dynamic_cast<CallExpr*>(e))  return call_str(c);
        if (auto* f = dynamic_cast<FieldExpr*>(e)) return field_str(f);
        if (auto* ix = dynamic_cast<IndexExpr*>(e))
            return expr(ix->object) + "[" + expr(ix->index) + "]";
        if (auto* nw = dynamic_cast<NewExpr*>(e)) {
            std::string t = nw->type ? std::string(nw->type->name) : "void";
            if (!nw->args.empty())
                std::fprintf(stderr, "genc: 'new %s(...)' args ignored (no ctor in v0.1)\n", t.c_str());
            return t + "_new()";
        }
        return "0";
    }

    std::string field_str(FieldExpr* f) {
        if (auto* oid = dynamic_cast<IdentExpr*>(f->object)) {
            if (enums.count(std::string(oid->name)))
                return std::string(oid->name) + "_" + std::string(f->name);
        }
        return expr(f->object) + "->" + std::string(f->name);
    }

    std::string call_str(CallExpr* c) {
        if (auto* id = dynamic_cast<IdentExpr*>(c->callee)) {
            if (id->name == "print" && c->args.size() == 1) {
                auto t = infer(c->args[0]);
                std::string fn;
                switch (t.kind) {
                    case TypeInfo::String: fn = "x0p_print_str"; break;
                    case TypeInfo::Bool:   fn = "x0p_print_bool"; break;
                    case TypeInfo::Float:  fn = "x0p_print_float"; break;
                    case TypeInfo::UserPtr: fn = "x0p_print_ptr"; break;
                    default:               fn = "x0p_print_int"; break;
                }
                return fn + "(" + expr(c->args[0]) + ")";
            }
        }
        // method call obj.method(args) -> Owner_method(obj, args)
        if (auto* f = dynamic_cast<FieldExpr*>(c->callee)) {
            std::string mname(f->name);
            std::string owner = find_method_owner(mname);
            if (!owner.empty()) {
                std::string s = owner + "_" + mname + "(" + expr(f->object);
                for (auto* a : c->args) s += ", " + expr(a);
                s += ")";
                return s;
            }
        }
        // free function call
        std::string callee = expr(c->callee);
        std::string s = callee + "(";
        for (size_t i = 0; i < c->args.size(); ++i) {
            if (i) s += ", ";
            s += expr(c->args[i]);
        }
        s += ")";
        return s;
    }

    // -----------------------------------------------------------------------
    // Defers
    // -----------------------------------------------------------------------
    void emit_defers_from(size_t dstart) {
        if (active_defers.size() <= dstart) return;
        for (size_t i = active_defers.size(); i > dstart; --i)
            emit_stmt(active_defers[i - 1]);
        active_defers.resize(dstart);
    }

    // -----------------------------------------------------------------------
    // Stmt
    // -----------------------------------------------------------------------
    void emit_stmt(Stmt* s) {
        if (!s) return;

        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            size_t dstart = active_defers.size();
            for (auto* st : b->stmts) emit_stmt(st);
            emit_defers_from(dstart);
            return;
        }

        if (auto* d = dynamic_cast<ValDeclStmt*>(s)) {
            TypeInfo t;
            if (d->type) t = from_type(d->type);
            else if (d->init) t = infer(d->init);
            if (t.kind == TypeInfo::Unknown) t = {TypeInfo::Int, ""};
            std::string init_s = d->init ? (" = " + expr(d->init))
                                          : " = " + (t.is_ptr() ? std::string("NULL") : std::string("0"));
            line(c_type(t) + " " + std::string(d->name) + init_s + ";");
            locals[std::string(d->name)] = t;
            return;
        }

        if (auto* e = dynamic_cast<ExprStmt*>(s)) {
            line(expr(e->expr) + ";");
            return;
        }

        if (auto* r = dynamic_cast<ReturnStmt*>(s)) {
            emit_defers_from(0);
            if (r->value) line("return " + expr(r->value) + ";");
            else          line("return;");
            return;
        }

        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            line("if (" + expr(i->cond) + ") {");
            inc(); emit_stmt(i->then); dec();
            if (i->els) {
                line("} else {");
                inc(); emit_stmt(i->els); dec();
            }
            line("}");
            return;
        }

        if (auto* w = dynamic_cast<WhileStmt*>(s)) {
            line("while (" + expr(w->cond) + ") {");
            inc();
            loop_boundary.push_back(active_defers.size());
            emit_stmt(w->body);
            loop_boundary.pop_back();
            dec();
            line("}");
            return;
        }

        if (dynamic_cast<BreakStmt*>(s)) {
            if (!loop_boundary.empty()) emit_defers_from(loop_boundary.back());
            line("break;");
            return;
        }
        if (dynamic_cast<ContinueStmt*>(s)) {
            if (!loop_boundary.empty()) emit_defers_from(loop_boundary.back());
            line("continue;");
            return;
        }

        if (auto* d = dynamic_cast<DeferStmt*>(s)) {
            active_defers.push_back(d->body);
            return;
        }

        if (auto* fi = dynamic_cast<ForInStmt*>(s)) {
            auto* r = dynamic_cast<RangeExpr*>(fi->iter);
            if (!r) {
                std::fprintf(stderr, "genc: for-in only supports ranges\n");
                return;
            }
            std::string v(fi->var);
            std::string op = r->inclusive ? "<= " : "< ";
            line("for (int64_t " + v + " = " + expr(r->lo) + "; "
                 + v + " " + op + expr(r->hi) + "; ++" + v + ") {");
            inc();
            loop_boundary.push_back(active_defers.size());
            locals[v] = {TypeInfo::Int, ""};
            emit_stmt(fi->body);
            loop_boundary.pop_back();
            dec();
            line("}");
            return;
        }

        if (auto* fs = dynamic_cast<ForStmt*>(s)) {
            line("{");
            inc();
            if (fs->init) emit_stmt(fs->init);
            line("while (true) {");
            inc();
            if (fs->cond) line("if (!(" + expr(fs->cond) + ")) break;");
            loop_boundary.push_back(active_defers.size());
            emit_stmt(fs->body);
            if (fs->post) line(expr(fs->post) + ";");
            loop_boundary.pop_back();
            dec();
            line("}");
            dec();
            line("}");
            return;
        }

        std::fprintf(stderr, "genc: unsupported statement\n");
    }

    // -----------------------------------------------------------------------
    // Helpers: czy funkcja/blok kończy się return/break/continue
    // -----------------------------------------------------------------------
    bool ends_with_term(Stmt* s) {
        if (!s) return false;
        if (dynamic_cast<ReturnStmt*>(s))   return true;
        if (dynamic_cast<BreakStmt*>(s))    return true;
        if (dynamic_cast<ContinueStmt*>(s)) return true;
        if (auto* b = dynamic_cast<BlockStmt*>(s))
            return !b->stmts.empty() && ends_with_term(b->stmts.back());
        if (auto* i = dynamic_cast<IfStmt*>(s))
            return i->els && ends_with_term(i->then) && ends_with_term(i->els);
        return false;
    }

    // -----------------------------------------------------------------------
    // Emisja funkcji / metod
    // -----------------------------------------------------------------------
    void emit_function_body(FunDecl* f, ClassInfo* owner) {
        locals.clear();
        active_defers.clear();
        loop_boundary.clear();
        std::string saved_class = current_class;
        current_class = owner ? owner->name : "";

        // Sygnatura
        std::string ret = (f->return_type && std::string(f->return_type->name) == "void")
                          ? "void" : c_type(from_type(f->return_type));
        if (!owner && f->name == "main") ret = "int";

        std::string name_part = owner
            ? std::string(owner->name) + "_" + std::string(f->name)
            : std::string(f->name);

        std::string sig = ret + " " + name_part + "(";
        bool first = true;
        if (owner) {
            sig += owner->name + "* self";
            locals["self"] = {TypeInfo::UserPtr, owner->name};
            first = false;
        }
        for (auto& p : f->params) {
            if (!first) sig += ", ";
            sig += c_type(from_type(p.type)) + " " + std::string(p.name);
            locals[std::string(p.name)] = from_type(p.type);
            first = false;
        }
        if (first) sig += "void";
        sig += ") {";
        raw(sig);
        inc();

        if (f->body) emit_stmt(f->body);

        // fallback return
        bool ends = f->body && ends_with_term(f->body);
        if (!ends) {
            if (!owner && f->name == "main")    line("return 0;");
            else if (ret == "int64_t")          line("return 0;");
            else if (ret == "double")           line("return 0.0;");
            else if (ret == "bool")             line("return false;");
            else if (ret == "char")             line("return '\\0';");
            else if (ret == "x0p_str")          line("return \"\";");
            else if (ret == "void")             { /* nothing */ }
            else                                line("return NULL;");
        }

        dec();
        raw("}");
        raw("");

        current_class = saved_class;
    }

    // -----------------------------------------------------------------------
    // Prepass — zbierz klasy/enumy/funkcje
    // -----------------------------------------------------------------------
    void collect(Program& p) {
        for (auto* d : p.decls) {
            if (auto* e = dynamic_cast<EnumDecl*>(d))
                enums[std::string(e->name)] = { std::string(e->name) };
            else if (auto* s = dynamic_cast<StructDecl*>(d)) {
                ClassInfo ci; ci.name = std::string(s->name);
                for (auto& f : s->fields)
                    ci.fields.push_back({std::string(f.name), f.type, f.init});
                classes[ci.name] = std::move(ci);
            }
            else if (auto* c = dynamic_cast<ClassDecl*>(d)) {
                ClassInfo ci; ci.name = std::string(c->name);
                for (auto& f : c->fields)
                    ci.fields.push_back({std::string(f.name), f.type, f.init});
                for (auto* m : c->methods) ci.methods.push_back(m);
                classes[ci.name] = std::move(ci);
            }
        }
        for (auto* d : p.decls) {
            if (auto* f = dynamic_cast<FunDecl*>(d))
                functions[std::string(f->name)] = from_type(f->return_type);
        }
    }

    // -----------------------------------------------------------------------
    // Runtime
    // -----------------------------------------------------------------------
    void emit_runtime() {
        if (opts_ && opts_->no_std) return;
        raw("// AUTO-GENERATED by X0+ genc. Do not edit.");
        raw("#include <stdint.h>");
        raw("#include <stdbool.h>");
        raw("#include <stdio.h>");
        raw("#include <stdlib.h>");
        raw("#include <string.h>");
        raw("");
        raw("typedef const char* x0p_str;");
        raw("");
        raw("static inline x0p_str x0p_strcat(x0p_str a, x0p_str b) {");
        raw("    size_t la = strlen(a), lb = strlen(b);");
        raw("    char* r = (char*)malloc(la + lb + 1);");
        raw("    memcpy(r, a, la);");
        raw("    memcpy(r + la, b, lb + 1);");
        raw("    return r;");
        raw("}");
        raw("static inline void x0p_print_int(int64_t v)   { printf(\"%lld\\n\", (long long)v); }");
        raw("static inline void x0p_print_float(double v)  { printf(\"%g\\n\", v); }");
        raw("static inline void x0p_print_str(x0p_str v)   { printf(\"%s\\n\", v); }");
        raw("static inline void x0p_print_bool(bool v)     { printf(\"%d\\n\", v ? 1 : 0); }");
        raw("static inline void x0p_print_ptr(void* v)     { printf(\"%p\\n\", v); }");
        raw("");
    }

    // -----------------------------------------------------------------------
    // Enum
    // -----------------------------------------------------------------------
    void emit_enum(EnumDecl* e) {
        std::string s = "typedef enum { ";
        for (size_t i = 0; i < e->members.size(); ++i) {
            if (i) s += ", ";
            s += std::string(e->name) + "_" + std::string(e->members[i].name);
        }
        s += " } " + std::string(e->name) + ";";
        raw(s);
        raw("");
    }

    // -----------------------------------------------------------------------
    // Class/struct
    // -----------------------------------------------------------------------
    void emit_struct_body(ClassInfo& ci) {
        // forward decl
        raw("typedef struct " + ci.name + " " + ci.name + ";");
        raw("struct " + ci.name + " {");
        inc();
        if (ci.fields.empty()) {
            line("uint8_t _pad;");
        } else {
            for (auto& f : ci.fields)
                line(c_type(from_type(f.type)) + " " + f.name + ";");
        }
        dec();
        raw("};");
        raw("");

        // _new() helper
        raw(ci.name + "* " + ci.name + "_new(void) {");
        inc();
        line(ci.name + "* self = (" + ci.name + "*)calloc(1, sizeof(" + ci.name + "));");
        // init simple literals
        for (auto& f : ci.fields) {
            if (!f.init) continue;
            std::string val;
            if (auto* i = dynamic_cast<IntLitExpr*>(f.init))    val = std::to_string(i->value);
            else if (auto* b = dynamic_cast<BoolLitExpr*>(f.init)) val = b->value ? "true" : "false";
            else if (auto* s = dynamic_cast<StrLitExpr*>(f.init)) {
                std::string v(s->value);
                if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
                    v = v.substr(1, v.size() - 2);
                val = "\"" + v + "\"";
            } else if (auto* fl = dynamic_cast<FloatLitExpr*>(f.init)) {
                char buf[64]; std::snprintf(buf, sizeof buf, "%g", fl->value); val = buf;
            } else {
                std::fprintf(stderr, "genc: complex field init skipped in %s\n", ci.name.c_str());
                continue;
            }
            line("self->" + f.name + " = " + val + ";");
        }
        line("return self;");
        dec();
        raw("}");
        raw("");

        // method prototypes
        for (auto* m : ci.methods) {
            std::string ret = (m->return_type && std::string(m->return_type->name) == "void")
                              ? "void" : c_type(from_type(m->return_type));
            std::string sig = ret + " " + ci.name + "_" + std::string(m->name) + "(" + ci.name + "* self";
            for (auto& p : m->params)
                sig += ", " + c_type(from_type(p.type)) + " " + std::string(p.name);
            sig += ");";
            raw(sig);
        }
        raw("");
    }

    void emit_method_bodies(ClassInfo& ci) {
        for (auto* m : ci.methods) emit_function_body(m, &ci);
    }

    // -----------------------------------------------------------------------
    // Entry
    // -----------------------------------------------------------------------
    std::string run(Program& p, const EmitOptions& o) {
        opts_ = &o;
        collect(p);

        if (opts_->multi_file) {
            raw("#include \"" + opts_->header_name + "\"");
            raw("");
        } else {
            emit_runtime();
        }

        // enumy pierwsze (potrzebne w ciałach funkcji)
        if (!opts_->multi_file)
            for (auto* d : p.decls)
                if (auto* e = dynamic_cast<EnumDecl*>(d)) emit_enum(e);

        // struct body + _new() + method prototypy
        if (!opts_->multi_file) for (auto* d : p.decls) {
            if (auto* s = dynamic_cast<StructDecl*>(d)) {
                if (auto* ci = find_class(std::string(s->name))) emit_struct_body(*ci);
            }
            else if (auto* c = dynamic_cast<ClassDecl*>(d)) {
                if (auto* ci = find_class(std::string(c->name))) emit_struct_body(*ci);
            }
            else if (dynamic_cast<InterfaceDecl*>(d))
                std::fprintf(stderr, "genc: interface not supported in v0.1 (skipped)\n");
        }

        // prototypes wolnych funkcji
        if (!opts_->multi_file) for (auto* d : p.decls) {
            if (auto* f = dynamic_cast<FunDecl*>(d)) {
                std::string ret = (f->return_type && std::string(f->return_type->name) == "void")
                                  ? "void" : c_type(from_type(f->return_type));
                if (f->name == "main") ret = "int";
                std::string sig = ret + " " + std::string(f->name) + "(";
                bool first = true;
                for (auto& pp : f->params) {
                    if (!first) sig += ", ";
                    sig += c_type(from_type(pp.type)) + " " + std::string(pp.name);
                    first = false;
                }
                if (first) sig += "void";
                sig += ");";
                raw(sig);
            }
        }
        raw("");

        // method bodies
        for (auto* d : p.decls) {
            if (auto* s = dynamic_cast<StructDecl*>(d)) {
                if (auto* ci = find_class(std::string(s->name))) emit_method_bodies(*ci);
            }
            else if (auto* c = dynamic_cast<ClassDecl*>(d)) {
                if (auto* ci = find_class(std::string(c->name))) emit_method_bodies(*ci);
            }
        }

        // free function bodies
        for (auto* d : p.decls) {
            if (auto* f = dynamic_cast<FunDecl*>(d)) emit_function_body(f, nullptr);
        }

        return out.str();
    }
};

} // namespace

std::string emit_c(Program& p, const EmitOptions& opts) {
    Emitter e;
    return e.run(p, opts);
}

} // namespace x0p
