#pragma once

#include "chain/block.hpp"
#include <vector>
#include <cstdint>
#include <algorithm>

namespace pow_engine::miner {

class DifficultyController {
public:
    explicit DifficultyController(uint32_t initial_bits = 0x1f0fffff,
                                  double target_block_time_sec = 4.0,
                                  size_t adjustment_interval = 5)
        : current_bits_(initial_bits),
          target_block_time_sec_(target_block_time_sec),
          interval_(adjustment_interval) {}

    [[nodiscard]] uint32_t get_current_bits() const noexcept {
        return current_bits_;
    }

    void set_bits(uint32_t bits) noexcept {
        current_bits_ = bits;
    }

    [[nodiscard]] double get_target_block_time() const noexcept {
        return target_block_time_sec_;
    }

    void set_target_block_time(double sec) noexcept {
        if (sec > 0.1) target_block_time_sec_ = sec;
    }

    [[nodiscard]] size_t get_interval() const noexcept {
        return interval_;
    }

    /**
     * @brief Evaluates whether to adjust difficulty based on the last `interval_` blocks.
     * Implements Bitcoin-style damped proportional adjustment:
     * New Target = Old Target * (Actual Timespan / Target Timespan)
     * Clamped to [0.25x, 4.0x] to prevent extreme fluctuations.
     */
    uint32_t maybe_adjust(const std::vector<chain::Block>& blockchain) {
        if (blockchain.size() < interval_) {
            return current_bits_;
        }

        // Only adjust at multiples of interval_
        if (blockchain.size() % interval_ != 0) {
            return current_bits_;
        }

        // Measure time taken for the last `interval_` blocks
        double actual_timespan = 0.0;
        const size_t start_idx = blockchain.size() - interval_;
        for (size_t i = start_idx; i < blockchain.size(); ++i) {
            actual_timespan += blockchain[i].mining_duration_seconds;
        }

        const double expected_timespan = target_block_time_sec_ * static_cast<double>(interval_);
        if (actual_timespan <= 0.01) {
            actual_timespan = 0.01;
        }

        // Clamp adjustment ratio to [0.25, 4.0]
        double ratio = actual_timespan / expected_timespan;
        ratio = std::clamp(ratio, 0.25, 4.0);

        chain::Target256 current_target = chain::nbits_to_target(current_bits_);

        // Perform 256-bit multiplication by ratio
        // We approximate using high 64 bits of the target
        int first_idx = -1;
        for (int i = 0; i < 32; ++i) {
            if (current_target[i] != 0) {
                first_idx = i;
                break;
            }
        }

        if (first_idx >= 0) {
            uint64_t high_val = (static_cast<uint64_t>(current_target[first_idx]) << 24);
            if (first_idx + 1 < 32) high_val |= (static_cast<uint64_t>(current_target[first_idx + 1]) << 16);
            if (first_idx + 2 < 32) high_val |= (static_cast<uint64_t>(current_target[first_idx + 2]) << 8);
            if (first_idx + 3 < 32) high_val |= static_cast<uint64_t>(current_target[first_idx + 3]);

            double adjusted_val = static_cast<double>(high_val) * ratio;
            if (adjusted_val < 1.0) adjusted_val = 1.0;

            // Handle possible byte shifts if ratio pushed value across byte boundary
            while (adjusted_val >= 4294967296.0 && first_idx > 0) {
                adjusted_val /= 256.0;
                first_idx--;
            }
            while (adjusted_val < 16777216.0 && first_idx < 31) {
                adjusted_val *= 256.0;
                first_idx++;
            }

            uint32_t final_val = static_cast<uint32_t>(adjusted_val);
            current_target.fill(0);
            if (first_idx < 32) current_target[first_idx] = static_cast<uint8_t>((final_val >> 24) & 0xff);
            if (first_idx + 1 < 32) current_target[first_idx + 1] = static_cast<uint8_t>((final_val >> 16) & 0xff);
            if (first_idx + 2 < 32) current_target[first_idx + 2] = static_cast<uint8_t>((final_val >> 8) & 0xff);
            if (first_idx + 3 < 32) current_target[first_idx + 3] = static_cast<uint8_t>(final_val & 0xff);

            current_bits_ = chain::target_to_nbits(current_target);
        }

        return current_bits_;
    }

private:
    uint32_t current_bits_;
    double target_block_time_sec_;
    size_t interval_;
};

} // namespace pow_engine::miner
