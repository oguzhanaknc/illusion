#pragma once

/// @file
/// The crash journal: an append-only log of every applied change, flushed every frame, from which a
/// crashed session's unsaved work is recovered.

#include <illusion/core/Expected.hpp>
#include <illusion/editor/core/Operation.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace illusion::editor
{

/// Writes `.illusion/journal/<session>.jsonl`: one JSON object per line.
///
/// | Line | Meaning |
/// | --- | --- |
/// | `{"type":"session","session":"...","format":1}` | First line |
/// | `{"type":"base","scene":"assets:/scenes/main.scene"}` | A scene was opened (`null`: a new scene) |
/// | `{"type":"ops","label":"...","ops":[...]}` | Operations applied to it (commits, undos, redos) |
/// | `{"type":"saved","scene":"..."}` | The scene was saved there: earlier lines are not needed |
///
/// A clean shutdown deletes the file, so a journal left behind belongs to a session that crashed.
class CrashJournal
{
public:
    CrashJournal() = default;
    ~CrashJournal();
    CrashJournal(const CrashJournal&) = delete;            ///< Not copyable.
    CrashJournal& operator=(const CrashJournal&) = delete; ///< Not copyable.

    /// Creates @p file (and its directory) and writes the session line. Returns false on I/O errors.
    bool open(const std::filesystem::path& file, std::string_view session);

    /// True while a file is open.
    [[nodiscard]] bool isOpen() const noexcept { return m_file.is_open(); }

    /// The journal file.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return m_path; }

    /// Records that @p scene was opened ("" for a new scene). @p content is the scene as it was loaded, for
    /// scenes whose file does not hold everything (ids were assigned on loading); empty to use the file.
    void writeBase(std::string_view scene, std::string_view content = {});

    /// Records applied operations, in the order they were applied.
    void writeOperations(std::string_view label, std::span<const Operation> operations);

    /// Records that the scene was saved to @p scene.
    void writeSaved(std::string_view scene);

    /// Writes buffered lines to the file (once per frame).
    void flush();

    /// Closes the file and deletes it.
    void closeClean();

private:
    void writeLine(const std::string& line);

    std::filesystem::path m_path;
    std::ofstream m_file;
};

/// What readJournal() found: the scene to start from and the operations to apply to it.
struct JournalReplay
{
    std::string session;                  ///< Session id of the journal.
    std::optional<std::string> scene{};   ///< Scene file to open first (nullopt: start from a new scene).
    std::optional<std::string> content{}; ///< The scene's contents, when the journal holds them.
    std::vector<Operation> operations;    ///< Operations after the last base or save, in order.
    u32 records = 0;                      ///< Number of "ops" lines among them.
    bool truncated = false;               ///< The last line was incomplete (the crash cut it) and is ignored.
};

/// Reads a journal written by CrashJournal.
[[nodiscard]] Expected<JournalReplay, std::string> readJournal(const std::filesystem::path& file);

} // namespace illusion::editor
