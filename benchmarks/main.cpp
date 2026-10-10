// Benchmark runner. Usage: illusion_benchmarks [name-filter]
// Environment: ILLUSION_BENCH_TOLERANCE (default 1.0) multiplies every budget.

#include "Budget.hpp"

#include <illusion/core/Log.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

int main(int argc, char** argv)
{
    illusion::Logger::instance().setLevel(illusion::LogLevel::Warn);

    double tolerance = 1.0;
    if (const char* env = std::getenv("ILLUSION_BENCH_TOLERANCE"))
    {
        tolerance = std::strtod(env, nullptr);
        if (tolerance <= 0.0)
        {
            std::fprintf(stderr, "invalid ILLUSION_BENCH_TOLERANCE '%s'\n", env);
            return 2;
        }
    }
    const std::string_view filter = argc > 1 ? argv[1] : "";

    int failures = 0;
    int executed = 0;
    for (const auto& benchmark : illusion::bench::registry())
    {
        if (!filter.empty() && benchmark.name.find(filter) == std::string::npos)
        {
            continue;
        }
        ankerl::nanobench::Bench bench;
        bench.title(benchmark.name).warmup(3).minEpochIterations(5).relative(false);
        benchmark.run(bench);
        ++executed;

        for (const auto& result : bench.results())
        {
            // nanobench reports time per iteration; divide by the batch size to get time per operation.
            const double medianNs =
                result.median(ankerl::nanobench::Result::Measure::elapsed) * 1e9 / result.config().mBatch;
            const double budget = benchmark.budgetNs * tolerance;
            const bool ok = medianNs <= budget;
            std::printf("%-6s %-55s %12.2f ns/op (budget %.2f)\n", ok ? "[ OK ]" : "[FAIL]",
                        result.config().mBenchmarkName.c_str(), medianNs, budget);
            failures += ok ? 0 : 1;
        }
    }

    std::printf("\n%d benchmark group(s), %d over budget (tolerance x%.2f)\n", executed, failures, tolerance);
    return failures == 0 ? 0 : 1;
}
