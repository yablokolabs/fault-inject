// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Yabloko Labs

#include <fault/inject/injector.hpp>
#include <fault/monitor/detector.hpp>
#include <fault/scenario/campaign.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);                \
            std::abort();                                                                 \
        }                                                                                 \
    } while (0)

// --- Test 1: Bit flip injection ---
static void test_bitflip() {
    fault::inject::InjectorConfig cfg{};
    cfg.kind      = fault::FaultKind::BitFlip;
    cfg.trigger   = fault::TriggerMode::Immediate;
    cfg.magnitude = 1.0;  // 1 bit flip

    fault::inject::Injector inj(cfg);
    inj.seed(42);

    double clean  = 100.0;
    double faulty = inj.apply(clean);

    // Bit flip should change the value
    CHECK(faulty != clean);
    // But not by a huge amount (LSB of mantissa)
    CHECK(std::abs(faulty - clean) < 1.0);
    CHECK(inj.fire_count() == 1);

    std::printf("  PASS: bit flip injection (clean=%.6f, faulty=%.15f)\n", clean, faulty);
}

// --- Test 2: Stuck fault ---
static void test_stuck() {
    fault::inject::InjectorConfig cfg{};
    cfg.kind    = fault::FaultKind::Stuck;
    cfg.trigger = fault::TriggerMode::Immediate;

    fault::inject::Injector inj(cfg);

    double v1 = inj.apply(10.0);  // First value becomes stuck value
    double v2 = inj.apply(20.0);  // Should return 10.0 (stuck)
    double v3 = inj.apply(30.0);  // Should return 10.0 (stuck)

    CHECK(v1 == 10.0);
    CHECK(v2 == 10.0);
    CHECK(v3 == 10.0);
    CHECK(inj.fire_count() == 3);

    std::printf("  PASS: stuck fault (all reads return %.1f)\n", v1);
}

// --- Test 3: Drift accumulation ---
static void test_drift() {
    fault::inject::InjectorConfig cfg{};
    cfg.kind      = fault::FaultKind::Drift;
    cfg.trigger   = fault::TriggerMode::Immediate;
    cfg.magnitude = 0.1;  // 0.1 per call

    fault::inject::Injector inj(cfg);

    double v1 = inj.apply(100.0);  // 100.0 + 0.1
    double v2 = inj.apply(100.0);  // 100.0 + 0.2
    double v3 = inj.apply(100.0);  // 100.0 + 0.3

    CHECK(std::abs(v1 - 100.1) < 1e-9);
    CHECK(std::abs(v2 - 100.2) < 1e-9);
    CHECK(std::abs(v3 - 100.3) < 1e-9);

    std::printf("  PASS: drift accumulation (%.1f → %.1f → %.1f)\n", v1, v2, v3);
}

// --- Test 4: Probabilistic trigger ---
static void test_probabilistic() {
    fault::inject::InjectorConfig cfg{};
    cfg.kind        = fault::FaultKind::Noise;
    cfg.trigger     = fault::TriggerMode::Probabilistic;
    cfg.probability = 0.5;
    cfg.magnitude   = 1.0;

    fault::inject::Injector inj(cfg);
    inj.seed(42);

    std::uint64_t changed = 0;
    for (int i = 0; i < 1000; ++i) {
        double result = inj.apply(100.0);
        if (result != 100.0) ++changed;
    }

    double rate = static_cast<double>(changed) / 1000.0;
    // Should be roughly 50% ± 10%
    CHECK(rate > 0.3);
    CHECK(rate < 0.7);

    std::printf("  PASS: probabilistic trigger (fire rate=%.1f%%)\n", rate * 100.0);
}

// --- Test 5: Range detector ---
static void test_range_detector() {
    fault::monitor::RangeDetector::Config cfg{};
    cfg.min_value  = -100.0;
    cfg.max_value  = 100.0;
    cfg.max_rate   = 10.0;
    cfg.max_age_ns = 1'000'000'000;  // 1s

    fault::monitor::RangeDetector det(cfg);

    // Normal values
    CHECK(det.check(50.0, 1'000'000) == fault::monitor::RangeDetector::Status::Normal);
    CHECK(det.check(55.0, 2'000'000) == fault::monitor::RangeDetector::Status::Normal);

    // Out of range
    CHECK(det.check(200.0, 3'000'000) == fault::monitor::RangeDetector::Status::OutOfRange);

    // Rate exceeded (jump from 55 to -80 = 135 > 10)
    CHECK(det.check(-80.0, 4'000'000) == fault::monitor::RangeDetector::Status::RateExceeded);

    CHECK(det.anomaly_count() == 2);

    std::printf("  PASS: range detector (anomalies=%lu)\n", det.anomaly_count());
}

// --- Test 6: Campaign runs and reports ---
static void test_campaign() {
    fault::scenario::Campaign campaign;

    // Test: noise injection on a constant signal
    campaign.add_test({
        "noise_on_constant",
        {fault::FaultKind::Noise, fault::TriggerMode::Immediate, fault::Severity::Minor, 1.0, 0, 1,
         5.0},
        {-50.0, 50.0, 20.0, 1'000'000'000},
        [](double v) { return v; },  // passthrough system
        [](std::uint64_t) { return 0.0; },  // constant truth
    });

    // Test: drift on a sine wave
    campaign.add_test({
        "drift_on_sine",
        {fault::FaultKind::Drift, fault::TriggerMode::Immediate, fault::Severity::Major, 1.0, 0, 1,
         0.01},
        {-2.0, 2.0, 0.5, 1'000'000'000},
        [](double v) { return v; },
        [](std::uint64_t step) { return std::sin(static_cast<double>(step) * 0.01); },
    });

    auto results = campaign.run_all(500);

    CHECK(results.size() == 2);
    CHECK(results[0].injections > 0);
    CHECK(results[1].injections > 0);

    // Noise should be detected (range/rate violations)
    // Drift should eventually exceed range
    CHECK(results[1].max_error > 0.0);

    fault::scenario::Campaign::print_results(results);

    std::printf("\n  PASS: campaign execution\n");
}

int main() {
    std::printf("fault-inject tests\n");
    std::printf("===================\n");

    test_bitflip();
    test_stuck();
    test_drift();
    test_probabilistic();
    test_range_detector();
    test_campaign();

    std::printf("\nAll 6 tests passed.\n");
    return 0;
}
