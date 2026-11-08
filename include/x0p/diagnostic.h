#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace x0p {

    struct SourceLocation {
        uint32_t offset;
        uint32_t line;    // 1-based
        uint32_t column;  // 1-based
    };

    struct SourceSpan {
        SourceLocation start;
        SourceLocation end;
        uint32_t       file_id = 0;
    };

    enum class Severity { Note, Warning, Error, Fatal };

    struct Diagnostic {
        Severity                 severity;
        std::string              code;      // np. "E0001"
        SourceSpan               span;
        std::string              message;
        std::vector<std::string> notes;
    };

    class DiagnosticEngine {
    public:
        void report(Diagnostic d);
        bool has_errors() const;
        void print_all() const;
        size_t error_count() const;

    private:
        std::vector<Diagnostic> diagnostics_;
    };

} // namespace x0p
