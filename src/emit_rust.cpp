#include "x0p/emit_rust.h"
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace x0p {
namespace {

struct TypeInfo {
    enum Kind { Int, Float, Bool, Char, String, UserPtr, Void, Unknown } kind = Unknown;
    std::string name;
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

std::string rust_type(const TypeInfo& t) {
    switch (t.kind) {
        case TypeInfo::Int:    return "i64";
        case TypeInfo::Float:  return "f64";
        case TypeInfo::Bool:   return "bool";
        case TypeInfo::Char:   return "char";
        case TypeInfo::String: return "String";
        case TypeInfo::UserPtr:return "Box<" + t.name + ">";
        case TypeInfo::Void:   return "()";
        default:               return "i64";
    }
}

std::string default_value(const TypeInfo& t) {
    switch (t.kind) {
        case TypeInfo::Int:    return "0";
        case TypeInfo::Float:  return "0.0";
        case TypeInfo::Bool:   return "false";
        case TypeInfo::Char:   return "'\\0'";
        case TypeInfo::String: return "String::new()";
        case TypeInfo::UserPtr:return t.name + "::new()";
        case TypeInfo::Void:   return "()";
        default:               return "0";
    }
}

struct FieldInfo { std::string name; Type* type; Expr* init; };
struct ClassInfo {
    std::string              name;
    std::vector<FieldInfo>   fields;
    std::vector<FunDecl*>    methods;
};
struct EnumInfo { std::string name; std::vector<std::string> variants; };

struct Emitter {
    std::ostringstream                          out;
    std::unordered_map<std::string, ClassInfo>  classes;
    std::unordered_map<std::string, EnumInfo>   enums;
    std::unordered_map<std::string, TypeInfo>   functions;
    std::unordered_map<std::string, TypeInfo>   locals;
    std::string                                 current_class;

    std::vector<Stmt*>    active_defers;
    std::vector<size_t>   loop_boundary;
    int                   indent = 0;

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
        if (auto* p = dynamic_cast<ParenExpr*>(e)) return infer(p->inner);
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
    // Expr
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
            return "String::from(\"" + esc + "\")";
        }
        if (auto* c = dynamic_cast<CharLitExpr*>(e)) return "'" + std::string(1, c->value) + "'";
        if (auto* b = dynamic_cast<BoolLitExpr*>(e)) return b->value ? "true" : "false";
        if (dynamic_cast<NullLitExpr*>(e)) return "None::<i64>";
        if (dynamic_cast<ThisExpr*>(e))    return "self";
        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            auto it = locals.find(std::string(id->name));
            if (it != locals.end()) return std::string(id->name);
            if (auto* ci = find_class(current_class))
                for (auto& f : ci->fields)
                    if (f.name == id->name) return "self." + std::string(id->name);
            return std::string(id->name);
        }
        if (auto* p = dynamic_cast<ParenExpr*>(e))
            return "(" + expr(p->inner) + ")";
        if (auto* u = dynamic_cast<UnaryExpr*>(e)) {
            std::string ops;
            switch (u->op) {
                case TokenKind::Minus: ops = "-"; break;
                case TokenKind::Plus:  ops = "";  break;
                case TokenKind::Bang:  ops = "!"; break;
                case TokenKind::Tilde: ops = "!"; break;
                default: break;
            }
            return "(" + ops + expr(u->operand) + ")";
        }
        if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
            auto lt = infer(b->lhs), rt = infer(b->rhs);
            if (b->op == TokenKind::Plus &&
                (lt.kind == TypeInfo::String || rt.kind == TypeInfo::String))
                return "format!(\"{}{}\", " + expr(b->lhs) + ", " + expr(b->rhs) + ")";
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
            std::string t = nw->type ? std::string(nw->type->name) : "()";
            if (!nw->args.empty())
                std::fprintf(stderr, "genr: 'new %s(...)' args ignored (no ctor in v0.1)\n", t.c_str());
            return t + "::new()";
        }
        return "0i64";
    }

    std::string field_str(FieldExpr* f) {
        if (auto* oid = dynamic_cast<IdentExpr*>(f->object)) {
            if (enums.count(std::string(oid->name)))
                return std::string(oid->name) + "::" + std::string(f->name) + " as i64";
        }
        return expr(f->object) + "." + std::string(f->name);
    }

    std::string call_str(CallExpr* c) {
        if (auto* id = dynamic_cast<IdentExpr*>(c->callee)) {
            if (id->name == "print" && c->args.size() == 1) {
                auto t = infer(c->args[0]);
                std::string inner = expr(c->args[0]);
                switch (t.kind) {
                    case TypeInfo::Bool:   return "println!(\"{}\", (" + inner + ") as i64)";
                    case TypeInfo::UserPtr:return "println!(\"{:?}\", " + inner + ")";
                    default:               return "println!(\"{}\", " + inner + ")";
                }
            }
        }
        // method call obj.method(args) -> obj.method(args) (native Rust syntax)
        if (auto* f = dynamic_cast<FieldExpr*>(c->callee)) {
            std::string mname(f->name);
            if (!find_method_owner(mname).empty()) {
                std::string s = expr(f->object) + "." + mname + "(";
                for (size_t i = 0; i < c->args.size(); ++i) {
                    if (i) s += ", ";
                    s += expr(c->args[i]);
                }
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
            // class instances always `let mut` (methods use &mut self)
            bool needs_mut = d->is_mutable || t.is_ptr();
            std::string kw = needs_mut ? "let mut " : "let ";
            std::string ty = ": " + rust_type(t);
            std::string init_s = d->init ? (" = " + expr(d->init))
                                          : (" = " + default_value(t));
            line(kw + std::string(d->name) + ty + init_s + ";");
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
            line("if " + expr(i->cond) + " {");
            inc(); emit_stmt(i->then); dec();
            if (i->els) {
                // else-if chain as else { if ... } — valid but slightly nested
                if (dynamic_cast<IfStmt*>(i->els)) {
                    line("} else {");
                    inc(); emit_stmt(i->els); dec();
                } else {
                    line("} else {");
                    inc(); emit_stmt(i->els); dec();
                }
            }
            line("}");
            return;
        }

        if (auto* w = dynamic_cast<WhileStmt*>(s)) {
            line("while " + expr(w->cond) + " {");
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
                std::fprintf(stderr, "genr: for-in only supports ranges\n");
                return;
            }
            std::string v(fi->var);
            std::string range = expr(r->lo) + (r->inclusive ? "..=" : "..") + expr(r->hi);
            line("for " + v + " in " + range + " {");
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
            line("while true {");
            inc();
            if (fs->cond) line("if !(" + expr(fs->cond) + ") { break; }");
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

        std::fprintf(stderr, "genr: unsupported statement\n");
    }

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
    // Functions
    // -----------------------------------------------------------------------
    void emit_function(FunDecl* f, ClassInfo* owner, bool is_main) {
        locals.clear();
        active_defers.clear();
        loop_boundary.clear();
        std::string saved_class = current_class;
        current_class = owner ? owner->name : "";

        std::string fname = is_main ? "x0p_main" : std::string(f->name);
        TypeInfo ret = from_type(f->return_type);
        std::string ret_s = ret.kind == TypeInfo::Void ? "" : " -> " + rust_type(ret);

        std::string sig = "fn " + fname + "(";
        bool first = true;
        if (owner) {
            sig += "&mut self";
            locals["self"] = {TypeInfo::UserPtr, owner->name};
            first = false;
        }
        for (auto& p : f->params) {
            if (!first) sig += ", ";
            sig += std::string(p.name) + ": " + rust_type(from_type(p.type));
            locals[std::string(p.name)] = from_type(p.type);
            first = false;
        }
        sig += ")" + ret_s + " {";
        line(sig);
        inc();

        if (f->body) emit_stmt(f->body);

        bool ends = f->body && ends_with_term(f->body);
        if (!ends) {
            if (ret.kind == TypeInfo::Void) { /* nothing */ }
            else                            line("unreachable!()");
        }

        dec();
        line("}");
        current_class = saved_class;
    }

    // -----------------------------------------------------------------------
    // Collect
    // -----------------------------------------------------------------------
    void collect(Program& p) {
        for (auto* d : p.decls) {
            if (auto* e = dynamic_cast<EnumDecl*>(d)) {
                EnumInfo ei; ei.name = std::string(e->name);
                for (auto& m : e->members) ei.variants.push_back(std::string(m.name));
                enums[ei.name] = std::move(ei);
            }
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
        for (auto* d : p.decls)
            if (auto* f = dynamic_cast<FunDecl*>(d))
                functions[std::string(f->name)] = from_type(f->return_type);
    }

    // -----------------------------------------------------------------------
    // Emitters per-decl
    // -----------------------------------------------------------------------
    void emit_enum(EnumDecl* e) {
        raw("#[derive(Debug, Clone, Copy, PartialEq, Eq)]");
        std::string s = "enum " + std::string(e->name) + " { ";
        for (size_t i = 0; i < e->members.size(); ++i) {
            if (i) s += ", ";
            s += std::string(e->members[i].name);
        }
        s += " }";
        raw(s);
        raw("");
    }

    void emit_class(ClassInfo& ci) {
        // struct
        raw("#[derive(Debug)]");
        raw("struct " + ci.name + " {");
        inc();
        if (ci.fields.empty()) {
            line("_dummy: u8,");
        } else {
            for (auto& f : ci.fields)
                line(f.name + ": " + rust_type(from_type(f.type)) + ",");
        }
        dec();
        raw("}");
        raw("");

        // impl
        raw("impl " + ci.name + " {");
        inc();
        // new()
        line("fn new() -> Box<" + ci.name + "> {");
        inc();
        std::string construct = "Box::new(" + ci.name + " { ";
        bool first = true;
        if (ci.fields.empty()) {
            construct += "_dummy: 0";
        } else {
            for (auto& f : ci.fields) {
                if (!first) construct += ", ";
                construct += f.name + ": ";
                if (f.init) construct += expr(f.init);
                else        construct += default_value(from_type(f.type));
                first = false;
            }
        }
        construct += " })";
        line(construct);
        dec();
        line("}");
        raw("");

        // methods
        for (auto* m : ci.methods) {
            emit_function(m, &ci, false);
            raw("");
        }
        dec();
        raw("}");
        raw("");
    }

    // -----------------------------------------------------------------------
    // Entry
    // -----------------------------------------------------------------------
    std::string run(Program& p) {
        collect(p);

        raw("// AUTO-GENERATED by X0+ genr. Do not edit.");
        raw("#![allow(unused, non_snake_case, dead_code)]");
        raw("");

        for (auto* d : p.decls)
            if (auto* e = dynamic_cast<EnumDecl*>(d)) emit_enum(e);

        for (auto* d : p.decls) {
            if (auto* s = dynamic_cast<StructDecl*>(d)) {
                if (auto* ci = find_class(std::string(s->name))) emit_class(*ci);
            }
            else if (auto* c = dynamic_cast<ClassDecl*>(d)) {
                if (auto* ci = find_class(std::string(c->name))) emit_class(*ci);
            }
            else if (dynamic_cast<InterfaceDecl*>(d))
                std::fprintf(stderr, "genr: interface not supported in v0.1 (skipped)\n");
        }

        // free functions (rename main)
        bool has_main = false;
        for (auto* d : p.decls) {
            if (auto* f = dynamic_cast<FunDecl*>(d)) {
                bool is_main = (f->name == "main");
                if (is_main) has_main = true;
                emit_function(f, nullptr, is_main);
                raw("");
            }
        }

        if (has_main) {
            raw("fn main() {");
            inc();
            line("let _ = x0p_main();");
            dec();
            raw("}");
        }

        return out.str();
    }
};

} // namespace

std::string emit_rust(Program& p) {
    Emitter e;
    return e.run(p);
}

} // namespace x0p
