#include "x0p/lexer.h"
#include <cctype>
#include <unordered_map>

namespace x0p {

// ---------------------------------------------------------------------------
// token_kind_name — generowane z X0P_KEYWORDS, więc nigdy nie zapomnisz.
// ---------------------------------------------------------------------------
const char* token_kind_name(TokenKind k) {
    switch (k) {
        case TokenKind::IntLit:    return "IntLit";
        case TokenKind::FloatLit:  return "FloatLit";
        case TokenKind::StringLit: return "StringLit";
        case TokenKind::CharLit:   return "CharLit";
        case TokenKind::Ident:     return "Ident";

        #define X(str, kind) case TokenKind::kind: return #kind;
        X0P_KEYWORDS(X)
        #undef X

        case TokenKind::LParen:           return "LParen";
        case TokenKind::RParen:           return "RParen";
        case TokenKind::LBrace:           return "LBrace";
        case TokenKind::RBrace:           return "RBrace";
        case TokenKind::LBracket:         return "LBracket";
        case TokenKind::RBracket:         return "RBracket";
        case TokenKind::Semi:             return "Semi";
        case TokenKind::Comma:            return "Comma";
        case TokenKind::Dot:              return "Dot";
        case TokenKind::DotDot:           return "DotDot";
        case TokenKind::DotDotDot:        return "DotDotDot";
        case TokenKind::Colon:            return "Colon";
        case TokenKind::ColonColon:       return "ColonColon";
        case TokenKind::Arrow:            return "Arrow";
        case TokenKind::FatArrow:         return "FatArrow";
        case TokenKind::At:               return "At";
        case TokenKind::Plus:             return "Plus";
        case TokenKind::Minus:            return "Minus";
        case TokenKind::Star:             return "Star";
        case TokenKind::Slash:            return "Slash";
        case TokenKind::Percent:          return "Percent";
        case TokenKind::Amp:              return "Amp";
        case TokenKind::Pipe:             return "Pipe";
        case TokenKind::Caret:            return "Caret";
        case TokenKind::Tilde:            return "Tilde";
        case TokenKind::Bang:             return "Bang";
        case TokenKind::Eq:               return "Eq";
        case TokenKind::EqEq:             return "EqEq";
        case TokenKind::BangEq:           return "BangEq";
        case TokenKind::Lt:               return "Lt";
        case TokenKind::Gt:               return "Gt";
        case TokenKind::LtEq:             return "LtEq";
        case TokenKind::GtEq:             return "GtEq";
        case TokenKind::AmpAmp:           return "AmpAmp";
        case TokenKind::PipePipe:         return "PipePipe";
        case TokenKind::Shl:              return "Shl";
        case TokenKind::Shr:              return "Shr";
        case TokenKind::PlusEq:           return "PlusEq";
        case TokenKind::MinusEq:          return "MinusEq";
        case TokenKind::StarEq:           return "StarEq";
        case TokenKind::SlashEq:          return "SlashEq";
        case TokenKind::PercentEq:        return "PercentEq";
        case TokenKind::AmpEq:            return "AmpEq";
        case TokenKind::PipeEq:           return "PipeEq";
        case TokenKind::CaretEq:          return "CaretEq";
        case TokenKind::ShlEq:            return "ShlEq";
        case TokenKind::ShrEq:            return "ShrEq";
        case TokenKind::Question:         return "Question";
        case TokenKind::QuestionQuestion: return "QuestionQuestion";
        case TokenKind::PlusPlus:         return "PlusPlus";
        case TokenKind::MinusMinus:       return "MinusMinus";
        case TokenKind::Eof:              return "Eof";
        case TokenKind::Unknown:          return "Unknown";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// lookup słów kluczowych (budowane raz, statyczne)
// ---------------------------------------------------------------------------
static const std::unordered_map<std::string_view, TokenKind>& keyword_table() {
    static const std::unordered_map<std::string_view, TokenKind> table = {
        #define X(str, kind) { #str, TokenKind::kind },
        X0P_KEYWORDS(X)
        #undef X
    };
    return table;
}

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------
Lexer::Lexer(std::string_view source, uint32_t file_id,
             Arena& arena, DiagnosticEngine& diag)
    : src_(source), file_id_(file_id), arena_(arena), diag_(diag) {}

char Lexer::peek(size_t ahead) const {
    size_t p = pos_ + ahead;
    return p < src_.size() ? src_[p] : '\0';
}

char Lexer::advance() {
    char c = src_[pos_++];
    if (c == '\n') { line_++; col_ = 1; } else { col_++; }
    return c;
}

bool Lexer::match(char c) {
    if (peek() == c) { advance(); return true; }
    return false;
}

SourceLocation Lexer::loc() const {
    return SourceLocation{ static_cast<uint32_t>(pos_), line_, col_ };
}

void Lexer::skip_whitespace_and_comments() {
    for (;;) {
        char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(); continue; }
        if (c == '/' && peek(1) == '/') {
            while (peek() && peek() != '\n') advance();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            advance(); advance();
            while (peek() && !(peek() == '*' && peek(1) == '/')) advance();
            if (peek()) { advance(); advance(); }
            else {
                diag_.report({Severity::Error, "E0001",
                              {loc(), loc(), file_id_},
                              "unterminated block comment", {}});
            }
            continue;
        }
        break;
    }
}

Token Lexer::make(TokenKind k, size_t start) {
    SourceSpan span{
        SourceLocation{ static_cast<uint32_t>(start),
                        tok_start_line_, tok_start_col_ },
        loc(),
        file_id_
    };
    return Token{ k, src_.substr(start, pos_ - start), span };
}

Token Lexer::lex_number() {
    size_t start = pos_;
    while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    bool is_float = false;
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
        is_float = true;
        advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    if (peek() == 'e' || peek() == 'E') {
        is_float = true;
        advance();
        if (peek() == '+' || peek() == '-') advance();
        while (std::isdigit(static_cast<unsigned char>(peek()))) advance();
    }
    return make(is_float ? TokenKind::FloatLit : TokenKind::IntLit, start);
}

Token Lexer::lex_ident_or_keyword() {
    size_t start = pos_;
    while (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_') advance();
    std::string_view word = src_.substr(start, pos_ - start);
    auto it = keyword_table().find(word);
    if (it != keyword_table().end()) return make(it->second, start);
    return make(TokenKind::Ident, start);
}

Token Lexer::lex_string() {
    size_t start = pos_;
    advance();
    while (peek() && peek() != '"') {
        if (peek() == '\\') advance();
        advance();
    }
    if (peek() == '"') advance();
    else {
        diag_.report({Severity::Error, "E0002",
                      {loc(), loc(), file_id_},
                      "unterminated string literal", {}});
    }
    return make(TokenKind::StringLit, start);
}

Token Lexer::lex_char() {
    size_t start = pos_;
    advance();
    if (peek() == '\\') advance();
    advance();
    if (peek() == '\'') advance();
    return make(TokenKind::CharLit, start);
}

Token Lexer::lex_punct() {
    size_t start = pos_;
    char c = advance();
    switch (c) {
        case '(':  return make(TokenKind::LParen, start);
        case ')':  return make(TokenKind::RParen, start);
        case '{':  return make(TokenKind::LBrace, start);
        case '}':  return make(TokenKind::RBrace, start);
        case '[':  return make(TokenKind::LBracket, start);
        case ']':  return make(TokenKind::RBracket, start);
        case ';':  return make(TokenKind::Semi, start);
        case ',':  return make(TokenKind::Comma, start);
        case '@':  return make(TokenKind::At, start);
        case '.':
            if (peek() == '.' && peek(1) == '.') { advance(); advance(); return make(TokenKind::DotDotDot, start); }
            if (peek() == '.')                   { advance();          return make(TokenKind::DotDot, start); }
            return make(TokenKind::Dot, start);
        case ':':
            if (match(':')) return make(TokenKind::ColonColon, start);
            return make(TokenKind::Colon, start);
        case '+':
            if (match('=')) return make(TokenKind::PlusEq, start);
            if (match('+')) return make(TokenKind::PlusPlus, start);
            return make(TokenKind::Plus, start);
        case '-':
            if (match('>')) return make(TokenKind::Arrow, start);
            if (match('=')) return make(TokenKind::MinusEq, start);
            if (match('-')) return make(TokenKind::MinusMinus, start);
            return make(TokenKind::Minus, start);
        case '*':  if (match('=')) return make(TokenKind::StarEq, start);   return make(TokenKind::Star, start);
        case '/':  if (match('=')) return make(TokenKind::SlashEq, start);  return make(TokenKind::Slash, start);
        case '%':  if (match('=')) return make(TokenKind::PercentEq, start);return make(TokenKind::Percent, start);
        case '&':
            if (match('&')) return make(TokenKind::AmpAmp, start);
            if (match('=')) return make(TokenKind::AmpEq, start);
            return make(TokenKind::Amp, start);
        case '|':
            if (match('|')) return make(TokenKind::PipePipe, start);
            if (match('=')) return make(TokenKind::PipeEq, start);
            return make(TokenKind::Pipe, start);
        case '^':
            if (match('=')) return make(TokenKind::CaretEq, start);
            return make(TokenKind::Caret, start);
        case '~':  return make(TokenKind::Tilde, start);
        case '!':
            if (match('=')) return make(TokenKind::BangEq, start);
            return make(TokenKind::Bang, start);
        case '=':
            if (match('=')) return make(TokenKind::EqEq, start);
            if (match('>')) return make(TokenKind::FatArrow, start);
            return make(TokenKind::Eq, start);
        case '<':
            if (match('=')) return make(TokenKind::LtEq, start);
            if (match('<')) return match('=') ? make(TokenKind::ShlEq, start)
                                              : make(TokenKind::Shl, start);
            return make(TokenKind::Lt, start);
        case '>':
            if (match('=')) return make(TokenKind::GtEq, start);
            if (match('>')) return match('=') ? make(TokenKind::ShrEq, start)
                                              : make(TokenKind::Shr, start);
            return make(TokenKind::Gt, start);
        case '?':
            if (match('?')) return make(TokenKind::QuestionQuestion, start);
            return make(TokenKind::Question, start);
    }
    return make(TokenKind::Unknown, start);
}

Token Lexer::next() {
    skip_whitespace_and_comments();
    tok_start_pos_  = pos_;
    tok_start_line_ = line_;
    tok_start_col_  = col_;

    if (pos_ >= src_.size()) {
        SourceLocation l = loc();
        return Token{ TokenKind::Eof, {}, SourceSpan{l, l, file_id_} };
    }
    char c = peek();
    if (std::isdigit(static_cast<unsigned char>(c)))                     return lex_number();
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')        return lex_ident_or_keyword();
    if (c == '"')                                                        return lex_string();
    if (c == '\'')                                                       return lex_char();
    return lex_punct();
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;
    out.reserve(src_.size() / 4);
    for (;;) {
        Token t = next();
        bool eof = (t.kind == TokenKind::Eof);
        out.push_back(t);
        if (eof) break;
    }
    return out;
}

} // namespace x0p
