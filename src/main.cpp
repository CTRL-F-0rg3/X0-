#include "x0p/arena.h"
#include "x0p/diagnostic.h"
#include "x0p/emit_asm.h"
#include "x0p/emit_cpp.h"
#include "x0p/emit_c.h"
#include "x0p/emit_rust.h"
#include "x0p/lexer.h"
#include "x0p/parser.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

static std::string read_file(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { std::fprintf(stderr, "x0p: cannot open '%s'\n", path); std::exit(1); }
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void write_out(const std::string& content, const char* outpath) {
    if (outpath) {
        std::ofstream f(outpath, std::ios::binary);
        if (!f) { std::fprintf(stderr, "x0p: cannot write '%s'\n", outpath); std::exit(1); }
        f << content;
    } else {
        std::fwrite(content.data(), 1, content.size(), stdout);
    }
}

static void usage() {
    std::fprintf(stderr,
        "X0+ transpiler\n"
        "\n"
        "usage:\n"
        "  x0p lex     <file.x0p>          dump tokens\n"
        "  x0p parse   <file.x0p>          dump AST\n"
        "  x0p gena    <file.x0p> [-o out] generate x86-64 assembly\n"
        "  x0p genc    <file.x0p> [-o out] generate C      [TODO]\n"
        "  x0p gencpp  <file.x0p> [-o out] generate C++    [TODO]\n"
        "  x0p genr    <file.x0p> [-o out] generate Rust   [TODO]\n");
}

int main(int argc, char** argv) {
    if (argc < 3) { usage(); return 1; }
    std::string cmd  = argv[1];
    std::string path = argv[2];
    const char* outpath = nullptr;

    for (int i = 3; i < argc; ++i) {
        if (std::string(argv[i]) == "-o" && i + 1 < argc) outpath = argv[++i];
    }

    if (cmd != "lex" && cmd != "parse" && cmd != "gena" &&
        cmd != "genc" && cmd != "gencpp" && cmd != "genr") {
        usage(); return 1;
    }

    std::string src = read_file(path.c_str());
    x0p::Arena arena;
    x0p::DiagnosticEngine diag;
    x0p::Lexer lex(src, 0, arena, diag);
    auto tokens = lex.tokenize();

    if (cmd == "lex") {
        for (const auto& t : tokens) {
            if (t.kind == x0p::TokenKind::Eof) {
                std::printf("%4u:%-3u  %-20s  <eof>\n",
                            t.span.start.line, t.span.start.column,
                            x0p::token_kind_name(t.kind));
                break;
            }
            std::printf("%4u:%-3u  %-20s  '%.*s'\n",
                        t.span.start.line, t.span.start.column,
                        x0p::token_kind_name(t.kind),
                        (int)t.lexeme.size(), t.lexeme.data());
        }
        diag.print_all();
        return diag.has_errors() ? 1 : 0;
    }

    if (cmd == "parse") {
        if (diag.has_errors()) { diag.print_all(); return 1; }
        x0p::Parser parser(std::move(tokens), 0, arena, diag);
        auto prog = parser.parse_program();
        prog.dump();
        diag.print_all();
        return diag.has_errors() ? 1 : 0;
    }

    if (cmd == "genr") {
        if (diag.has_errors()) { diag.print_all(); return 1; }
        x0p::Parser parser(std::move(tokens), 0, arena, diag);
        auto prog = parser.parse_program();
        if (diag.has_errors()) { diag.print_all(); return 1; }
        std::string r = x0p::emit_rust(prog);
        write_out(r, outpath);
        return 0;
    }

    if (cmd == "genc") {
        if (diag.has_errors()) { diag.print_all(); return 1; }
        x0p::Parser parser(std::move(tokens), 0, arena, diag);
        auto prog = parser.parse_program();
        if (diag.has_errors()) { diag.print_all(); return 1; }
        std::string c = x0p::emit_c(prog);
        write_out(c, outpath);
        return 0;
    }

    if (cmd == "gencpp") {
        if (diag.has_errors()) { diag.print_all(); return 1; }
        x0p::Parser parser(std::move(tokens), 0, arena, diag);
        auto prog = parser.parse_program();
        if (diag.has_errors()) { diag.print_all(); return 1; }
        std::string cpp = x0p::emit_cpp(prog);
        write_out(cpp, outpath);
        return 0;
    }

    if (cmd == "gena") {
        if (diag.has_errors()) { diag.print_all(); return 1; }
        x0p::Parser parser(std::move(tokens), 0, arena, diag);
        auto prog = parser.parse_program();
        if (diag.has_errors()) { diag.print_all(); return 1; }
        std::string asmout = x0p::emit_asm(prog);
        write_out(asmout, outpath);
        return 0;
    }

    std::fprintf(stderr, "x0p: '%s' not yet implemented\n", cmd.c_str());
    return 1;
}
