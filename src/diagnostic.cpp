#include "x0p/diagnostic.h"
#include <cstdio>

namespace x0p {

    void DiagnosticEngine::report(Diagnostic d) {
        diagnostics_.push_back(std::move(d));
    }

    bool DiagnosticEngine::has_errors() const {
        for (const auto& d : diagnostics_)
            if (d.severity == Severity::Error || d.severity == Severity::Fatal)
                return true;
        return false;
    }

    size_t DiagnosticEngine::error_count() const {
        size_t n = 0;
        for (const auto& d : diagnostics_)
            if (d.severity == Severity::Error || d.severity == Severity::Fatal)
                ++n;
        return n;
    }

    static const char* sev_str(Severity s) {
        switch (s) {
            case Severity::Note:    return "note";
            case Severity::Warning: return "warning";
            case Severity::Error:   return "error";
            case Severity::Fatal:   return "fatal";
        }
        return "?";
    }

    void DiagnosticEngine::print_all() const {
        for (const auto& d : diagnostics_) {
            std::fprintf(stderr, "file%d:%u:%u: %s [%s]: %s\n",
                         d.span.file_id,
                         d.span.start.line,
                         d.span.start.column,
                         sev_str(d.severity),
                         d.code.c_str(),
                         d.message.c_str());
            for (const auto& note : d.notes)
                std::fprintf(stderr, "    note: %s\n", note.c_str());
        }
    }

} // namespace x0p
