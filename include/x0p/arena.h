#pragma once
#include <cstddef>
#include <new>
#include <utility>

namespace x0p {

    // Arena — wszystkie alokacje fazy w jednym miejscu.
    // Reset na koniec fazy = zero wycieków, zero free() w drzewie AST.
    class Arena {
    public:
        explicit Arena(size_t chunk_size = 64 * 1024);
        ~Arena();
        Arena(const Arena&) = delete;
        Arena& operator=(const Arena&) = delete;

        void* alloc(size_t size, size_t alignment = alignof(std::max_align_t));
        void reset();

        template <typename T, typename... Args>
        T* make(Args&&... args) {
            void* mem = alloc(sizeof(T), alignof(T));
            return new (mem) T(std::forward<Args>(args)...);
        }

    private:
        struct Chunk;
        Chunk*  head_       = nullptr;
        size_t  chunk_size_;
    };

} // namespace x0p
