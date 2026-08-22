#ifndef TVPDecodeArenaH
#define TVPDecodeArenaH

#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

class TVPDecodeArena {
    static constexpr size_t kDefaultCapacity = 4 * 1024 * 1024; // 4MB
    static constexpr size_t kMaxCapacity = 64 * 1024 * 1024;    // 64MB

    uint8_t *base_ = nullptr;
    size_t capacity_ = 0;
    size_t offset_ = 0;
    bool active_ = false;
    size_t pageSize_ = 0;
    size_t peakOffset_ = 0;
    size_t allocCount_ = 0;
    size_t lastPeakBytes_ = 0;
    size_t lastAllocCount_ = 0;

    size_t roundToPage(size_t n) const {
        return (n + pageSize_ - 1) & ~(pageSize_ - 1);
    }

    bool grow(size_t needed) {
        if(needed > kMaxCapacity) return false;
        if(base_) return capacity_ >= needed;

        // The decoder libraries retain pointers to their allocation results
        // (for example, libpng keeps png_struct/png_info pointers while it
        // parses chunk metadata).  Moving the arena on growth invalidates
        // those pointers, so reserve one stable mapping for the lifetime of
        // this decode instead of copying to a new mapping.
        const size_t mapSize = roundToPage(kMaxCapacity);
        uint8_t *newBase = (uint8_t *)mmap(nullptr, mapSize,
                                           PROT_READ | PROT_WRITE,
                                           MAP_PRIVATE | MAP_ANON, -1, 0);
        if(newBase == MAP_FAILED) return false;
        base_ = newBase;
        capacity_ = mapSize;
        return capacity_ >= needed;
    }

public:
    TVPDecodeArena() {
        pageSize_ = (size_t)sysconf(_SC_PAGESIZE);
        if(pageSize_ == 0) pageSize_ = 4096;
    }

    ~TVPDecodeArena() {
        if(base_) munmap(base_, capacity_);
    }

    void Begin() {
        offset_ = 0;
        peakOffset_ = 0;
        allocCount_ = 0;
        active_ = true;
    }

    void End() {
        active_ = false;
        lastPeakBytes_ = peakOffset_;
        lastAllocCount_ = allocCount_;
        offset_ = 0;
        if(base_ && capacity_ > kDefaultCapacity) {
            munmap(base_, capacity_);
            base_ = nullptr;
            capacity_ = 0;
        }
    }

    size_t GetLastPeakBytes() const { return lastPeakBytes_; }
    size_t GetLastAllocCount() const { return lastAllocCount_; }

    bool IsActive() const { return active_; }

    bool Owns(const void *ptr) const {
        if(!base_ || !ptr) return false;
        const auto address = reinterpret_cast<uintptr_t>(ptr);
        const auto begin = reinterpret_cast<uintptr_t>(base_);
        return address >= begin && address < begin + offset_;
    }

    void *Alloc(size_t size) {
        if(!active_) return nullptr;
        size = (size + 15) & ~(size_t)15; // 16-byte align
        size_t newOffset = offset_ + size;
        if(newOffset > capacity_) {
            if(!grow(newOffset)) return nullptr;
        }
        void *ptr = base_ + offset_;
        offset_ = newOffset;
        allocCount_++;
        if(offset_ > peakOffset_) peakOffset_ = offset_;
        return ptr;
    }

    static TVPDecodeArena &Instance() {
        static thread_local TVPDecodeArena arena;
        return arena;
    }
};

inline bool TVPDecodeArenaActive() {
    return TVPDecodeArena::Instance().IsActive();
}
inline void *TVPDecodeArenaAlloc(size_t size) {
    return TVPDecodeArena::Instance().Alloc(size);
}
inline bool TVPDecodeArenaOwns(const void *ptr) {
    return TVPDecodeArena::Instance().Owns(ptr);
}
inline size_t TVPDecodeArenaLastPeak() {
    return TVPDecodeArena::Instance().GetLastPeakBytes();
}
inline size_t TVPDecodeArenaLastCount() {
    return TVPDecodeArena::Instance().GetLastAllocCount();
}

#else
inline bool TVPDecodeArenaActive() { return false; }
inline void *TVPDecodeArenaAlloc(size_t) { return nullptr; }
inline bool TVPDecodeArenaOwns(const void *) { return false; }
inline size_t TVPDecodeArenaLastPeak() { return 0; }
inline size_t TVPDecodeArenaLastCount() { return 0; }
#endif

#endif
