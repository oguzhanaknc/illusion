#define DOCTEST_CONFIG_IMPLEMENT
#include "TestSupport.hpp"

#include <illusion/core/Log.hpp>

#include <doctest/doctest.h>

int main(int argc, char** argv)
{
    // Keep test output readable: only warnings and above reach the console.
    illusion::Logger::instance().setLevel(illusion::LogLevel::Warn);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
