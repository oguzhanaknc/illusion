// A stand-in for the CMake program in the module build tests (core/TestEditorModules.cpp): it answers the two
// command lines ModuleBuild runs, quickly and without a compiler.
//
//   -S <src> -B <build> ...   configure: writes <build>/CMakeCache.txt, <build>/configure-args.txt and
//                             <build>/configure-env.txt (the value of ILLUSION_TEST_ENVIRONMENT); fails with
//                             a CMake error when <src>/FAIL_CONFIGURE exists, leaving the cache behind as
//                             CMake does.
//   --build <build> ...       build: prints Ninja progress, prints <src>/<module>/ERRORS of every module that
//                             has one and fails, or writes a library file for every module (or --target).
//                             The build of an exported game (a <src>/CMakeLists.txt with OUTPUT_NAME
//                             [=[<name>]=]) also writes <build>/game/<name>[.exe]
//                             (<name>.app/Contents/MacOS/<name> on macOS).
//   --install <build> ... --prefix <dir>
//                             install of an exported game (export.build): copies the game from <build>/game
//                             to <dir> and <src>/data to its data directory (Contents/Resources/data on
//                             macOS).
//   --record <file> <args...> writes the arguments to <file>, one per line (the code editor of
//                             editor.open_source in the tests).
//   --sign <log> <file>       a recording sign command: appends <file> to <log> and kSignature to <file>;
//   fails
//                             when <file> does not exist.
//   --config <CPackConfig.cmake> -G <generator> -C <config> -B <dir> -D CPACK_PACKAGE_FILE_NAME=<name> ...
//                             cpack (this program built as cpack[.exe]): writes <dir>/<name><extension of the
//                             generator> holding the generator and the game in the build tree, and the
//                             arguments to <dir>/cpack-args.txt.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

#if defined(_WIN32)
constexpr const char* kPrefix = "";
constexpr const char* kSuffix = ".dll";
constexpr const char* kExecutableSuffix = ".exe";
#else
constexpr const char* kPrefix = "lib";
constexpr const char* kSuffix = ".so";
constexpr const char* kExecutableSuffix = "";
#endif

// What --sign appends to the file it signs.
constexpr const char* kSignature = "ILLUSION-TEST-SIGNATURE";

std::string readFile(const std::filesystem::path& path)
{
    // Reading through rdbuf() avoids GCC 13's -Wnull-dereference false positive in istreambuf_iterator.
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    if (in)
    {
        text << in.rdbuf();
    }
    return text.str();
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

// The OUTPUT_NAME of the game an export's CMakeLists.txt builds, or "" for other projects.
std::string gameName(const std::filesystem::path& source)
{
    const std::string text = readFile(source / "CMakeLists.txt");
    const std::string key = "OUTPUT_NAME [=[";
    const std::size_t begin = text.find(key);
    const std::size_t end = begin == std::string::npos ? begin : text.find("]=]", begin);
    return end == std::string::npos ? std::string()
                                    : text.substr(begin + key.size(), end - begin - key.size());
}

// The game's file below the build's game directory or the install prefix.
std::filesystem::path gameFile(const std::string& game)
{
#if defined(__APPLE__)
    return std::filesystem::path(game + ".app") / "Contents" / "MacOS" / (game + kExecutableSuffix);
#else
    return game + kExecutableSuffix;
#endif
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
    const char* environment = std::getenv("ILLUSION_TEST_ENVIRONMENT");
    std::ofstream(build / "configure-env.txt", std::ios::binary)
        << (environment != nullptr ? environment : "");
    if (std::filesystem::exists(source / "FAIL_CONFIGURE"))
    {
        std::ofstream(build / "CMakeCache.txt", std::ios::binary) << "SOURCE=\n";
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
    if (!output.empty())
    {
        std::filesystem::create_directories(output);
    }
    for (const std::string& module : modules)
    {
        std::ofstream(output / (kPrefix + module + kSuffix), std::ios::binary) << "fake library\n";
    }
    const std::string game = gameName(source);
    if (!game.empty())
    {
        const std::filesystem::path built = tree / "game" / gameFile(game);
        std::filesystem::create_directories(built.parent_path());
        std::ofstream(built, std::ios::binary) << "fake game\n";
    }
    std::cout << "[2/2] Linking CXX shared module\n";
    return 0;
}

// The install of an exported game: the game of <build>/game and <source>/data/* in its data directory.
int install(const std::vector<std::string>& args)
{
    const std::filesystem::path tree = args.at(1);
    std::filesystem::path prefix;
    for (std::size_t i = 2; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--prefix")
        {
            prefix = args[i + 1];
        }
    }
    const std::filesystem::path source = valueOf(readFile(tree / "CMakeCache.txt"), "SOURCE");
    const std::string game = source.empty() ? std::string() : gameName(source);
    if (prefix.empty() || game.empty())
    {
        std::cerr << "CMake Error: not a build tree of a game\n";
        return 1;
    }
#if defined(__APPLE__)
    const std::filesystem::path data = prefix / (game + ".app") / "Contents" / "Resources" / "data";
#else
    const std::filesystem::path data = prefix / "data";
#endif
    std::filesystem::create_directories(data);
    for (const auto& entry : std::filesystem::directory_iterator(source / "data"))
    {
        std::filesystem::copy_file(entry.path(), data / entry.path().filename(),
                                   std::filesystem::copy_options::overwrite_existing);
    }
    const std::filesystem::path installed = prefix / gameFile(game);
    std::filesystem::create_directories(installed.parent_path());
    std::filesystem::copy_file(tree / "game" / gameFile(game), installed,
                               std::filesystem::copy_options::overwrite_existing);
    std::cout << "-- Installing: " << installed.generic_string() << "\n";
    return 0;
}

int sign(const std::vector<std::string>& args)
{
    const std::filesystem::path file = args.at(2);
    if (!std::filesystem::is_regular_file(file))
    {
        std::cerr << "SignTool Error: File not found: " << file.generic_string() << "\n";
        return 1;
    }
    std::ofstream(args.at(1), std::ios::binary | std::ios::app) << file.generic_string() << '\n';
    std::ofstream(file, std::ios::binary | std::ios::app) << kSignature;
    std::cout << "Successfully signed: " << file.filename().generic_string() << "\n";
    return 0;
}

int cpack(const std::vector<std::string>& args)
{
    std::filesystem::path config;
    std::filesystem::path directory;
    std::string generator;
    std::string name;
    for (std::size_t i = 0; i + 1 < args.size(); ++i)
    {
        if (args[i] == "--config")
        {
            config = args[i + 1];
        }
        else if (args[i] == "-B")
        {
            directory = args[i + 1];
        }
        else if (args[i] == "-G")
        {
            generator = args[i + 1];
        }
        else if (args[i] == "-D" && args[i + 1].rfind("CPACK_PACKAGE_FILE_NAME=", 0) == 0)
        {
            name = args[i + 1].substr(std::string("CPACK_PACKAGE_FILE_NAME=").size());
        }
    }
    const std::filesystem::path tree = config.parent_path();
    const std::filesystem::path source = valueOf(readFile(tree / "CMakeCache.txt"), "SOURCE");
    const std::string game = source.empty() ? std::string() : gameName(source);
    const std::string extension = generator == "ZIP"         ? ".zip"
                                  : generator == "NSIS"      ? ".exe"
                                  : generator == "DragNDrop" ? ".dmg"
                                  : generator == "TGZ"       ? ".tar.gz"
                                                             : "";
    if (game.empty() || name.empty() || extension.empty() || directory.empty())
    {
        std::cerr << "CPack Error: unexpected arguments\n";
        return 1;
    }
    std::filesystem::create_directories(directory);
    std::ofstream recorded(directory / "cpack-args.txt", std::ios::binary);
    for (const std::string& arg : args)
    {
        recorded << arg << '\n';
    }
    std::ofstream(directory / (name + extension), std::ios::binary)
        << generator << " package\n"
        << readFile(tree / "game" / gameFile(game));
    std::cout << "CPack: - package: " << (directory / (name + extension)).generic_string() << " generated.\n";
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
    if (args.size() == 3 && args[0] == "--sign")
    {
        return sign(args);
    }
    if (args.size() >= 2 && args[0] == "--config")
    {
        return cpack(args);
    }
    if (args.size() >= 2 && args[0] == "--install")
    {
        return install(args);
    }
    if (args.size() >= 2 && args[0] == "--build")
    {
        return build(args);
    }
    std::cerr << "fake cmake: unexpected arguments\n";
    return 2;
}
