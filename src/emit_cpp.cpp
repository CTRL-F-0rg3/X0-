#include "x0p/emit_cpp.h"
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace x0p {
namespace {

struct FieldInfo { std::string name; Type* type; Expr* init; };

struct ClassInfo {
    std::string              name;
    std::vector<FieldInfo>   fields;
    std::vector<FunDecl*>    methods;
    std::string              extends;
    std::vector<std::string> implements;
    bool                     is_struct    = false;
    bool                     is_interface = false;
};

struct EnumInfo { std::string name; };

struct Emitter {
    std::ostringstream                          out;
    std::unordered_map<std::string, ClassInfo>  classes;
    std::unordered_map<std::string, EnumInfo>   enums;

    std::vector<Stmt*>    active_defers;
    std::vector<size_t>   loop_boundary;   // dstart dla każdej aktywnej pętli
    int                   indent    = 0;
    int                   unique_id = 0;

    void line(const std::string& s) {
        for (int i = 0; i < indent; ++i) out << "    ";
        out << s << "\n";
    }
    void raw(const std::string& s) { out << s << "\n"; }
    void inc() { ++indent; }
    void dec() { --indent; }

    // -----------------------------------------------------------------------
    // Typy
    // -----------------------------------------------------------------------
    std::string type_to_cpp(Type* t) {
        if (!t) return "auto";
        std::string n(t->name);
        if (n == "void")   return "void";
        if (n == "bool")   return "bool";
        if (n == "char")   return "char";
        if (n == "int")    return "int64_t";
        if (n == "float")  return "double";
        if (n == "double") return "double";
        if (n == "String") return "std::string";
        return n + "*";   // user type -> pointer
    }

    bool is_user_type(Type* t) {
        if (!t) return false;
        std::string n(t->name);
        return n != "void" && n != "bool" && n != "char"
            && n != "int"  && n != "float" && n != "double"
            && n != "String";
    }

    // -----------------------------------------------------------------------
    // Prepass
    // -----------------------------------------------------------------------
    void collect(Program& p) {
        for (auto* d : p.decls) {
            if (auto* e = dynamic_cast<EnumDecl*>(d)) {
                enums[std::string(e->name)] = { std::string(e->name) };
            }
            else if (auto* s = dynamic_cast<StructDecl*>(d)) {
                ClassInfo ci; ci.name = std::string(s->name); ci.is_struct = true;
                for (auto& f : s->fields) ci.fields.push_back({std::string(f.name), f.type, f.init});
                classes[ci.name] = std::move(ci);
            }
            else if (auto* c = dynamic_cast<ClassDecl*>(d)) {
                ClassInfo ci; ci.name = std::string(c->name);
                ci.extends = std::string(c->extends);
                for (auto& i : c->implements) ci.implements.push_back(std::string(i));
                for (auto& f : c->fields) ci.fields.push_back({std::string(f.name), f.type, f.init});
                for (auto* m : c->methods) ci.methods.push_back(m);
                classes[ci.name] = std::move(ci);
            }
            else if (auto* i = dynamic_cast<InterfaceDecl*>(d)) {
                ClassInfo ci; ci.name = std::string(i->name); ci.is_interface = true;
                for (auto* m : i->methods) ci.methods.push_back(m);
                classes[ci.name] = std::move(ci);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Expr → std::string
    // -----------------------------------------------------------------------
    std::string expr(Expr* e) {
        if (auto* n = dynamic_cast<IntLitExpr*>(e))
            return std::to_string(n->value);
        if (auto* f = dynamic_cast<FloatLitExpr*>(e)) {
            char buf[64]; std::snprintf(buf, sizeof buf, "%g", f->value);
            return std::string(buf);
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
            return "std::string(\"" + esc + "\")";
        }
        if (auto* c = dynamic_cast<CharLitExpr*>(e))
            return "'" + std::string(1, c->value) + "'";
        if (auto* b = dynamic_cast<BoolLitExpr*>(e))
            return b->value ? "true" : "false";
        if (dynamic_cast<NullLitExpr*>(e))   return "nullptr";
        if (dynamic_cast<ThisExpr*>(e))      return "this";
        if (auto* id = dynamic_cast<IdentExpr*>(e))
            return std::string(id->name);
        if (auto* p = dynamic_cast<ParenExpr*>(e))
            return "(" + expr(p->inner) + ")";
        if (auto* u = dynamic_cast<UnaryExpr*>(e)) {
            std::string op = token_kind_name(u->op);
            std::string ops;
            switch (u->op) {
                case TokenKind::Minus: ops = "-"; break;
                case TokenKind::Plus:  ops = "+"; break;
                case TokenKind::Bang:  ops = "!"; break;
                case TokenKind::Tilde: ops = "~"; break;
                default: ops = ""; break;
            }
            return "(" + ops + expr(u->operand) + ")";
        }
        if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
            std::string ops;
            switch (b->op) {
                case TokenKind::Plus:             ops = "+";   break;
                case TokenKind::Minus:            ops = "-";   break;
                case TokenKind::Star:             ops = "*";   break;
                case TokenKind::Slash:            ops = "/";   break;
                case TokenKind::Percent:          ops = "%";   break;
                case TokenKind::EqEq:             ops = "==";  break;
                case TokenKind::BangEq:           ops = "!=";  break;
                case TokenKind::Lt:               ops = "<";   break;
                case TokenKind::LtEq:             ops = "<=";  break;
                case TokenKind::Gt:               ops = ">";   break;
                case TokenKind::GtEq:             ops = ">=";  break;
                case TokenKind::Amp:              ops = "&";   break;
                case TokenKind::Pipe:             ops = "|";   break;
                case TokenKind::Caret:            ops = "^";   break;
                case TokenKind::Shl:              ops = "<<";  break;
                case TokenKind::Shr:              ops = ">>";  break;
                case TokenKind::AmpAmp:           ops = "&&";  break;
                case TokenKind::PipePipe:         ops = "||";  break;
                default:                          ops = "?";   break;
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
        if (auto* c = dynamic_cast<CallExpr*>(e))   return call_str(c);
        if (auto* f = dynamic_cast<FieldExpr*>(e))  return field_str(f);
        if (auto* ix = dynamic_cast<IndexExpr*>(e))
            return expr(ix->object) + "[" + expr(ix->index) + "]";
        if (auto* nw = dynamic_cast<NewExpr*>(e)) {
            std::string t = nw->type ? std::string(nw->type->name) : "";
            std::string s = "new " + t + "(";
            for (size_t i = 0; i < nw->args.size(); ++i) {
                if (i) s += ", ";
                s += expr(nw->args[i]);
            }
            s += ")";
            return s;
        }
        return "0";
    }

    std::string field_str(FieldExpr* f) {
        // enum.member ?
        if (auto* oid = dynamic_cast<IdentExpr*>(f->object)) {
            if (enums.count(std::string(oid->name)))
                return std::string(oid->name) + "::" + std::string(f->name);
        }
        return expr(f->object) + "->" + std::string(f->name);
    }

    std::string call_str(CallExpr* c) {
        // print(...) special
        if (auto* id = dynamic_cast<IdentExpr*>(c->callee)) {
            if (id->name == "print" && c->args.size() == 1)
                return "x0p_print(" + expr(c->args[0]) + ")";
        }
        // obj.method(args) -> obj->method(args)
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
    // Emisja deferów — z aktualnego zakresu
    // -----------------------------------------------------------------------
    void emit_defers_from(size_t dstart) {
        for (size_t i = active_defers.size(); i > dstart; --i)
            emit_stmt(active_defers[i - 1]);
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
            active_defers.resize(dstart);
            return;
        }

        if (auto* d = dynamic_cast<ValDeclStmt*>(s)) {
            std::string kw = d->is_mutable ? "" : "const ";
            std::string ty;
            if (d->type) {
                ty = type_to_cpp(d->type) + " ";
            } else {
                ty = "auto ";
            }
            std::string init_s = d->init ? (" = " + expr(d->init)) : "";
            if (!d->type && !d->init) ty = "int64_t ", init_s = " = 0";
            line(kw + ty + std::string(d->name) + init_s + ";");
            return;
        }

        if (auto* e = dynamic_cast<ExprStmt*>(s)) {
            line(expr(e->expr) + ";");
            return;
        }

        if (auto* r = dynamic_cast<ReturnStmt*>(s)) {
            if (r->value) {
                emit_defers_from(0);
                line("return " + expr(r->value) + ";");
            } else {
                emit_defers_from(0);
                line("return;");
            }
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
                std::fprintf(stderr, "emit_cpp: for-in only supports ranges\n");
                return;
            }
            std::string v(fi->var);
            std::string op = r->inclusive ? "<= " : "< ";
            line("for (int64_t " + v + " = " + expr(r->lo) + "; "
                 + v + " " + op + expr(r->hi) + "; ++" + v + ") {");
            inc();
            loop_boundary.push_back(active_defers.size());
            emit_stmt(fi->body);
            loop_boundary.pop_back();
            dec();
            line("}");
            return;
        }

        if (auto* fs = dynamic_cast<ForStmt*>(s)) {
            // uproszczone: init jako stmt, potem while
            line("{");
            inc();
            if (fs->init) emit_stmt(fs->init);
            std::string top = "while (true) {";
            line(top);
            inc();
            if (fs->cond) {
                line("if (!(" + expr(fs->cond) + ")) break;");
            }
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

        std::fprintf(stderr, "emit_cpp: unsupported statement\n");
    }

    // -----------------------------------------------------------------------
    // Funkcje
    // -----------------------------------------------------------------------
    std::string signature(FunDecl* f, bool is_method) {
        std::string ret = type_to_cpp(f->return_type);
        if (!is_method && f->name == "main" && ret == "int64_t") ret = "int";
        std::string s = ret + " " + std::string(f->name) + "(";
        for (size_t i = 0; i < f->params.size(); ++i) {
            if (i) s += ", ";
            s += type_to_cpp(f->params[i].type) + " " + std::string(f->params[i].name);
        }
        s += ")";
        return s;
    }

    void emit_function(FunDecl* f, ClassDecl* owner) {
        active_defers.clear();
        loop_boundary.clear();

        std::string sig = signature(f, owner != nullptr);
        std::string prefix = owner ? std::string(owner->name) + "::" : "";
        // Inject prefix into "ret name(" part
        std::string ret = type_to_cpp(f->return_type);
        if (!owner && f->name == "main" && ret == "int64_t") ret = "int";
        std::string full = ret + " " + prefix + std::string(f->name) + "(";
        for (size_t i = 0; i < f->params.size(); ++i) {
            if (i) full += ", ";
            full += type_to_cpp(f->params[i].type) + " " + std::string(f->params[i].name);
        }
        full += ") {";
        raw(full);
        inc();
        if (f->body) emit_stmt(f->body);

        // fallback return
        std::string rt = type_to_cpp(f->return_type);
        if (rt == "void") {
            // nothing
        } else if (!owner && f->name == "main") {
            line("return 0;");
        } else if (rt == "int64_t") {
            line("return 0;");
        } else if (rt == "double" || rt == "bool" || rt == "char") {
            line("return {};");
        } else {
            line("return {};");
        }

        dec();
        raw("}");
        raw("");
    }

    // -----------------------------------------------------------------------
    // Klasy / struct / enum / interface
    // -----------------------------------------------------------------------
    void emit_class(ClassDecl* c) {
        std::string bases;
        std::vector<std::string> bs;
        if (!c->extends.empty() && classes.count(std::string(c->extends)))
            bs.push_back("public " + std::string(c->extends));
        else if (!c->extends.empty())
            std::fprintf(stderr, "emit_cpp: unknown base class '%s' (skipped)\n",
                         std::string(c->extends).c_str());
        for (auto& i : c->implements) {
            if (classes.count(std::string(i))) bs.push_back("public " + std::string(i));
            else std::fprintf(stderr, "emit_cpp: unknown interface '%s' (skipped)\n",
                              std::string(i).c_str());
        }
        if (!bs.empty()) {
            bases = " : ";
            for (size_t i = 0; i < bs.size(); ++i) {
                if (i) bases += ", ";
                bases += bs[i];
            }
        }

        raw("class " + std::string(c->name) + bases + " {");
        inc();
        std::string cur_vis = "public";
        raw("public:");

        for (auto& f : c->fields) {
            std::string vis = f.mods.is_private   ? "private"
                            : f.mods.is_protected ? "protected" : "public";
            if (vis != cur_vis) { raw(vis + ":"); cur_vis = vis; }
            std::string ty = type_to_cpp(f.type);
            std::string decl = ty + " " + std::string(f.name);
            if (f.init) decl += " = " + expr(f.init);
            else        decl += "{}";
            decl += ";";
            line(decl);
        }
        for (auto* m : c->methods) {
            std::string vis = m->mods.is_private   ? "private"
                            : m->mods.is_protected ? "protected" : "public";
            if (vis != cur_vis) { raw(vis + ":"); cur_vis = vis; }
            line(signature(m, true) + ";");
        }
        dec();
        raw("};");
        raw("");

        // method bodies
        for (auto* m : c->methods) emit_function(m, c);
    }

    void emit_struct(StructDecl* s) {
        raw("struct " + std::string(s->name) + " {");
        inc();
        for (auto& f : s->fields) {
            std::string ty = type_to_cpp(f.type);
            std::string decl = ty + " " + std::string(f.name);
            if (f.init) decl += " = " + expr(f.init);
            else        decl += "{}";
            decl += ";";
            line(decl);
        }
        dec();
        raw("};");
        raw("");
    }

    void emit_enum(EnumDecl* e) {
        std::string s = "enum class " + std::string(e->name) + " {";
        for (size_t i = 0; i < e->members.size(); ++i) {
            if (i) s += ", ";
            s += std::string(e->members[i].name);
        }
        s += " };";
        raw(s);
        raw("");
    }

    void emit_interface(InterfaceDecl* i) {
        raw("class " + std::string(i->name) + " {");
        inc();
        raw("public:");
        line("virtual ~" + std::string(i->name) + "() = default;");
        for (auto* m : i->methods) {
            line("virtual " + signature(m, true) + " = 0;");
        }
        dec();
        raw("};");
        raw("");
    }

    // -----------------------------------------------------------------------
    // Entry
    // -----------------------------------------------------------------------
    std::string run(Program& p) {
        collect(p);

        raw("// AUTO-GENERATED by X0+ gencpp. Do not edit.");
        raw("#include <cstdint>");
        raw("#include <iostream>");
        raw("#include <string>");
        raw("#include <type_traits>");
        raw("");
        raw("template<typename T>");
        raw("void x0p_print(T v) {");
        raw("    if constexpr (std::is_enum_v<T>) std::cout << static_cast<int64_t>(v) << \"\\n\";");
        raw("    else                               std::cout << v << \"\\n\";");
        raw("}");
        raw("inline void x0p_print(bool v) { std::cout << (v ? 1 : 0) << \"\\n\"; }");
        raw("");

        // forward declarations dla wolnych funkcji (oprócz main)
        for (auto* d : p.decls) {
            if (auto* f = dynamic_cast<FunDecl*>(d)) {
                if (f->name == "main") continue;
                line(signature(f, false) + ";");
            }
        }
        raw("");

        // definicje typów
        for (auto* d : p.decls) {
            if (auto* e = dynamic_cast<EnumDecl*>(d))           emit_enum(e);
            else if (auto* s = dynamic_cast<StructDecl*>(d))    emit_struct(s);
            else if (auto* i = dynamic_cast<InterfaceDecl*>(d)) emit_interface(i);
            else if (auto* c = dynamic_cast<ClassDecl*>(d))     emit_class(c);
        }

        // definicje wolnych funkcji
        for (auto* d : p.decls) {
            if (auto* f = dynamic_cast<FunDecl*>(d)) emit_function(f, nullptr);
        }
        return out.str();
    }
};

} // namespace

std::string emit_cpp(Program& p) {
    Emitter e;
    return e.run(p);
}

} // namespace x0p
