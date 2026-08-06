#pragma once

#include <cstdint>
#include <initializer_list>
#include <unordered_set>

// Predicts the barriers required by whole-resource GPU hazards without
// affecting command submission. Resource id 0 is treated as invalid.
class GodotGpuBarrierShadowPlanner {
public:
    struct Step {
        bool barrier = false;
        bool raw = false;
        bool waw = false;
        bool war = false;
    };

    struct Counters {
        uint64_t barriers = 0;
        uint64_t raw = 0;
        uint64_t waw = 0;
        uint64_t war = 0;
    };

    Step record(std::initializer_list<int64_t> reads,
                std::initializer_list<int64_t> writes) {
        Step step;
        step.raw = intersects(reads, pending_writes_);
        step.waw = intersects(writes, pending_writes_);
        step.war = intersects(writes, pending_reads_);
        step.barrier = step.raw || step.waw || step.war;

        if (step.barrier) {
            ++counters_.barriers;
            counters_.raw += step.raw ? 1u : 0u;
            counters_.waw += step.waw ? 1u : 0u;
            counters_.war += step.war ? 1u : 0u;
            clear_pending();
        }

        insert_valid(pending_reads_, reads);
        insert_valid(pending_writes_, writes);
        return step;
    }

    // Ends the current epoch. Read-only work needs no completion barrier.
    bool finish() noexcept {
        const bool barrier = !pending_writes_.empty();
        if (barrier) ++counters_.barriers;
        clear_pending();
        return barrier;
    }

    // Abandons an epoch without predicting a completion barrier.
    void reset() noexcept { clear_pending(); }

    const Counters &counters() const noexcept { return counters_; }
    bool has_pending_reads() const noexcept { return !pending_reads_.empty(); }
    bool has_pending_writes() const noexcept { return !pending_writes_.empty(); }

private:
    static bool intersects(
        std::initializer_list<int64_t> resources,
        const std::unordered_set<int64_t> &pending) noexcept {
        for (const int64_t resource : resources) {
            if (resource != 0 && pending.find(resource) != pending.end()) {
                return true;
            }
        }
        return false;
    }

    static void insert_valid(std::unordered_set<int64_t> &pending,
                             std::initializer_list<int64_t> resources) {
        for (const int64_t resource : resources) {
            if (resource != 0) pending.insert(resource);
        }
    }

    void clear_pending() noexcept {
        pending_reads_.clear();
        pending_writes_.clear();
    }

    std::unordered_set<int64_t> pending_reads_;
    std::unordered_set<int64_t> pending_writes_;
    Counters counters_;
};
