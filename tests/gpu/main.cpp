#define DOCTEST_CONFIG_IMPLEMENT
#include <illusion/core/Log.hpp>

#include <doctest/doctest.h>

int main(int argc, char** argv)
{
    // Show warnings (including Vulkan validation messages) but not the info chatter.
    illusion::Logger::instance().setLevel(illusion::LogLevel::Warn);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
