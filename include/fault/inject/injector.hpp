// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Yabloko Labs

#pragma once

#include <fault/types.hpp>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <time.h>

namespace fault::inject {

inline Timestamp now_ns() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<Timestamp>(ts.tv_sec) * 1'000'000'000 + ts.tv_nsec;
}

/// Configuration for a fault injector.
struct InjectorConfig {
    FaultKind    kind         = FaultKind::BitFlip;
    TriggerMode  trigger      = TriggerMode::Immediate;
    Severity     severity     = Severity::Minor;
    double       probability  = 1.0;   ///< For Probabilistic trigger [0, 1].
    std::uint64_t activate_after = 0;  ///< For AfterN trigger.
    std::uint64_t period      = 1;     ///< For Periodic trigger.
    double       magnitude    = 1.0;   ///< Fault-specific parameter.
};

/// Generic fault injector — applies faults to numeric values.
///
/// Supports bit flips, stuck values, drift, and noise injection.
/// Designed for testing resilience of estimation and control pipelines.
class Injector {
public:
    explicit Injector(InjectorConfig cfg)
        : cfg_(cfg), gen_(std::random_device{}()) {}

    void seed(std::uint64_t s) { gen_.seed(s); }

    /// Apply fault to a double value. Returns the (possibly corrupted) value.
    /// Also records whether the fault was active this call.
    [[nodiscard]] double apply(double clean_value) {
        ++call_count_;

        if (!should_fire()) {
            return clean_value;
        }

        ++fire_count_;
        last_event_ = {cfg_.kind, cfg_.severity, now_ns(), fire_count_, cfg_.magnitude};

        switch (cfg_.kind) {
            case FaultKind::BitFlip:
                return apply_bitflip(clean_value);
            case FaultKind::Stuck:
                return apply_stuck(clean_value);
            case FaultKind::Drift:
                return apply_drift(clean_value);
            case FaultKind::Noise:
                return apply_noise(clean_value);
            case FaultKind::Delay:
            case FaultKind::Drop:
            case FaultKind::Byzantine:
                // These affect timing/routing, not values.
                // For value injection, return clean.
                return clean_value;
        }
        return clean_value;
    }

    /// Apply fault to a buffer (for message-level injection).
    void apply_buffer(void* data, std::size_t len) {
        ++call_count_;
        if (!should_fire()) return;
        ++fire_count_;
        last_event_ = {cfg_.kind, cfg_.severity, now_ns(), fire_count_, cfg_.magnitude};

        if (cfg_.kind == FaultKind::BitFlip && len > 0) {
            auto* bytes = static_cast<std::uint8_t*>(data);
            std::size_t n_flips = static_cast<std::size_t>(cfg_.magnitude);
            if (n_flips == 0) n_flips = 1;

            std::uniform_int_distribution<std::size_t> byte_dist(0, len - 1);
            std::uniform_int_distribution<int> bit_dist(0, 7);

            for (std::size_t i = 0; i < n_flips; ++i) {
                std::size_t byte_idx = byte_dist(gen_);
                int bit_idx = bit_dist(gen_);
                bytes[byte_idx] ^= (1u << bit_idx);
            }
        }
    }

    /// Check if the injector would drop this message.
    [[nodiscard]] bool should_drop() {
        ++call_count_;
        if (cfg_.kind != FaultKind::Drop) return false;
        if (!should_fire_internal()) return false;
        ++fire_count_;
        last_event_ = {cfg_.kind, cfg_.severity, now_ns(), fire_count_, cfg_.magnitude};
        return true;
    }

    /// Get delay in nanoseconds (0 if not a delay fault or not firing).
    [[nodiscard]] Timestamp get_delay() {
        ++call_count_;
        if (cfg_.kind != FaultKind::Delay) return 0;
        if (!should_fire_internal()) return 0;
        ++fire_count_;
        auto delay_ns = static_cast<Timestamp>(cfg_.magnitude);
        last_event_ = {cfg_.kind, cfg_.severity, now_ns(), fire_count_, cfg_.magnitude};
        return delay_ns;
    }

    [[nodiscard]] std::uint64_t call_count() const { return call_count_; }
    [[nodiscard]] std::uint64_t fire_count() const { return fire_count_; }
    [[nodiscard]] FaultEvent const& last_event() const { return last_event_; }
    [[nodiscard]] InjectorConfig const& config() const { return cfg_; }

    /// Fire rate as fraction [0, 1].
    [[nodiscard]] double fire_rate() const {
        return call_count_ > 0 ? static_cast<double>(fire_count_) / static_cast<double>(call_count_)
                               : 0.0;
    }

private:
    bool should_fire() {
        return should_fire_internal();
    }

    bool should_fire_internal() {
        switch (cfg_.trigger) {
            case TriggerMode::Immediate:
                return true;
            case TriggerMode::AfterN:
                return call_count_ > cfg_.activate_after;
            case TriggerMode::Probabilistic: {
                std::uniform_real_distribution<double> dist(0.0, 1.0);
                return dist(gen_) < cfg_.probability;
            }
            case TriggerMode::Periodic:
                return cfg_.period > 0 && (call_count_ % cfg_.period == 0);
        }
        return false;
    }

    [[nodiscard]] double apply_bitflip(double value) const {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &value, sizeof(double));

        // Flip the least significant bit(s) of the mantissa
        std::size_t n_flips = static_cast<std::size_t>(cfg_.magnitude);
        if (n_flips == 0) n_flips = 1;
        for (std::size_t i = 0; i < n_flips && i < 52; ++i) {
            bits ^= (1ULL << i);
        }

        double result = 0;
        std::memcpy(&result, &bits, sizeof(double));
        return result;
    }

    [[nodiscard]] double apply_stuck(double value) {
        if (stuck_value_set_) return stuck_value_;
        stuck_value_     = value;
        stuck_value_set_ = true;
        return value;
    }

    [[nodiscard]] double apply_drift(double value) {
        drift_accumulator_ += cfg_.magnitude;
        return value + drift_accumulator_;
    }

    [[nodiscard]] double apply_noise(double value) {
        std::normal_distribution<double> dist(0.0, cfg_.magnitude);
        return value + dist(gen_);
    }

    InjectorConfig  cfg_;
    std::mt19937_64 gen_;
    std::uint64_t   call_count_{0};
    std::uint64_t   fire_count_{0};
    FaultEvent      last_event_{};

    // Stuck fault state
    double stuck_value_{0.0};
    bool   stuck_value_set_{false};

    // Drift fault state
    double drift_accumulator_{0.0};
};

}  // namespace fault::inject
