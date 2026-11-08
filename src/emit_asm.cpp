#include "x0p/emit_asm.h"
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace x0p {

namespace {

struct FieldInfo { std::string name; int offset; };

struct ClassInfo {
    std::string                     name;
    std::vector<FieldInfo>          fields;
    std::vector<FunDecl*>           methods;
    bool                            is_struct = false;
};

struct EnumInfo {
    std::string                              name;
    std::unordered_map<std::string, int64_t> members;
};

struct Emitter {
    std::ostringstream                          out;
    std::vector<std::string>                    rodata;
    std::unordered_map<std::string, int>        locals;       // name -> rbp offset (negative)
    std::unordered_map<std::string, bool>       is_string;
    std::unordered_map<std::string, ClassInfo>  classes;
    std::unordered_map<std::string, EnumInfo>   enums;

    ClassInfo*                                  current_class = nullptr;
    int                                         self_off      = 0;
    int                                         next_local    = 0;
    int                                         label_id      = 0;
    int                                         stack_depth   = 0;
    int                                         unique_id     = 0;
    std::string                                 return_label;
    std::vector<std::string>                    break_stack;
    std::vector<std::string>                    continue_stack;
    std::vector<Stmt*>                          active_defers;   // LIFO

    void line(const std::string& s) { out << "    " << s << "\n"; }
    void raw (const std::string& s) { out << s << "\n"; }
    void lbl (const std::string& s) { out << s << ":\n"; }

    std::string new_label()  { return ".L" + std::to_string(label_id++); }
    std::string uid(const std::string& p) { return p + std::to_string(unique_id++); }

    void push()             { line("push rax"); ++stack_depth; }
    void pop(const char* r) { out << "    pop " << r << "\n"; --stack_depth; }
    void align_call()       { if (stack_depth % 2) line("sub rsp, 8"); }
    void unalign_call()     { if (stack_depth % 2) line("add rsp, 8"); }

    int alloc_local(const std::string& name) {
        next_local += 8;
        locals[name] = -next_local;
        return -next_local;
    }

    std::string intern_string(const std::string& raw_s) {
        std::string c = raw_s;
        if (c.size() >= 2 && c.front() == '"' && c.back() == '"')
            c = c.substr(1, c.size() - 2);
        std::string esc;
        for (char ch : c) {
            switch (ch) {
                case '\\': esc += "\\\\"; break;
                case '"':  esc += "\\\""; break;
                case '\n': esc += "\\n";  break;
                case '\t': esc += "\\t";  break;
                default:   esc += ch;
            }
        }
        std::string l = ".Lstr" + std::to_string(rodata.size());
        rodata.push_back(l + ": .asciz \"" + esc + "\"");
        return l;
    }

    bool expr_is_string(Expr* e) {
        if (dynamic_cast<StrLitExpr*>(e)) return true;
        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            auto it = is_string.find(std::string(id->name));
            return it != is_string.end() && it->second;
        }
        if (auto* p = dynamic_cast<ParenExpr*>(e)) return expr_is_string(p->inner);
        return false;
    }

    // =======================================================================
    // Prepass — zbierz enumy, klasy, struktury
    // =======================================================================
    void collect(Program& p) {
        for (auto* d : p.decls) {
            if (auto* e = dynamic_cast<EnumDecl*>(d)) {
                EnumInfo info; info.name = std::string(e->name);
                for (size_t i = 0; i < e->members.size(); ++i)
                    info.members[std::string(e->members[i].name)] = (int64_t)i;
                enums[info.name] = std::move(info);
            }
            else if (auto* s = dynamic_cast<StructDecl*>(d)) {
                ClassInfo info; info.name = std::string(s->name); info.is_struct = true;
                int off = 0;
                for (auto& f : s->fields) {
                    info.fields.push_back({std::string(f.name), off});
                    off += 8;
                }
                classes[info.name] = std::move(info);
            }
            else if (auto* c = dynamic_cast<ClassDecl*>(d)) {
                ClassInfo info; info.name = std::string(c->name); info.is_struct = false;
                int off = 0;
                for (auto& f : c->fields) {
                    info.fields.push_back({std::string(f.name), off});
                    off += 8;
                }
                for (auto* m : c->methods) info.methods.push_back(m);
                classes[info.name] = std::move(info);
            }
        }
    }

    int find_field_offset(const ClassInfo& ci, const std::string& name) {
        for (auto& f : ci.fields) if (f.name == name) return f.offset;
        return -1;
    }

    std::string find_method_owner(const std::string& method) {
        std::string found;
        for (auto& [cname, ci] : classes) {
            for (auto* m : ci.methods) {
                if (m->name == method) {
                    if (!found.empty() && found != cname) return "";  // ambiguous
                    found = cname;
                }
            }
        }
        return found;
    }

    int find_field_offset_by_name(const std::string& name) {
        for (auto& [cname, ci] : classes) {
            int off = find_field_offset(ci, name);
            if (off >= 0) return off;
        }
        return -1;
    }

    // =======================================================================
    // Expr
    // =======================================================================
    void emit_expr(Expr* e) {
        if (auto* n = dynamic_cast<IntLitExpr*>(e))  { line("mov rax, " + std::to_string(n->value)); return; }
        if (auto* b = dynamic_cast<BoolLitExpr*>(e)) { line("mov rax, " + std::to_string(b->value ? 1 : 0)); return; }
        if (dynamic_cast<NullLitExpr*>(e))           { line("xor eax, eax"); return; }
        if (dynamic_cast<ThisExpr*>(e))              { line("mov rax, [rbp" + std::to_string(self_off) + "]"); return; }

        if (auto* s = dynamic_cast<StrLitExpr*>(e)) {
            std::string l = intern_string(std::string(s->value));
            line("lea rax, [rip+" + l + "]");
            return;
        }

        if (auto* id = dynamic_cast<IdentExpr*>(e)) {
            // 1) local
            auto it = locals.find(std::string(id->name));
            if (it != locals.end()) { line("mov rax, [rbp" + std::to_string(it->second) + "]"); return; }
            // 2) implicit this.field
            if (current_class) {
                int off = find_field_offset(*current_class, std::string(id->name));
                if (off >= 0) {
                    line("mov rax, [rbp" + std::to_string(self_off) + "]");
                    line("mov rax, [rax+" + std::to_string(off) + "]");
                    return;
                }
            }
            // 3) global function address
            line("lea rax, [rip+" + std::string(id->name) + "]");
            return;
        }

        if (auto* p = dynamic_cast<ParenExpr*>(e)) { emit_expr(p->inner); return; }

        if (auto* f = dynamic_cast<FieldExpr*>(e)) {
            // enum.member?
            if (auto* oid = dynamic_cast<IdentExpr*>(f->object)) {
                auto eit = enums.find(std::string(oid->name));
                if (eit != enums.end()) {
                    auto mit = eit->second.members.find(std::string(f->name));
                    if (mit != eit->second.members.end()) {
                        line("mov rax, " + std::to_string(mit->second));
                        return;
                    }
                }
            }
            // field access: obj.name  →  [obj_ptr + offset]
            emit_expr(f->object);
            int off = find_field_offset_by_name(std::string(f->name));
            if (off < 0) { std::fprintf(stderr, "emit_asm: field '%s' not found\n", std::string(f->name).c_str()); return; }
            line("mov rax, [rax+" + std::to_string(off) + "]");
            return;
        }

        if (auto* u = dynamic_cast<UnaryExpr*>(e)) {
            emit_expr(u->operand);
            switch (u->op) {
                case TokenKind::Minus: line("neg rax"); break;
                case TokenKind::Bang:  line("cmp rax, 0"); line("sete al"); line("movzx rax, al"); break;
                case TokenKind::Tilde: line("not rax"); break;
                default: break;
            }
            return;
        }

        if (auto* b = dynamic_cast<BinaryExpr*>(e)) {
            emit_expr(b->lhs); push();
            emit_expr(b->rhs); line("mov rdi, rax"); pop("rax");
            switch (b->op) {
                case TokenKind::Plus:    line("add rax, rdi"); break;
                case TokenKind::Minus:   line("sub rax, rdi"); break;
                case TokenKind::Star:    line("imul rax, rdi"); break;
                case TokenKind::Slash:   line("cqo"); line("idiv rdi"); break;
                case TokenKind::Percent: line("cqo"); line("idiv rdi"); line("mov rax, rdx"); break;
                case TokenKind::EqEq:    line("cmp rax, rdi"); line("sete al");  line("movzx rax, al"); break;
                case TokenKind::BangEq:  line("cmp rax, rdi"); line("setne al"); line("movzx rax, al"); break;
                case TokenKind::Lt:      line("cmp rax, rdi"); line("setl al");  line("movzx rax, al"); break;
                case TokenKind::LtEq:    line("cmp rax, rdi"); line("setle al"); line("movzx rax, al"); break;
                case TokenKind::Gt:      line("cmp rax, rdi"); line("setg al");  line("movzx rax, al"); break;
                case TokenKind::GtEq:    line("cmp rax, rdi"); line("setge al"); line("movzx rax, al"); break;
                case TokenKind::Amp:     line("and rax, rdi"); break;
                case TokenKind::Pipe:    line("or rax, rdi"); break;
                case TokenKind::Caret:   line("xor rax, rdi"); break;
                case TokenKind::Shl:     line("mov rcx, rdi"); line("shl rax, cl"); break;
                case TokenKind::Shr:     line("mov rcx, rdi"); line("sar rax, cl"); break;
                case TokenKind::AmpAmp: {
                    std::string f = new_label(), end = new_label();
                    line("cmp rax, 0"); line("je " + f);
                    line("cmp rdi, 0"); line("je " + f);
                    line("mov rax, 1"); line("jmp " + end);
                    lbl(f); line("xor eax, eax"); lbl(end);
                    break;
                }
                case TokenKind::PipePipe: {
                    std::string t = new_label(), end = new_label();
                    line("cmp rax, 0"); line("jne " + t);
                    line("cmp rdi, 0"); line("jne " + t);
                    line("xor eax, eax"); line("jmp " + end);
                    lbl(t); line("mov rax, 1"); lbl(end);
                    break;
                }
                default: line("nop"); break;
            }
            return;
        }

        if (auto* a = dynamic_cast<AssignExpr*>(e)) {
            // obj.field = value  lub  id = value
            if (auto* fe = dynamic_cast<FieldExpr*>(a->target)) {
                emit_expr(a->value); push();
                emit_expr(fe->object); line("mov rdi, rax"); pop("rax");
                int off = find_field_offset_by_name(std::string(fe->name));
                if (off < 0) { std::fprintf(stderr, "emit_asm: field '%s' not found\n", std::string(fe->name).c_str()); return; }
                line("mov [rdi+" + std::to_string(off) + "], rax");
                return;
            }
            if (auto* id = dynamic_cast<IdentExpr*>(a->target)) {
                emit_expr(a->value);
                // local
                auto it = locals.find(std::string(id->name));
                if (it != locals.end()) { line("mov [rbp" + std::to_string(it->second) + "], rax"); return; }
                // this.field
                if (current_class) {
                    int off = find_field_offset(*current_class, std::string(id->name));
                    if (off >= 0) {
                        line("mov rdi, [rbp" + std::to_string(self_off) + "]");
                        line("mov [rdi+" + std::to_string(off) + "], rax");
                        return;
                    }
                }
                std::fprintf(stderr, "emit_asm: cannot assign to '%s'\n", std::string(id->name).c_str());
                return;
            }
            emit_expr(a->value);
            return;
        }

        if (auto* c = dynamic_cast<CallExpr*>(e)) { emit_call(c); return; }

        if (auto* nw = dynamic_cast<NewExpr*>(e)) {
            std::string type_name(nw->type ? nw->type->name : "");
            auto cit = classes.find(type_name);
            if (cit == classes.end()) {
                std::fprintf(stderr, "emit_asm: unknown type '%s' in new\n", type_name.c_str());
                line("xor eax, eax");
                return;
            }
            int64_t size = (int64_t)cit->second.fields.size() * 8;
            if (size == 0) size = 8;
            line("mov rdi, " + std::to_string(size));
            align_call();
            line("call malloc");
            unalign_call();
            push();   // save pointer
            for (size_t i = 0; i < nw->args.size() && i < cit->second.fields.size(); ++i) {
                emit_expr(nw->args[i]);
                line("mov rdi, [rsp]");
                line("mov [rdi+" + std::to_string(cit->second.fields[i].offset) + "], rax");
            }
            pop("rax");
            return;
        }

        line("xor eax, eax");
    }

    // =======================================================================
    // Call
    // =======================================================================
    void emit_call(CallExpr* c) {
        static const char* argregs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};

        if (auto* id = dynamic_cast<IdentExpr*>(c->callee)) {
            // print(...)
            if (id->name == "print" && c->args.size() == 1) {
                bool s = expr_is_string(c->args[0]);
                std::string fmt = intern_string(s ? "%s\n" : "%d\n");
                emit_expr(c->args[0]);   // rax = wartość (może clobber rdi!)
                push();
                line("lea rdi, [rip+" + fmt + "]");
                pop("rsi");
                line("xor eax, eax");
                align_call();
                line("call printf");
                unalign_call();
                return;
            }
            if (c->args.size() > 6) {
                std::fprintf(stderr, "emit_asm: >6 args not supported\n");
                line("xor eax, eax");
                return;
            }
            for (auto* a : c->args) { emit_expr(a); push(); }
            for (int i = (int)c->args.size() - 1; i >= 0; --i) pop(argregs[i]);
            align_call();
            line("call " + std::string(id->name));
            unalign_call();
            return;
        }

        // obj.method(args) → Type_method(obj_ptr, args)
        if (auto* f = dynamic_cast<FieldExpr*>(c->callee)) {
            std::string mname(f->name);
            std::string owner = find_method_owner(mname);
            if (owner.empty()) {
                std::fprintf(stderr, "emit_asm: method '%s' not found (or ambiguous)\n", mname.c_str());
                line("xor eax, eax");
                return;
            }
            if (c->args.size() > 5) {
                std::fprintf(stderr, "emit_asm: >5 args in method call\n");
                line("xor eax, eax");
                return;
            }
            // self first, then args
            emit_expr(f->object);
            push();   // save self
            for (auto* a : c->args) { emit_expr(a); push(); }
            for (int i = (int)c->args.size() - 1; i >= 0; --i) pop(argregs[i + 1]);
            pop(argregs[0]);  // self → rdi
            align_call();
            line("call " + owner + "_" + mname);
            unalign_call();
            return;
        }

        std::fprintf(stderr, "emit_asm: only direct calls supported\n");
        line("xor eax, eax");
    }

    // =======================================================================
    // Stmt
    // =======================================================================
    void emit_stmt(Stmt* s) {
        if (!s) return;

        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            size_t dstart = active_defers.size();
            for (auto* st : b->stmts) emit_stmt(st);
            // emit defers in reverse (LIFO)
            for (size_t i = active_defers.size(); i > dstart; --i) {
                emit_stmt(active_defers[i - 1]);
            }
            active_defers.resize(dstart);
            return;
        }

        if (auto* d = dynamic_cast<ValDeclStmt*>(s)) {
            int off = alloc_local(std::string(d->name));
            is_string[std::string(d->name)] = d->init && expr_is_string(d->init);
            if (d->init) {
                emit_expr(d->init);
                line("mov [rbp" + std::to_string(off) + "], rax");
            } else {
                line("mov qword ptr [rbp" + std::to_string(off) + "], 0");
            }
            return;
        }

        if (auto* e = dynamic_cast<ExprStmt*>(s)) { emit_expr(e->expr); return; }

        if (auto* r = dynamic_cast<ReturnStmt*>(s)) {
            if (r->value) emit_expr(r->value);
            else line("xor eax, eax");
            // run all active defers before returning
            for (size_t i = active_defers.size(); i > 0; --i)
                emit_stmt(active_defers[i - 1]);
            line("jmp " + return_label);
            return;
        }

        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            std::string else_l = new_label(), end_l = new_label();
            emit_expr(i->cond);
            line("cmp rax, 0");
            line("je " + else_l);
            emit_stmt(i->then);
            line("jmp " + end_l);
            lbl(else_l);
            if (i->els) emit_stmt(i->els);
            lbl(end_l);
            return;
        }

        if (auto* w = dynamic_cast<WhileStmt*>(s)) {
            std::string top = new_label(), end = new_label();
            break_stack.push_back(end);
            continue_stack.push_back(top);
            lbl(top);
            emit_expr(w->cond);
            line("cmp rax, 0");
            line("je " + end);
            emit_stmt(w->body);
            line("jmp " + top);
            lbl(end);
            break_stack.pop_back();
            continue_stack.pop_back();
            return;
        }

        if (auto* d = dynamic_cast<DeferStmt*>(s)) {
            active_defers.push_back(d->body);
            return;
        }

        if (dynamic_cast<BreakStmt*>(s)) {
            if (break_stack.empty()) { std::fprintf(stderr, "emit_asm: break outside loop\n"); return; }
            line("jmp " + break_stack.back());
            return;
        }
        if (dynamic_cast<ContinueStmt*>(s)) {
            if (continue_stack.empty()) { std::fprintf(stderr, "emit_asm: continue outside loop\n"); return; }
            line("jmp " + continue_stack.back());
            return;
        }

        if (auto* fi = dynamic_cast<ForInStmt*>(s)) { emit_for_in(fi); return; }
        if (auto* fs = dynamic_cast<ForStmt*>(s))   { emit_for_c(fs);   return; }

        std::fprintf(stderr, "emit_asm: unsupported statement (skipped)\n");
    }

    void emit_for_in(ForInStmt* s) {
        auto* r = dynamic_cast<RangeExpr*>(s->iter);
        if (!r) {
            std::fprintf(stderr, "emit_asm: for-in only supports ranges (lo..hi)\n");
            return;
        }
        int id       = unique_id++;
        std::string i_name  = "__for_i_" + std::to_string(id);
        std::string hi_name = "__for_hi_" + std::to_string(id);
        int i_off  = alloc_local(i_name);
        int hi_off = alloc_local(hi_name);
        // user var aliases to i
        std::string user_var(s->var);
        locals[user_var] = i_off;

        // hi first (evaluate once), then lo
        emit_expr(r->hi);
        line("mov [rbp" + std::to_string(hi_off) + "], rax");
        emit_expr(r->lo);
        line("mov [rbp" + std::to_string(i_off) + "], rax");

        std::string top = new_label(), end = new_label(), cont = new_label();
        break_stack.push_back(end);
        continue_stack.push_back(cont);
        lbl(top);
        line("mov rax, [rbp" + std::to_string(i_off) + "]");
        line("mov rdi, [rbp" + std::to_string(hi_off) + "]");
        line("cmp rax, rdi");
        if (r->inclusive) line("jg " + end);
        else              line("jge " + end);
        emit_stmt(s->body);
        lbl(cont);
        line("mov rax, [rbp" + std::to_string(i_off) + "]");
        line("add rax, 1");
        line("mov [rbp" + std::to_string(i_off) + "], rax");
        line("jmp " + top);
        lbl(end);
        break_stack.pop_back();
        continue_stack.pop_back();
    }

    void emit_for_c(ForStmt* s) {
        std::string top = new_label(), end = new_label(), cont = new_label();
        if (s->init) emit_stmt(s->init);
        break_stack.push_back(end);
        continue_stack.push_back(cont);
        lbl(top);
        if (s->cond) {
            emit_expr(s->cond);
            line("cmp rax, 0");
            line("je " + end);
        }
        emit_stmt(s->body);
        lbl(cont);
        if (s->post) emit_expr(s->post);
        line("jmp " + top);
        lbl(end);
        break_stack.pop_back();
        continue_stack.pop_back();
    }

    // =======================================================================
    // Function
    // =======================================================================
    int count_locals(Stmt* s) {
        if (!s) return 0;
        if (auto* b = dynamic_cast<BlockStmt*>(s)) {
            int n = 0;
            for (auto* st : b->stmts) n += count_locals(st);
            return n;
        }
        if (dynamic_cast<ValDeclStmt*>(s)) return 1;
        if (auto* i = dynamic_cast<IfStmt*>(s)) {
            int n = count_locals(i->then);
            if (i->els) n += count_locals(i->els);
            return n;
        }
        if (auto* w = dynamic_cast<WhileStmt*>(s)) return count_locals(w->body);
        if (auto* f = dynamic_cast<ForInStmt*>(s)) return 2 + count_locals(f->body);  // _i, _hi
        if (auto* f = dynamic_cast<ForStmt*>(s))   return 1 + count_locals(f->body);  // init local
        return 0;
    }

    void emit_function(FunDecl* f, ClassDecl* owner = nullptr) {
        ClassInfo* saved_class = current_class;
        int        saved_self  = self_off;
        current_class = owner ? &classes[std::string(owner->name)] : nullptr;

        int n_explicit = (int)f->params.size();
        int n_implicit = owner ? 1 : 0;
        int n_params   = n_explicit + n_implicit;
        int n_body     = count_locals(f->body);
        int n_total    = n_params + n_body;
        int stack_size = ((n_total * 8 + 15) / 16) * 16;
        if (stack_size < 16) stack_size = 16;

        locals.clear();
        is_string.clear();
        next_local   = 0;
        stack_depth  = 0;
        return_label = new_label();
        break_stack.clear();
        continue_stack.clear();
        active_defers.clear();

        std::string sym = owner
            ? std::string(owner->name) + "_" + std::string(f->name)
            : std::string(f->name);

        raw("");
        raw(".globl " + sym);
        raw(".type " + sym + ", @function");
        raw(sym + ":");
        line("push rbp");
        line("mov rbp, rsp");
        line("sub rsp, " + std::to_string(stack_size));

        static const char* argregs[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        int argi = 0;
        if (owner) {
            int off = alloc_local("__self");
            self_off = off;
            line("mov [rbp" + std::to_string(off) + "], " + argregs[argi++]);
        }
        for (int i = 0; i < n_explicit; ++i) {
            int off = alloc_local(std::string(f->params[i].name));
            line("mov [rbp" + std::to_string(off) + "], " + argregs[argi++]);
        }

        emit_stmt(f->body);

        line("xor eax, eax");
        lbl(return_label);
        line("leave");
        line("ret");

        current_class = saved_class;
        self_off      = saved_self;
    }

    // =======================================================================
    // Entry
    // =======================================================================
    std::string run(Program& p) {
        collect(p);
        raw(".intel_syntax noprefix");
        raw(".section .text");
        for (auto* d : p.decls) {
            if (auto* f = dynamic_cast<FunDecl*>(d)) {
                emit_function(f, nullptr);
            }
            else if (auto* c = dynamic_cast<ClassDecl*>(d)) {
                for (auto* m : c->methods) emit_function(m, c);
            }
            // struct: no methods
            // enum: constants, no code
        }
        if (!rodata.empty()) {
            raw("");
            raw(".section .rodata");
            for (auto& s : rodata) raw(s);
        }
        return out.str();
    }
};

} // namespace

std::string emit_asm(Program& p) {
    Emitter e;
    return e.run(p);
}

} // namespace x0p
