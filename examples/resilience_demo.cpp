// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Yabloko Labs
//
// Demo: Fault injection campaign against a simulated sensor pipeline.
//
// Tests 4 fault types against a moving target with range monitoring.

#include <fault/scenario/campaign.hpp>
#include <cmath>
#include <cstdio>

int main() {
    using namespace fault;
    using namespace fault::inject;
    using namespace fault::scenario;

    std::printf("=== Fault Injection Resilience Demo ===\n");
    std::printf("Testing sensor pipeline under 4 fault scenarios\n\n");

    // Ground truth: target oscillating in altitude (1000 ± 200m)
    auto truth = [](std::uint64_t step) -> double {
        return 1000.0 + 200.0 * std::sin(static_cast<double>(step) * 0.02);
    };

    // Passthrough system (no filtering — worst case)
    auto system = [](double v) -> double { return v; };

    // Detector: altitude must be in [500, 1500], max rate 50m/step
    monitor::RangeDetector::Config det_cfg{500.0, 1500.0, 50.0, 1'000'000'000};

    Campaign campaign;

    // 1. Bit flip — single bit corruption
    campaign.add_test({
        "bitflip_1bit",
        {FaultKind::BitFlip, TriggerMode::Periodic, Severity::Minor, 1.0, 0, 100, 1.0},
        det_cfg, system, truth,
    });

    // 2. Noise — σ=20m Gaussian noise
    campaign.add_test({
        "noise_20m",
        {FaultKind::Noise, TriggerMode::Immediate, Severity::Minor, 1.0, 0, 1, 20.0},
        det_cfg, system, truth,
    });

    // 3. Drift — 0.1m per sample accumulating bias
    campaign.add_test({
        "drift_0.1m_per_step",
        {FaultKind::Drift, TriggerMode::Immediate, Severity::Major, 1.0, 0, 1, 0.1},
        det_cfg, system, truth,
    });

    // 4. Stuck — sensor freezes at first reading
    campaign.add_test({
        "stuck_at_first_reading",
        {FaultKind::Stuck, TriggerMode::AfterN, Severity::Hazardous, 1.0, 50, 1, 0.0},
        det_cfg, system, truth,
    });

    auto results = campaign.run_all(2000);

    Campaign::print_results(results);

    std::printf("\n--- Analysis ---\n");
    for (auto const& r : results) {
        std::printf("%-25s: %s → %s\n", r.name.c_str(),
                    r.detected ? "DETECTED" : "UNDETECTED",
                    r.system_survived ? "system survived" : "system FAILED");
    }

    return 0;
}
