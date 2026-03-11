// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Yabloko Labs

#pragma once

#include <fault/types.hpp>
#include <cmath>
#include <cstddef>

namespace fault::monitor {

/// Simple fault detection via range and rate-of-change checks.
///
/// Monitors a signal stream and flags anomalies:
/// - Value outside expected range
/// - Rate of change exceeds threshold
/// - Stale signal (no update within timeout)
///
/// Designed for testing fault detection logic against injected faults.
class RangeDetector {
public:
    struct Config {
        double min_value;         ///< Lower bound.
        double max_value;         ///< Upper bound.
        double max_rate;          ///< Max allowed change per update.
        Timestamp max_age_ns;     ///< Staleness timeout.
    };

    enum class Status : std::uint8_t {
        Normal,
        OutOfRange,
        RateExceeded,
        Stale,
    };

    explicit RangeDetector(Config cfg) : cfg_(cfg) {}

    /// Feed a new sample. Returns detection status.
    [[nodiscard]] Status check(double value, Timestamp now) {
        ++sample_count_;

        // Range check
        if (value < cfg_.min_value || value > cfg_.max_value) {
            ++anomaly_count_;
            return Status::OutOfRange;
        }

        // Rate check (skip on first sample)
        if (sample_count_ > 1) {
            double rate = std::abs(value - last_value_);
            if (rate > cfg_.max_rate) {
                ++anomaly_count_;
                return Status::RateExceeded;
            }
        }

        // Staleness check
        if (sample_count_ > 1 && (now - last_timestamp_) > cfg_.max_age_ns) {
            ++anomaly_count_;
            return Status::Stale;
        }

        last_value_     = value;
        last_timestamp_ = now;
        return Status::Normal;
    }

    [[nodiscard]] std::uint64_t sample_count() const { return sample_count_; }
    [[nodiscard]] std::uint64_t anomaly_count() const { return anomaly_count_; }

    /// Anomaly rate [0, 1].
    [[nodiscard]] double anomaly_rate() const {
        return sample_count_ > 0
                   ? static_cast<double>(anomaly_count_) / static_cast<double>(sample_count_)
                   : 0.0;
    }

private:
    Config        cfg_;
    double        last_value_{0.0};
    Timestamp     last_timestamp_{0};
    std::uint64_t sample_count_{0};
    std::uint64_t anomaly_count_{0};
};

/// Chi-squared innovation monitor for Kalman filter consistency.
///
/// Tracks the normalized innovation squared (NIS) and flags
/// when the filter's innovations are statistically inconsistent
/// with the assumed measurement noise model.
class InnovationMonitor {
public:
    /// @param dim       Measurement dimension (degrees of freedom).
    /// @param threshold Chi-squared threshold for the given dimension.
    ///                  e.g., 5.991 for 2D at 95% confidence.
    InnovationMonitor(std::size_t dim, double threshold)
        : dim_(dim), threshold_(threshold) {}

    /// Feed a NIS value. Returns true if within bounds.
    [[nodiscard]] bool check(double nis) {
        ++sample_count_;
        nis_sum_ += nis;

        if (nis > threshold_) {
            ++exceedance_count_;
            return false;
        }
        return true;
    }

    /// Average NIS (should be close to dim_ if filter is consistent).
    [[nodiscard]] double average_nis() const {
        return sample_count_ > 0 ? nis_sum_ / static_cast<double>(sample_count_) : 0.0;
    }

    /// Fraction of samples exceeding the threshold.
    [[nodiscard]] double exceedance_rate() const {
        return sample_count_ > 0
                   ? static_cast<double>(exceedance_count_) / static_cast<double>(sample_count_)
                   : 0.0;
    }

    /// Filter consistency check: average NIS should be near dim_.
    [[nodiscard]] bool is_consistent(double tolerance = 0.5) const {
        double avg = average_nis();
        return std::abs(avg - static_cast<double>(dim_)) < tolerance * static_cast<double>(dim_);
    }

    [[nodiscard]] std::uint64_t sample_count() const { return sample_count_; }

private:
    std::size_t   dim_;
    double        threshold_;
    double        nis_sum_{0.0};
    std::uint64_t sample_count_{0};
    std::uint64_t exceedance_count_{0};
};

}  // namespace fault::monitor
