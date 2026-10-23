// Child program for tests/platform/TestProcess.cpp. The first argument picks what it does:
//
//     args <a>...    prints each further argument as hex-encoded UTF-8, one per line
//     cwd            prints the working directory as hex-encoded UTF-8
//     env <name>     prints the variable's value, or "<unset>"
//     exit <code>    exits with <code>
//     cat            copies stdin to stdout until end of file
//     spam <bytes>   writes <bytes> bytes of lines "abc...xyz" to stdout, then "done" to stderr
//     hang           never exits by itself

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{

std::string hex(const std::string& text)
{
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const char c : text)
    {
        const auto byte = static_cast<unsigned char>(c);
        out += kDigits[byte >> 4];
        out += kDigits[byte & 15];
    }
    return out;
}

void writeLine(const std::string& text)
{
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
}

int run(const std::vector<std::string>& args)
{
    if (args.empty())
    {
        return 2;
    }
    const std::string& mode = args[0];
    if (mode == "args")
    {
        for (std::size_t i = 1; i < args.size(); ++i)
        {
            writeLine(hex(args[i]));
        }
    }
    else if (mode == "cwd")
    {
        const std::u8string path = std::filesystem::current_path().u8string();
        writeLine(hex(std::string(reinterpret_cast<const char*>(path.data()), path.size())));
    }
    else if (mode == "env" && args.size() == 2)
    {
#if defined(_WIN32)
        // getenv() would convert through the ANSI code page; read the UTF-16 value instead.
        const std::wstring name(args[1].begin(), args[1].end());
        std::wstring value(32768, L'\0');
        const DWORD size =
            GetEnvironmentVariableW(name.c_str(), value.data(), static_cast<DWORD>(value.size()));
        if (size == 0)
        {
            writeLine("<unset>");
        }
        else
        {
            value.resize(size);
            std::string utf8(static_cast<std::size_t>(WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                                                          static_cast<int>(value.size()),
                                                                          nullptr, 0, nullptr, nullptr)),
                             '\0');
            WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(),
                                static_cast<int>(utf8.size()), nullptr, nullptr);
            writeLine(utf8);
        }
#else
        const char* value = std::getenv(args[1].c_str());
        writeLine(value != nullptr ? value : "<unset>");
#endif
    }
    else if (mode == "exit" && args.size() == 2)
    {
        return std::atoi(args[1].c_str());
    }
    else if (mode == "cat")
    {
        // read() returns what has arrived; fread() would wait for a full buffer.
        char buffer[4096];
        for (;;)
        {
#if defined(_WIN32)
            const int count = _read(0, buffer, sizeof(buffer));
#else
            const ssize_t count = read(0, buffer, sizeof(buffer));
#endif
            if (count <= 0)
            {
                break;
            }
            std::fwrite(buffer, 1, static_cast<std::size_t>(count), stdout);
            std::fflush(stdout);
        }
    }
    else if (mode == "spam" && args.size() == 2)
    {
        const std::size_t total = std::strtoull(args[1].c_str(), nullptr, 10);
        const std::string line = "abcdefghijklmnopqrstuvwxyz\n";
        for (std::size_t written = 0; written < total;)
        {
            const std::size_t count = std::min(line.size(), total - written);
            std::fwrite(line.data(), 1, count, stdout);
            written += count;
        }
        std::fflush(stdout);
        std::fputs("done\n", stderr);
    }
    else if (mode == "hang")
    {
        for (;;)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    else
    {
        return 2;
    }
    std::fflush(stdout);
    return 0;
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
{
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
    {
        const std::u8string text = std::filesystem::path(argv[i]).u8string();
        args.emplace_back(reinterpret_cast<const char*>(text.data()), text.size());
    }
    return run(args);
}
#else
int main(int argc, char** argv)
{
    return run(std::vector<std::string>(argv + 1, argv + argc));
}
#endif
