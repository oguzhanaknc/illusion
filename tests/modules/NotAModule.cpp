// A shared library without a module entry point, for the module tests.

#if defined(_WIN32)
#define TEST_EXPORT __declspec(dllexport)
#else
#define TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" TEST_EXPORT int illusionTestNotAModule()
{
    return 42;
}
