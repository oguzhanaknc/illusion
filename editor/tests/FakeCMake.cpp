// A stand-in for the CMake program in the module build tests (core/TestEditorModules.cpp): it answers the two
// command lines ModuleBuild runs, quickly and without a compiler.
//
//   -S <src> -B <build> ...   configure: writes <build>/CMakeCache.txt and <build>/configure-args.txt; fails
//                             with a CMake error when <src>/FAIL_CONFIGURE exists.
//   --build <build> ...       build: prints Ninja progress, prints <src>/<module>/ERRORS of every module that
//                             has one and fails, or writes a library file for every module (or --target).
//   --record <file> <args...> writes the arguments to <file>, one per line (the code editor of
//                             editor.open_source in the tests).

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{

#if defined(_WIN32)
constexpr const char* kPrefix = "";
constexpr const char* kSuffix = ".dll";
#else
constexpr const char* kPrefix = "lib";
constexpr const char* kSuffix = ".so";
#endif

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string valueOf(const std::string& cache, const std::string& key)
{
    std::istringstream lines(cache);
    for (std::string line; std::getline(lines, line);)
    {
        if (line.rfind(key + "=", 0) == 0)
        {
            return line.substr(key.size() + 1);
        }
    }
    return {};
}

int configure(const std::vector<std::string>& args)
{
    const std::filesystem::path source = args.at(1);
    const std::filesystem::path build = args.at(3);
    std::filesystem::create_directories(build);
    std::string output;
    std::ofstream recorded(build / "configure-args.txt", std::ios::binary);
    for (const std::string& arg : args)
    {
        recorded << arg << '\n';
        const std::string key = "-DILLUSION_MODULE_OUTPUT_DIRECTORY=";
        if (arg.rfind(key, 0) == 0)
        {
            output = arg.substr(key.size());
        }
    }
    if (std::filesystem::exists(source / "FAIL_CONFIGURE"))
    {
        std::cerr << "CMake Error at CMakeLists.txt:6 (find_package):\n"
                  << "  Could not find a package configuration file provided by \"illusion\".\n\n"
                  << "-- Configuring incomplete, errors occurred!\n";
        return 1;
    }
    std::ofstream(build / "CMakeCache.txt", std::ios::binary) << "SOURCE=" << source.generic_string() << '\n'
                                                              << "OUTPUT=" << output << '\n';
    std::cout << "-- Configuring done\n-- Generating done\n";
    return 0;
}

int build(const std::vector<std::string>& args)
{
    const std::filesystem::path tree = args.at(1);
    const std::string cache = readFile(tree / "CMakeCache.txt");
    const std::filesystem::path source = valueOf(cache, "SOURCE");
    const std::filesystem::path output = valueOf(cache, "OUTPUT");
    std::vector<std::string> modules;
    for (std::size_t i = 2; i < args.size(); ++i)
    {
        if (args[i] == "--target")
        {
            modules.assign(args.begin() + static_cast<std::ptrdiff_t>(i) + 1, args.end());
        }
    }
    if (modules.empty())
    {
        for (const auto& entry : std::filesystem::directory_iterator(source))
        {
            if (std::filesystem::exists(entry.path() / "CMakeLists.txt"))
            {
                modules.push_back(entry.path().filename().string());
            }
        }
    }
    std::cout << "[1/2] Building CXX object\n";
    bool failed = false;
    for (const std::string& module : modules)
    {
        const std::filesystem::path errors = source / module / "ERRORS";
        if (std::filesystem::exists(errors))
        {
            std::cout << readFile(errors);
            failed = true;
        }
    }
    if (failed)
    {
        std::cout << "ninja: build stopped: subcommand failed.\n";
        return 1;
    }
    std::filesystem::create_directories(output);
    for (const std::string& module : modules)
    {
        std::ofstream(output / (kPrefix + module + kSuffix), std::ios::binary) << "fake library\n";
    }
    std::cout << "[2/2] Linking CXX shared module\n";
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (args.size() >= 4 && args[0] == "-S")
    {
        return configure(args);
    }
    if (args.size() >= 2 && args[0] == "--record")
    {
        std::ofstream out(args[1], std::ios::binary);
        for (std::size_t i = 2; i < args.size(); ++i)
        {
            out << args[i] << '\n';
        }
        return 0;
    }
    if (args.size() >= 2 && args[0] == "--build")
    {
        return build(args);
    }
    std::cerr << "fake cmake: unexpected arguments\n";
    return 2;
}
