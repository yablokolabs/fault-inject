// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Yabloko Labs

#pragma once

#include <fault/inject/injector.hpp>
#include <fault/monitor/detector.hpp>
#include <functional>
#include <string>
#include <vector>

namespace fault::scenario {

/// Result of a single fault injection test.
struct TestResult {
    std::string      name;
    FaultKind        fault_kind;
    Severity         severity;
    std::uint64_t    injections;     ///< Number of times fault fired.
    std::uint64_t    detections;     ///< Number of anomalies detected.
    double           max_error;      ///< Maximum observed error.
    double           mean_error;     ///< Mean observed error.
    bool             detected;       ///< Was the fault detected by the monitor?
    bool             system_survived;///< Did the system remain within bounds?
};

/// A fault injection campaign — runs multiple scenarios against a system.
///
/// Usage:
///   Campaign campaign;
///   campaign.add_test("bit_flip_nav", config, system_fn, detector_config);
///   auto results = campaign.run_all(1000);  // 1000 steps each
class Campaign {
public:
    struct TestCase {
        std::string                        name;
        inject::InjectorConfig             fault_config;
        monitor::RangeDetector::Config     detector_config;

        /// System function: takes clean_value, returns actual output.
        /// The campaign wraps this with fault injection.
        std::function<double(double)>      system_fn;

        /// Ground truth generator: step → clean value.
        std::function<double(std::uint64_t)> truth_fn;
    };

    void add_test(TestCase tc) { tests_.push_back(std::move(tc)); }

    /// Run all test cases for the given number of steps.
    [[nodiscard]] std::vector<TestResult> run_all(std::uint64_t steps) {
        std::vector<TestResult> results;
        results.reserve(tests_.size());

        for (auto& tc : tests_) {
            results.push_back(run_one(tc, steps));
        }
        return results;
    }

    /// Print results summary to stdout.
    static void print_results(std::vector<TestResult> const& results) {
        std::printf("\n%-25s %-10s %-8s %8s %8s %10s %10s %8s %8s\n", "Test", "Fault",
                    "Severity", "Injects", "Detects", "MaxErr", "MeanErr", "Detect?", "Survive?");
        std::printf(
            "%-25s %-10s %-8s %8s %8s %10s %10s %8s %8s\n",
            "-------------------------", "----------", "--------", "--------", "--------",
            "----------", "----------", "--------", "--------");

        for (auto const& r : results) {
            std::printf("%-25s %-10.*s %-8s %8lu %8lu %10.4f %10.4f %8s %8s\n", r.name.c_str(),
                        static_cast<int>(kind_name(r.fault_kind).size()),
                        kind_name(r.fault_kind).data(),
                        r.severity == Severity::Benign    ? "benign"
                        : r.severity == Severity::Minor   ? "minor"
                        : r.severity == Severity::Major   ? "major"
                                                          : "hazard",
                        r.injections, r.detections, r.max_error, r.mean_error,
                        r.detected ? "YES" : "no", r.system_survived ? "YES" : "no");
        }
    }

private:
    [[nodiscard]] TestResult run_one(TestCase& tc, std::uint64_t steps) {
        inject::Injector injector(tc.fault_config);
        injector.seed(42);

        monitor::RangeDetector detector(tc.detector_config);

        double max_err  = 0.0;
        double err_sum  = 0.0;
        bool   detected = false;

        Timestamp sim_time = 0;
        Timestamp const dt_ns = 10'000'000;  // 10ms steps

        for (std::uint64_t step = 0; step < steps; ++step) {
            sim_time += dt_ns;

            double clean = tc.truth_fn(step);
            double corrupted = injector.apply(clean);
            double output = tc.system_fn(corrupted);

            double err = std::abs(output - clean);
            max_err = std::max(max_err, err);
            err_sum += err;

            auto status = detector.check(output, sim_time);
            if (status != monitor::RangeDetector::Status::Normal) {
                detected = true;
            }
        }

        return {tc.name,
                tc.fault_config.kind,
                tc.fault_config.severity,
                injector.fire_count(),
                detector.anomaly_count(),
                max_err,
                err_sum / static_cast<double>(steps),
                detected,
                max_err < (tc.detector_config.max_value - tc.detector_config.min_value)};
    }

    std::vector<TestCase> tests_;
};

}  // namespace fault::scenario
