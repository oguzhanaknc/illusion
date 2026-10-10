#define ANKERL_NANOBENCH_IMPLEMENT
#include "Budget.hpp"

#include <utility>

namespace illusion::bench
{

std::vector<Benchmark>& registry()
{
    static std::vector<Benchmark> benchmarks;
    return benchmarks;
}

Registrar::Registrar(std::string name, double budgetNs, std::function<void(ankerl::nanobench::Bench&)> run)
{
    registry().push_back(Benchmark{std::move(name), budgetNs, std::move(run)});
}

} // namespace illusion::bench
