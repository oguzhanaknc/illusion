#include <illusion/platform/Dialog.hpp>
#include <illusion/platform/FileSystem.hpp>
#include <illusion/platform/Process.hpp>

#include <string>

#if defined(ILL_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(ILL_PLATFORM_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace illusion
{

#if defined(ILL_PLATFORM_MACOS)
namespace
{

CFStringRef cfString(std::string_view text)
{
    return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(text.data()),
                                   static_cast<CFIndex>(text.size()), kCFStringEncodingUTF8, false);
}

} // namespace
#endif

bool showErrorDialog(std::string_view title, std::string_view message)
{
#if defined(ILL_PLATFORM_WINDOWS)
    // utf8Path() converts UTF-8 to the UTF-16 the wide API takes.
    const std::wstring wideTitle = utf8Path(title).wstring();
    const std::wstring wideMessage = utf8Path(message).wstring();
    return MessageBoxW(nullptr, wideMessage.c_str(), wideTitle.c_str(),
                       MB_OK | MB_ICONERROR | MB_SETFOREGROUND) != 0;
#elif defined(ILL_PLATFORM_MACOS)
    const CFStringRef header = cfString(title);
    const CFStringRef text = cfString(message);
    if (header == nullptr || text == nullptr)
    {
        if (header != nullptr)
        {
            CFRelease(header);
        }
        if (text != nullptr)
        {
            CFRelease(text);
        }
        return false;
    }
    CFOptionFlags response = 0;
    const SInt32 shown =
        CFUserNotificationDisplayAlert(0.0, kCFUserNotificationStopAlertLevel, nullptr, nullptr, nullptr,
                                       header, text, nullptr, nullptr, nullptr, &response);
    CFRelease(header);
    CFRelease(text);
    return shown == 0;
#else
    ProcessOptions options;
    options.captureOutput = false;
    options.captureError = false;
    if (std::optional<std::filesystem::path> zenity = findExecutable("zenity"))
    {
        options.executable = std::move(*zenity);
        options.arguments = {"--error",          "--no-markup", "--title",
                             std::string(title), "--text",      std::string(message)};
    }
    else if (std::optional<std::filesystem::path> kdialog = findExecutable("kdialog"))
    {
        options.executable = std::move(*kdialog);
        options.arguments = {"--title", std::string(title), "--error", std::string(message)};
    }
    else
    {
        return false;
    }
    Expected<Process, std::string> process = Process::spawn(options);
    return process.hasValue() && process->wait().has_value();
#endif
}

} // namespace illusion
