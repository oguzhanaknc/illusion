// Builds of a project's game modules through CMake child processes (ADR 0043).

#include "PathText.hpp"
#include "ToolProcess.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/editor/core/ModuleBuild.hpp>
#include <illusion/platform/Process.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace illusion::editor
{

using detail::commandText;
using detail::drainLines;
using detail::outputDrained;
using detail::pathFromUtf8;
using detail::ToolOutput;

namespace
{

// Longer lines are kept in the output but not parsed for diagnostics.
constexpr usize kMaxParsedLine = 4096;

std::optional<std::string> readField(JsonValue root, std::string_view name, std::string& error)
{
    const std::optional<std::string_view> value = root.find(name).asString();
    if (!value)
    {
        error = "\"" + std::string(name) + "\" must be a string";
        return std::nullopt;
    }
    return std::string(*value);
}

std::optional<u32> parseNumber(std::string_view text) noexcept
{
    u32 value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || text.empty())
    {
        return std::nullopt;
    }
    return value;
}

std::string_view trimRight(std::string_view text) noexcept
{
    while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
    {
        text.remove_suffix(1);
    }
    return text;
}

// "fatal error", "error", "warning" or "note" at the start of @p text, followed by @p terminator.
std::optional<DiagnosticSeverity> severityPrefix(std::string_view& text) noexcept
{
    constexpr std::array<std::pair<std::string_view, DiagnosticSeverity>, 4> kWords{{
        {"fatal error", DiagnosticSeverity::Error},
        {"error", DiagnosticSeverity::Error},
        {"warning", DiagnosticSeverity::Warning},
        {"note", DiagnosticSeverity::Note},
    }};
    for (const auto& [word, severity] : kWords)
    {
        if (text.starts_with(word))
        {
            text.remove_prefix(word.size());
            return severity;
        }
    }
    return std::nullopt;
}

// A trailing " [-Wflag]" of GCC and Clang becomes the code.
void splitFlag(BuildDiagnostic& diagnostic)
{
    std::string_view message = diagnostic.message;
    if (message.ends_with(']'))
    {
        const usize open = message.rfind(" [-W");
        if (open != std::string_view::npos)
        {
            diagnostic.code = std::string(message.substr(open + 2, message.size() - open - 3));
            diagnostic.message.resize(open);
        }
    }
}

// MSVC and clang-cl: "file(line[,col]): error C1234: message" or "file(line[,col]): error: message".
std::optional<BuildDiagnostic> parseMsvc(std::string_view line)
{
    const usize marker = line.find("): ");
    if (marker == std::string_view::npos)
    {
        return std::nullopt;
    }
    const usize open = line.rfind('(', marker);
    if (open == std::string_view::npos || open == 0)
    {
        return std::nullopt;
    }
    const std::string_view location = line.substr(open + 1, marker - open - 1);
    const usize comma = location.find(',');
    const std::optional<u32> number = parseNumber(location.substr(0, comma));
    std::optional<u32> column =
        comma == std::string_view::npos ? std::optional<u32>(0) : parseNumber(location.substr(comma + 1));
    if (!number || !column)
    {
        return std::nullopt;
    }
    std::string_view rest = line.substr(marker + 3);
    const std::optional<DiagnosticSeverity> severity = severityPrefix(rest);
    if (!severity)
    {
        return std::nullopt;
    }
    BuildDiagnostic diagnostic{
        .file = std::string(line.substr(0, open)), .line = *number, .column = *column, .severity = *severity};
    if (rest.starts_with(' '))
    {
        const usize colon = rest.find(": ");
        if (colon == std::string_view::npos)
        {
            return std::nullopt;
        }
        diagnostic.code = std::string(rest.substr(1, colon - 1));
        rest.remove_prefix(colon);
    }
    if (!rest.starts_with(": "))
    {
        return std::nullopt;
    }
    diagnostic.message = std::string(rest.substr(2));
    splitFlag(diagnostic);
    return diagnostic;
}

// GCC and Clang: "file:line[:col]: error: message". The file may contain colons (drive letters).
std::optional<BuildDiagnostic> parseGnu(std::string_view line)
{
    for (usize colon = line.find(':'); colon != std::string_view::npos; colon = line.find(':', colon + 1))
    {
        if (colon == 0)
        {
            continue;
        }
        std::string_view rest = line.substr(colon + 1);
        const usize lineEnd = rest.find(':');
        if (lineEnd == std::string_view::npos)
        {
            return std::nullopt;
        }
        const std::optional<u32> number = parseNumber(rest.substr(0, lineEnd));
        if (!number)
        {
            continue;
        }
        rest.remove_prefix(lineEnd + 1);
        u32 column = 0;
        if (const usize columnEnd = rest.find(':'); columnEnd != std::string_view::npos)
        {
            if (const std::optional<u32> parsed = parseNumber(rest.substr(0, columnEnd)))
            {
                column = *parsed;
                rest.remove_prefix(columnEnd + 1);
            }
        }
        if (!rest.starts_with(' '))
        {
            continue;
        }
        rest.remove_prefix(1);
        const std::optional<DiagnosticSeverity> severity = severityPrefix(rest);
        if (!severity || !rest.starts_with(": "))
        {
            continue;
        }
        BuildDiagnostic diagnostic{.file = std::string(line.substr(0, colon)),
                                   .line = *number,
                                   .column = column,
                                   .severity = *severity,
                                   .message = std::string(rest.substr(2))};
        splitFlag(diagnostic);
        return diagnostic;
    }
    return std::nullopt;
}

// MSVC linkers: "object : error LNK2019: message" and "LINK : fatal error LNK1104: message".
std::optional<BuildDiagnostic> parseLinker(std::string_view line)
{
    const usize marker = line.find(" : ");
    if (marker == std::string_view::npos)
    {
        return std::nullopt;
    }
    std::string_view rest = line.substr(marker + 3);
    const std::optional<DiagnosticSeverity> severity = severityPrefix(rest);
    if (!severity || !rest.starts_with(' '))
    {
        return std::nullopt;
    }
    const usize colon = rest.find(": ");
    if (colon == std::string_view::npos || colon < 2)
    {
        return std::nullopt;
    }
    return BuildDiagnostic{.file = std::string(line.substr(0, marker)),
                           .severity = *severity,
                           .code = std::string(rest.substr(1, colon - 1)),
                           .message = std::string(rest.substr(colon + 2))};
}

// CMake: "CMake Error at file:line (command):", "CMake Warning (dev) at ...:" or "CMake Error: message".
std::optional<BuildDiagnostic> parseCMake(std::string_view line)
{
    if (!line.starts_with("CMake "))
    {
        return std::nullopt;
    }
    std::string_view rest = line.substr(6);
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    if (rest.starts_with("Error"))
    {
        rest.remove_prefix(5);
    }
    else if (rest.starts_with("Warning"))
    {
        severity = DiagnosticSeverity::Warning;
        rest.remove_prefix(7);
        if (rest.starts_with(" (dev)"))
        {
            rest.remove_prefix(6);
        }
    }
    else
    {
        return std::nullopt;
    }
    BuildDiagnostic diagnostic{.severity = severity};
    if (rest.starts_with(": "))
    {
        diagnostic.message = std::string(rest.substr(2));
        return diagnostic;
    }
    if (!rest.starts_with(" at ") || !rest.ends_with(':'))
    {
        return std::nullopt;
    }
    rest = rest.substr(4, rest.size() - 5);
    if (const usize command = rest.rfind(" ("); command != std::string_view::npos && rest.ends_with(')'))
    {
        rest = rest.substr(0, command);
    }
    const usize colon = rest.rfind(':');
    if (colon == std::string_view::npos)
    {
        return std::nullopt;
    }
    const std::optional<u32> number = parseNumber(rest.substr(colon + 1));
    if (!number)
    {
        return std::nullopt;
    }
    diagnostic.file = std::string(rest.substr(0, colon));
    diagnostic.line = *number;
    return diagnostic;
}

using Environment = std::vector<std::pair<std::string, std::string>>;

// The environment each toolchain script set up, once per process and script: running vcvars takes a second or
// two.
std::mutex g_environmentMutex;
std::vector<std::pair<std::filesystem::path, Environment>> g_environments;

const Environment* cachedEnvironment(const std::filesystem::path& script)
{
    const auto found = std::find_if(g_environments.begin(), g_environments.end(),
                                    [&](const auto& entry) { return entry.first == script; });
    return found == g_environments.end() ? nullptr : &found->second;
}

// True when this process already runs inside a Visual Studio developer environment.
bool insideDeveloperEnvironment()
{
    const std::optional<std::string> installation = environmentVariable("VCINSTALLDIR");
    return installation.has_value() && !installation->empty();
}

// The command that runs @p script and prints the environment it leaves, as `NAME=value` lines in UTF-8.
Expected<ProcessOptions, std::string> environmentCommand(const std::filesystem::path& script,
                                                         const std::filesystem::path& buildDirectory)
{
#if defined(ILL_PLATFORM_WINDOWS)
    // cmd.exe's quoting rules differ from CommandLineToArgvW's, so the script is called from a batch file
    // instead of a /c command line. chcp 65001 makes `set` print UTF-8.
    std::error_code code;
    std::filesystem::create_directories(buildDirectory, code);
    const std::filesystem::path batch = buildDirectory / "illusion-environment.cmd";
    std::ofstream out(batch, std::ios::binary | std::ios::trunc);
    out << "@echo off\r\nchcp 65001 >nul\r\ncall \"" << detail::pathText(script)
        << "\" >nul\r\nif errorlevel 1 exit /b 1\r\nset\r\n";
    if (!out.good())
    {
        return unexpected("cannot write " + detail::pathText(batch));
    }
    const std::optional<std::string> shell = environmentVariable("ComSpec");
    return ProcessOptions{.executable = pathFromUtf8(shell && !shell->empty() ? *shell : "cmd.exe"),
                          .arguments = {"/d", "/c", detail::pathText(batch)}};
#else
    static_cast<void>(buildDirectory);
    return unexpected("the toolchain's environment script " + detail::pathText(script) +
                      " can only run on Windows");
#endif
}

// The `NAME=value` lines of `set`; cmd's hidden per-drive variables (`=C:=C:\`) are left out.
void addEnvironmentLine(std::string_view line, Environment& environment)
{
    if (!line.empty() && line.back() == '\r')
    {
        line.remove_suffix(1);
    }
    const usize equals = line.find('=');
    if (equals == std::string_view::npos || equals == 0)
    {
        return;
    }
    environment.emplace_back(std::string(line.substr(0, equals)), std::string(line.substr(equals + 1)));
}

// A path of an SDK file, relative to the directory of its description.
std::filesystem::path sdkPath(const std::filesystem::path& file, const std::filesystem::path& path)
{
    if (path.empty() || !path.is_relative())
    {
        return path;
    }
    std::filesystem::path resolved = (file.parent_path() / path).lexically_normal();
    // "a/b/.." normalizes to "a/".
    if (resolved.filename().empty() && resolved.has_relative_path())
    {
        resolved = resolved.parent_path();
    }
    return resolved;
}

} // namespace

Expected<ModuleToolchain, std::string> parseModuleToolchain(std::string_view json)
{
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(json);
    if (!parsed)
    {
        return unexpected("invalid JSON at line " + std::to_string(parsed.error().line) + ": " +
                          parsed.error().message);
    }
    const JsonValue root = parsed->root();
    if (!root.isObject() || root.find("format").asString() != kModuleSdkFormat)
    {
        return unexpected("not a module SDK description (\"format\" must be \"" +
                          std::string(kModuleSdkFormat) + "\")");
    }
    const std::optional<u64> version = root.find("version").asU64();
    if (version != 1u && version != 2u)
    {
        return unexpected(std::string("unsupported module SDK description version (expected 1 or 2)"));
    }
    // Version 2 says whether the package has the player component (ADR 0055); version 1 never has it.
    const JsonValue player = root.find("player");
    if (version == 2u && !player.isBool())
    {
        return unexpected(std::string("\"player\" must be true or false"));
    }
    std::string error;
    const std::optional<std::string> cmake = readField(root, "cmake", error);
    const std::optional<std::string> generator = readField(root, "generator", error);
    const std::optional<std::string> config = readField(root, "config", error);
    const std::optional<std::string> package = readField(root, "package", error);
    const std::optional<std::string> prefix = readField(root, "modulePrefix", error);
    const std::optional<std::string> suffix = readField(root, "moduleSuffix", error);
    if (!cmake || !generator || !config || !package || !prefix || !suffix)
    {
        return unexpected(error);
    }
    if (cmake->empty() || generator->empty() || config->empty() || package->empty() || suffix->empty())
    {
        return unexpected(
            std::string("\"cmake\", \"generator\", \"config\", \"package\" and \"moduleSuffix\" "
                        "must not be empty"));
    }
    const JsonValue script = root.find("environmentScript");
    if (!script.isAbsent() && !script.isString())
    {
        return unexpected(std::string("\"environmentScript\" must be a string"));
    }
    const JsonValue pack = root.find("pack");
    if (!pack.isAbsent() && !pack.isString())
    {
        return unexpected(std::string("\"pack\" must be a string"));
    }
    ModuleToolchain toolchain{.cmake = pathFromUtf8(*cmake),
                              .generator = *generator,
                              .config = *config,
                              .package = pathFromUtf8(*package),
                              .modulePrefix = *prefix,
                              .moduleSuffix = *suffix,
                              .environmentScript = pathFromUtf8(script.asString().value_or("")),
                              .player = version == 2u && player.asBool() == true,
                              .pack = pathFromUtf8(pack.asString().value_or(""))};
    const JsonValue arguments = root.find("arguments");
    if (!arguments.isArray())
    {
        return unexpected(std::string("\"arguments\" must be an array of strings"));
    }
    for (const JsonValue argument : arguments.elements())
    {
        const std::optional<std::string_view> text = argument.asString();
        if (!text)
        {
            return unexpected(std::string("\"arguments\" must be an array of strings"));
        }
        toolchain.arguments.emplace_back(*text);
    }
    return toolchain;
}

Expected<ModuleToolchain, std::string> readModuleToolchain(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return unexpected("cannot read " + detail::pathText(file));
    }
    std::ostringstream text;
    text << in.rdbuf();
    Expected<ModuleToolchain, std::string> toolchain = parseModuleToolchain(text.str());
    if (!toolchain)
    {
        return unexpected(detail::pathText(file) + ": " + toolchain.error());
    }
    // An installed SDK names its files relative to this one, so that it can move.
    toolchain->package = sdkPath(file, toolchain->package);
    toolchain->pack = sdkPath(file, toolchain->pack);
    return toolchain;
}

std::string_view diagnosticSeverityName(DiagnosticSeverity severity) noexcept
{
    switch (severity)
    {
    case DiagnosticSeverity::Error:
        return "error";
    case DiagnosticSeverity::Warning:
        return "warning";
    case DiagnosticSeverity::Note:
        return "note";
    }
    return "error";
}

void BuildOutputParser::feed(std::string_view line, std::vector<BuildDiagnostic>& out)
{
    line = trimRight(line);
    if (line.size() > kMaxParsedLine)
    {
        m_inCMakeMessage = false;
        return;
    }
    if (m_inCMakeMessage && !out.empty())
    {
        if (line.starts_with("  "))
        {
            const usize start = line.find_first_not_of(' ');
            std::string& message = out.back().message;
            message += message.empty() ? "" : " ";
            message += line.substr(start);
            return;
        }
        // A blank line before the indented message keeps it open; anything else ends it.
        if (line.empty() && out.back().message.empty())
        {
            return;
        }
        m_inCMakeMessage = false;
    }
    if (std::optional<BuildDiagnostic> cmake = parseCMake(line))
    {
        m_inCMakeMessage = cmake->message.empty();
        out.push_back(std::move(*cmake));
        return;
    }
    if (std::optional<BuildDiagnostic> msvc = parseMsvc(line))
    {
        out.push_back(std::move(*msvc));
        return;
    }
    if (std::optional<BuildDiagnostic> gnu = parseGnu(line))
    {
        out.push_back(std::move(*gnu));
        return;
    }
    if (std::optional<BuildDiagnostic> linker = parseLinker(line))
    {
        out.push_back(std::move(*linker));
    }
}

std::optional<BuildProgress> parseBuildProgress(std::string_view line) noexcept
{
    if (!line.starts_with('['))
    {
        return std::nullopt;
    }
    const usize close = line.find(']');
    if (close == std::string_view::npos)
    {
        return std::nullopt;
    }
    std::string_view inside = line.substr(1, close - 1);
    if (const usize slash = inside.find('/'); slash != std::string_view::npos)
    {
        const std::optional<u32> done = parseNumber(inside.substr(0, slash));
        const std::optional<u32> total = parseNumber(inside.substr(slash + 1));
        if (done && total && *total > 0)
        {
            return BuildProgress{static_cast<f64>(*done), static_cast<f64>(*total)};
        }
        return std::nullopt;
    }
    if (inside.ends_with('%'))
    {
        inside.remove_suffix(1);
        inside.remove_prefix(std::min(inside.find_first_not_of(' '), inside.size()));
        if (const std::optional<u32> percent = parseNumber(inside); percent && *percent <= 100)
        {
            return BuildProgress{static_cast<f64>(*percent), 100.0};
        }
    }
    return std::nullopt;
}

struct ModuleBuild::Impl
{
    ModuleBuildOptions options;
    Stage stage = Stage::Configure;
    bool needsConfigure = false;
    bool configured = false;
    bool succeeded = false;
    Environment environment;
    Process process;
    ToolOutput output;
    BuildProgress progress;
    std::string line;

    std::vector<std::string> configureArguments() const
    {
        std::vector<std::string> arguments{"-S", detail::pathText(options.sourceDirectory),
                                           "-B", detail::pathText(options.buildDirectory),
                                           "-G", options.toolchain.generator};
        arguments.insert(arguments.end(), options.toolchain.arguments.begin(),
                         options.toolchain.arguments.end());
        arguments.push_back("-Dillusion_DIR=" + detail::pathText(options.toolchain.package));
        if (!options.outputDirectory.empty())
        {
            arguments.push_back("-DILLUSION_MODULE_OUTPUT_DIRECTORY=" +
                                detail::pathText(options.outputDirectory.generic_u8string()));
        }
        return arguments;
    }

    std::vector<std::string> buildArguments() const
    {
        std::vector<std::string> arguments{"--build", detail::pathText(options.buildDirectory), "--config",
                                           options.toolchain.config};
        if (!options.targets.empty())
        {
            arguments.push_back("--target");
            arguments.insert(arguments.end(), options.targets.begin(), options.targets.end());
        }
        return arguments;
    }

    // What a successful configure records in the build tree: the toolchain that configured it.
    std::string configureStamp() const
    {
        std::string text = detail::pathText(options.toolchain.cmake) + "\n";
        for (const std::string& argument : configureArguments())
        {
            text += argument + "\n";
        }
        return text;
    }

    std::filesystem::path stampPath() const { return options.buildDirectory / "illusion-configured.txt"; }

    // Whether the build tree was configured, successfully, with this toolchain.
    bool upToDate() const
    {
        std::ifstream in(stampPath(), std::ios::binary);
        std::ostringstream text;
        if (in)
        {
            text << in.rdbuf();
        }
        return in && text.str() == configureStamp();
    }

    // Starts the configure step when the tree needs one, otherwise the build step.
    Expected<void, std::string> begin()
    {
        if (!needsConfigure)
        {
            stage = Stage::Build;
            return spawn(buildArguments());
        }
        // A configure that fails or is interrupted leaves a cache behind; without the stamp and the cache the
        // next build configures again, and a toolchain change starts from a clean cache.
        std::error_code code;
        std::filesystem::remove(stampPath(), code);
        std::filesystem::remove(options.buildDirectory / "CMakeCache.txt", code);
        stage = Stage::Configure;
        configured = true;
        return spawn(configureArguments());
    }

    void writeStamp() { std::ofstream(stampPath(), std::ios::binary | std::ios::trunc) << configureStamp(); }

    Expected<void, std::string> spawn(std::vector<std::string> arguments)
    {
        addLine("> cmake " + commandText(arguments));
        ProcessOptions processOptions{.executable = options.toolchain.cmake,
                                      .arguments = std::move(arguments),
                                      .environment = environment};
        Expected<Process, std::string> started = Process::spawn(processOptions);
        if (!started)
        {
            return unexpected("cannot start " + detail::pathText(options.toolchain.cmake) + ": " +
                              started.error());
        }
        process = std::move(*started);
        return {};
    }

    void addLine(std::string text)
    {
        if (stage == Stage::Environment)
        {
            // Environment values stay out of the output: they may hold tokens.
            addEnvironmentLine(text, environment);
            return;
        }
        if (stage == Stage::Build)
        {
            if (const std::optional<BuildProgress> reported = parseBuildProgress(text))
            {
                progress = *reported;
            }
        }
        output.add(std::move(text));
    }

    void readLines()
    {
        drainLines(process, line, [this](std::string text) { addLine(std::move(text)); });
    }

    bool finished() const { return outputDrained(process); }
};

ModuleBuild::ModuleBuild()
    : m_impl(std::make_unique<Impl>())
{
}

ModuleBuild::~ModuleBuild()
{
    if (m_impl->process.valid() && !m_impl->process.poll())
    {
        static_cast<void>(m_impl->process.terminate());
        static_cast<void>(m_impl->process.wait());
    }
}

Expected<std::unique_ptr<ModuleBuild>, std::string> ModuleBuild::start(ModuleBuildOptions options)
{
    std::unique_ptr<ModuleBuild> build(new ModuleBuild());
    Impl& impl = *build->m_impl;
    impl.options = std::move(options);
    impl.needsConfigure = impl.options.reconfigure || !impl.upToDate();
    const std::filesystem::path& script = impl.options.toolchain.environmentScript;
    if (!script.empty() && !insideDeveloperEnvironment())
    {
        std::lock_guard lock(g_environmentMutex);
        const Environment* cached = cachedEnvironment(script);
        if (cached == nullptr)
        {
            Expected<ProcessOptions, std::string> command =
                environmentCommand(script, impl.options.buildDirectory);
            if (!command)
            {
                return unexpected(std::move(command).error());
            }
            impl.output.addUnparsed("> " + detail::pathText(script));
            Expected<Process, std::string> started = Process::spawn(*command);
            if (!started)
            {
                return unexpected("cannot run " + detail::pathText(script) + ": " + started.error());
            }
            impl.process = std::move(*started);
            impl.stage = Stage::Environment;
            return build;
        }
        impl.environment = *cached;
    }
    if (Expected<void, std::string> spawned = impl.begin(); !spawned)
    {
        return unexpected(std::move(spawned).error());
    }
    return build;
}

bool ModuleBuild::update()
{
    Impl& impl = *m_impl;
    if (impl.stage == Stage::Done)
    {
        return true;
    }
    impl.readLines();
    const std::optional<i32> exitCode = impl.process.poll();
    if (!exitCode || !impl.finished())
    {
        return false;
    }
    impl.readLines();
    if (*exitCode != 0)
    {
        const bool environment = impl.stage == Stage::Environment;
        impl.stage = Stage::Done;
        impl.addLine(environment ? detail::pathText(impl.options.toolchain.environmentScript) +
                                       " exited with code " + std::to_string(*exitCode)
                                 : "cmake exited with code " + std::to_string(*exitCode));
        return true;
    }
    if (impl.stage == Stage::Environment)
    {
        {
            std::lock_guard lock(g_environmentMutex);
            if (cachedEnvironment(impl.options.toolchain.environmentScript) == nullptr)
            {
                g_environments.emplace_back(impl.options.toolchain.environmentScript, impl.environment);
            }
        }
        if (Expected<void, std::string> spawned = impl.begin(); !spawned)
        {
            impl.stage = Stage::Done;
            impl.addLine(std::move(spawned).error());
        }
        return impl.stage == Stage::Done;
    }
    if (impl.stage == Stage::Configure)
    {
        impl.writeStamp();
        impl.stage = Stage::Build;
        if (Expected<void, std::string> spawned = impl.spawn(impl.buildArguments()); !spawned)
        {
            impl.stage = Stage::Done;
            impl.addLine(std::move(spawned).error());
        }
        return impl.stage == Stage::Done;
    }
    impl.succeeded = true;
    impl.stage = Stage::Done;
    return true;
}

ModuleBuild::Stage ModuleBuild::stage() const noexcept
{
    return m_impl->stage;
}

bool ModuleBuild::configured() const noexcept
{
    return m_impl->configured;
}

bool ModuleBuild::succeeded() const noexcept
{
    return m_impl->succeeded;
}

const std::vector<BuildDiagnostic>& ModuleBuild::diagnostics() const noexcept
{
    return m_impl->output.diagnostics;
}

BuildProgress ModuleBuild::progress() const noexcept
{
    return m_impl->progress;
}

std::string ModuleBuild::outputTail() const
{
    return m_impl->output.tail();
}

std::vector<std::filesystem::path> ModuleBuild::libraries() const
{
    const ModuleBuildOptions& options = m_impl->options;
    std::vector<std::filesystem::path> found;
    std::error_code code;
    if (!options.targets.empty())
    {
        for (const std::string& target : options.targets)
        {
            std::filesystem::path file =
                options.outputDirectory /
                pathFromUtf8(options.toolchain.modulePrefix + target + options.toolchain.moduleSuffix);
            if (std::filesystem::is_regular_file(file, code))
            {
                found.push_back(std::move(file));
            }
        }
    }
    else
    {
        for (std::filesystem::directory_iterator it(options.outputDirectory, code), end; !code && it != end;
             it.increment(code))
        {
            const std::string name = detail::pathText(it->path().filename());
            if (it->is_regular_file(code) && name.starts_with(options.toolchain.modulePrefix) &&
                name.ends_with(options.toolchain.moduleSuffix) &&
                name.size() > options.toolchain.modulePrefix.size() + options.toolchain.moduleSuffix.size())
            {
                found.push_back(it->path());
            }
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

} // namespace illusion::editor
