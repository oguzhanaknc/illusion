#pragma once

// Minimal benchmark registry with performance budgets.
//
// Each benchmark measures one operation with nanobench and declares a budget: the maximum allowed
// median time per operation in nanoseconds on a reference machine. The runner fails (exit code 1) if
// any median exceeds budget * tolerance. Budgets are deliberately generous; they exist to catch large
// regressions (accidental allocations, O(n^2) paths), not to track small changes.

#include <nanobench.h>

#include <functional>
#include <string>
#include <vector>

namespace illusion::bench
{

struct Benchmark
{
    std::string name;
    double budgetNs; // per operation
    std::function<void(ankerl::nanobench::Bench&)> run;
};

std::vector<Benchmark>& registry();

struct Registrar
{
    Registrar(std::string name, double budgetNs, std::function<void(ankerl::nanobench::Bench&)> run);
};

} // namespace illusion::bench

#define ILL_BENCH_CONCAT_IMPL(a, b) a##b
#define ILL_BENCH_CONCAT(a, b) ILL_BENCH_CONCAT_IMPL(a, b)

/// Registers a benchmark: ILL_BENCHMARK("name", budgetNsPerOp) { bench.run(...); }
#define ILL_BENCHMARK(name, budgetNs)                                                                        \
    static void ILL_BENCH_CONCAT(illBench_, __LINE__)(ankerl::nanobench::Bench & bench);                     \
    static const ::illusion::bench::Registrar ILL_BENCH_CONCAT(illBenchRegistrar_, __LINE__){                \
        name, budgetNs, &ILL_BENCH_CONCAT(illBench_, __LINE__)};                                             \
    static void ILL_BENCH_CONCAT(illBench_, __LINE__)(ankerl::nanobench::Bench & bench)
