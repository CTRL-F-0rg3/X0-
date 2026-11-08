#include "x0p/arena.h"
#include <cstdlib>
#include <cassert>
#include <cstdint>

namespace x0p {

struct Arena::Chunk {
    Chunk*  next;
    size_t  used;
    size_t  cap;
    // dane idą zaraz za strukturą (flexible array)
};

Arena::Arena(size_t chunk_size) : chunk_size_(chunk_size) {}

Arena::~Arena() {
    Chunk* c = head_;
    while (c) {
        Chunk* n = c->next;
        std::free(c);
        c = n;
    }
}

void* Arena::alloc(size_t size, size_t alignment) {
    if (!head_ || head_->used + size + alignment > head_->cap) {
        size_t cap = size + alignment > chunk_size_ ? size + alignment : chunk_size_;
        void* mem = std::malloc(sizeof(Chunk) + cap);
        assert(mem && "arena OOM");
        auto* c = static_cast<Chunk*>(mem);
        c->next = head_;
        c->used = 0;
        c->cap  = cap;
        head_   = c;
    }
    // wyrównanie w górę
    uintptr_t base = reinterpret_cast<uintptr_t>(head_ + 1) + head_->used;
    uintptr_t aligned_addr = (base + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
    head_->used = (aligned_addr - reinterpret_cast<uintptr_t>(head_ + 1)) + size;
    return reinterpret_cast<void*>(aligned_addr);
}

void Arena::reset() {
    Chunk* c = head_;
    while (c) {
        c->used = 0;
        c = c->next;
    }
}

} // namespace x0p
