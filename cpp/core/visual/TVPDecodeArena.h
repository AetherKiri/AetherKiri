#ifndef TVPDecodeArenaH
#define TVPDecodeArenaH

#if defined(__APPLE__) || defined(__linux__) || defined(__ANDROID__)
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

// Decode libraries retain many of the pointers returned by their allocator
// until the image has been destroyed.  The arena must therefore never move a
// live allocation while growing.  Use mmap-backed segments instead of
// reallocating/copying the original block.
class TVPDecodeArena {
    static constexpr size_t kDefaultCapacity = 4 * 1024 * 1024; // 4MB
    static constexpr size_t kMaxCapacity = 64 * 1024 * 1024;    // 64MB

    struct Block {
        uint8_t *base = nullptr;
        size_t capacity = 0;
        size_t offset = 0;
    };

    std::vector<Block> blocks_;
    size_t mappedCapacity_ = 0;
    bool active_ = false;
    size_t pageSize_ = 0;
    size_t currentBlock_ = 0;
    size_t usedBytes_ = 0;
    size_t peakBytes_ = 0;
    size_t allocCount_ = 0;
    size_t lastPeakBytes_ = 0;
    size_t lastAllocCount_ = 0;

    size_t roundToPage(size_t n) const {
        if(n > std::numeric_limits<size_t>::max() - (pageSize_ - 1)) return 0;
        return ((n + pageSize_ - 1) / pageSize_) * pageSize_;
    }

    bool addBlock(size_t needed) {
        if(needed > kMaxCapacity || mappedCapacity_ >= kMaxCapacity)
            return false;

        const size_t remaining = kMaxCapacity - mappedCapacity_;
        size_t capacity = std::min(kDefaultCapacity, remaining);
        while(capacity < needed && capacity <= remaining / 2)
            capacity *= 2;
        if(capacity < needed) capacity = needed;
        capacity = roundToPage(capacity);
        if(capacity == 0 || capacity > remaining) return false;

        uint8_t *base = static_cast<uint8_t *>(
            mmap(nullptr, capacity, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANON, -1, 0));
        if(base == MAP_FAILED) return false;

        blocks_.push_back({base, capacity, 0});
        mappedCapacity_ += capacity;
        currentBlock_ = blocks_.size() - 1;
        return true;
    }

    void releaseBlock(Block &block) {
        if(block.base != nullptr) munmap(block.base, block.capacity);
        block = {};
    }

public:
    TVPDecodeArena() {
        const long page_size = sysconf(_SC_PAGESIZE);
        pageSize_ = page_size > 0 ? static_cast<size_t>(page_size) : 4096;
    }

    ~TVPDecodeArena() {
        for(Block &block : blocks_) releaseBlock(block);
    }

    TVPDecodeArena(const TVPDecodeArena &) = delete;
    TVPDecodeArena &operator=(const TVPDecodeArena &) = delete;

    void Begin() {
        for(Block &block : blocks_) block.offset = 0;
        currentBlock_ = 0;
        usedBytes_ = 0;
        peakBytes_ = 0;
        allocCount_ = 0;
        active_ = true;
    }

    void End() {
        active_ = false;
        lastPeakBytes_ = peakBytes_;
        lastAllocCount_ = allocCount_;
        usedBytes_ = 0;
        peakBytes_ = 0;
        allocCount_ = 0;
        currentBlock_ = 0;

        // Keep only the normal first block hot between image loads. Large or
        // overflow mappings are returned immediately so one unusual image
        // does not permanently raise the process footprint.
        if(!blocks_.empty() && blocks_.front().capacity == kDefaultCapacity) {
            for(size_t i = 1; i < blocks_.size(); ++i)
                releaseBlock(blocks_[i]);
            blocks_.resize(1);
            blocks_.front().offset = 0;
            mappedCapacity_ = blocks_.front().capacity;
        } else {
            for(Block &block : blocks_) releaseBlock(block);
            blocks_.clear();
            mappedCapacity_ = 0;
        }
    }

    size_t GetLastPeakBytes() const { return lastPeakBytes_; }
    size_t GetLastAllocCount() const { return lastAllocCount_; }

    bool IsActive() const { return active_; }

    bool Owns(const void *pointer) const {
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        for(const Block &block : blocks_) {
            const uintptr_t begin = reinterpret_cast<uintptr_t>(block.base);
            if(address >= begin && address - begin < block.capacity)
                return true;
        }
        return false;
    }

    void *Alloc(size_t size) {
        if(!active_ || size > std::numeric_limits<size_t>::max() - 15)
            return nullptr;
        size = (size + 15) & ~(size_t)15; // 16-byte align
        if(size == 0) size = 16;
        if(usedBytes_ > kMaxCapacity - size) return nullptr;

        if(blocks_.empty() && !addBlock(size)) return nullptr;
        while(currentBlock_ < blocks_.size()) {
            Block &block = blocks_[currentBlock_];
            if(size <= block.capacity - block.offset) {
                void *pointer = block.base + block.offset;
                block.offset += size;
                usedBytes_ += size;
                peakBytes_ = std::max(peakBytes_, usedBytes_);
                ++allocCount_;
                return pointer;
            }
            ++currentBlock_;
        }
        if(!addBlock(size)) return nullptr;

        Block &block = blocks_[currentBlock_];
        void *pointer = block.base;
        block.offset = size;
        usedBytes_ += size;
        peakBytes_ = std::max(peakBytes_, usedBytes_);
        ++allocCount_;
        return pointer;
    }

    static TVPDecodeArena &Instance() {
        static thread_local TVPDecodeArena arena;
        return arena;
    }
};

inline bool TVPDecodeArenaActive() {
    return TVPDecodeArena::Instance().IsActive();
}
inline bool TVPDecodeArenaOwns(const void *pointer) {
    return TVPDecodeArena::Instance().Owns(pointer);
}
inline void *TVPDecodeArenaAlloc(size_t size) {
    return TVPDecodeArena::Instance().Alloc(size);
}
inline size_t TVPDecodeArenaLastPeak() {
    return TVPDecodeArena::Instance().GetLastPeakBytes();
}
inline size_t TVPDecodeArenaLastCount() {
    return TVPDecodeArena::Instance().GetLastAllocCount();
}

#else
inline bool TVPDecodeArenaActive() { return false; }
inline bool TVPDecodeArenaOwns(const void *) { return false; }
inline void *TVPDecodeArenaAlloc(size_t) { return nullptr; }
inline size_t TVPDecodeArenaLastPeak() { return 0; }
inline size_t TVPDecodeArenaLastCount() { return 0; }
#endif

#endif
