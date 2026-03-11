// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Yabloko Labs

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fault {

/// Fault classification.
enum class FaultKind : std::uint8_t {
    BitFlip,      ///< Single or multi-bit flip in data.
    Delay,        ///< Timing delay injection.
    Drop,         ///< Message / sample drop.
    Stuck,        ///< Value frozen at last known good.
    Drift,        ///< Gradual bias accumulation.
    Noise,        ///< Increased noise amplitude.
    Byzantine,    ///< Inconsistent values to different consumers.
};

/// Fault injection trigger mode.
enum class TriggerMode : std::uint8_t {
    Immediate,    ///< Active from start.
    AfterN,       ///< Activate after N calls.
    Probabilistic,///< Random activation per call.
    Periodic,     ///< Every Nth call.
};

/// Fault severity for reporting.
enum class Severity : std::uint8_t {
    Benign,       ///< No observable effect expected.
    Minor,        ///< Degraded but functional.
    Major,        ///< Significant impact on output.
    Hazardous,    ///< System-level failure expected.
};

/// Timestamp in nanoseconds.
using Timestamp = std::int64_t;

/// Fault event record.
struct FaultEvent {
    FaultKind     kind;
    Severity      severity;
    Timestamp     timestamp;
    std::uint64_t injection_count;  ///< How many times this fault fired.
    double        magnitude;        ///< Fault parameter (bits flipped, delay ns, etc.).
};

/// Convert FaultKind to string.
[[nodiscard]] constexpr auto kind_name(FaultKind k) -> std::string_view {
    switch (k) {
        case FaultKind::BitFlip:   return "bit_flip";
        case FaultKind::Delay:     return "delay";
        case FaultKind::Drop:      return "drop";
        case FaultKind::Stuck:     return "stuck";
        case FaultKind::Drift:     return "drift";
        case FaultKind::Noise:     return "noise";
        case FaultKind::Byzantine: return "byzantine";
    }
    return "unknown";
}

}  // namespace fault
