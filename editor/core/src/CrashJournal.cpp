#include "PathText.hpp"

#include <illusion/core/Json.hpp>
#include <illusion/core/Log.hpp>
#include <illusion/editor/core/CrashJournal.hpp>

#include <system_error>

namespace illusion::editor
{
namespace
{

constexpr i64 kJournalFormat = 1;

std::string compact(const JsonMutDocument& doc)
{
    std::string out;
    if (!doc.write(out, JsonWriteOptions{.pretty = false}))
    {
        out.clear();
    }
    return out;
}

} // namespace

CrashJournal::~CrashJournal()
{
    if (m_file.is_open())
    {
        m_file.flush();
    }
}

bool CrashJournal::open(const std::filesystem::path& file, std::string_view session)
{
    if (m_file.is_open())
    {
        m_file.close();
    }
    std::error_code code;
    std::filesystem::create_directories(file.parent_path(), code);
    m_file.open(file, std::ios::binary | std::ios::trunc);
    if (!m_file)
    {
        ILL_LOG_WARN("Editor", "Cannot write the crash journal {}", detail::pathText(file));
        return false;
    }
    m_path = file;
    JsonMutDocument doc;
    JsonMutValue line = doc.makeObject();
    line.add("type", doc.makeString("session"));
    line.add("session", doc.makeString(session));
    line.add("format", doc.makeI64(kJournalFormat));
    doc.setRoot(line);
    writeLine(compact(doc));
    flush();
    return true;
}

void CrashJournal::writeBase(std::string_view scene, std::string_view content)
{
    if (!m_file.is_open())
    {
        return;
    }
    JsonMutDocument doc;
    JsonMutValue line = doc.makeObject();
    line.add("type", doc.makeString("base"));
    line.add("scene", scene.empty() ? doc.makeNull() : doc.makeString(scene));
    if (!content.empty())
    {
        line.add("content", doc.makeString(content));
    }
    doc.setRoot(line);
    writeLine(compact(doc));
}

void CrashJournal::writeOperations(std::string_view label, std::span<const Operation> operations)
{
    if (!m_file.is_open() || operations.empty())
    {
        return;
    }
    JsonMutDocument doc;
    JsonMutValue line = doc.makeObject();
    line.add("type", doc.makeString("ops"));
    line.add("label", doc.makeString(label));
    JsonMutValue ops = line.addArray("ops");
    for (const Operation& operation : operations)
    {
        ops.push(writeOperation(doc, operation));
    }
    doc.setRoot(line);
    writeLine(compact(doc));
}

void CrashJournal::writeSaved(std::string_view scene)
{
    if (!m_file.is_open())
    {
        return;
    }
    JsonMutDocument doc;
    JsonMutValue line = doc.makeObject();
    line.add("type", doc.makeString("saved"));
    line.add("scene", doc.makeString(scene));
    doc.setRoot(line);
    writeLine(compact(doc));
}

void CrashJournal::flush()
{
    if (m_file.is_open())
    {
        m_file.flush();
    }
}

void CrashJournal::closeClean()
{
    if (!m_file.is_open())
    {
        return;
    }
    m_file.close();
    std::error_code code;
    std::filesystem::remove(m_path, code);
    m_path.clear();
}

void CrashJournal::writeLine(const std::string& line)
{
    if (line.empty())
    {
        return;
    }
    m_file << line << '\n';
}

Expected<JournalReplay, std::string> readJournal(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        return unexpected("cannot read " + detail::pathText(file));
    }
    JournalReplay replay;
    std::string line;
    bool first = true;
    while (std::getline(in, line))
    {
        const bool last = in.peek() == std::char_traits<char>::eof();
        if (line.empty())
        {
            continue;
        }
        Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(line);
        if (!parsed)
        {
            if (last)
            {
                replay.truncated = true; // the crash cut the last line
                break;
            }
            return unexpected("damaged journal line: " + parsed.error().message);
        }
        const JsonValue root = parsed->root();
        const std::string_view type = root.find("type").asString().value_or("");
        if (first)
        {
            if (type != "session" || root.find("format").asI64() != kJournalFormat)
            {
                return unexpected(std::string("not an editor journal"));
            }
            replay.session = std::string(root.find("session").asString().value_or(""));
            first = false;
            continue;
        }
        if (type == "base" || type == "saved")
        {
            const std::optional<std::string_view> scene = root.find("scene").asString();
            replay.scene = scene ? std::optional<std::string>(std::string(*scene)) : std::nullopt;
            const std::optional<std::string_view> content = root.find("content").asString();
            replay.content = content ? std::optional<std::string>(std::string(*content)) : std::nullopt;
            replay.operations.clear();
            replay.records = 0;
        }
        else if (type == "ops")
        {
            for (const JsonValue entry : root.find("ops").elements())
            {
                Expected<Operation, std::string> operation = readOperation(entry);
                if (!operation)
                {
                    return unexpected(std::move(operation).error());
                }
                replay.operations.push_back(std::move(operation).value());
            }
            ++replay.records;
        }
    }
    if (first)
    {
        return unexpected(std::string("empty journal"));
    }
    return replay;
}

} // namespace illusion::editor
