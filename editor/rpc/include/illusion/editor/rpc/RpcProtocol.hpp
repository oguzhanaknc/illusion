#pragma once

/// @file
/// The editor RPC protocol: JSON-RPC 2.0 messages framed as newline-delimited JSON (ADR 0037).

#include <illusion/core/Base.hpp>
#include <illusion/core/Expected.hpp>
#include <illusion/core/Json.hpp>
#include <illusion/editor/core/CommandError.hpp>

#include <string>
#include <string_view>

namespace illusion::editor::rpc
{

/// Semantic version of the protocol (0.x until the protocol is declared stable).
inline constexpr std::string_view kProtocolVersion = "0.1.0";

/// Largest message, in bytes without the line break (16 MiB).
inline constexpr usize kMaxMessageSize = 16u * 1024u * 1024u;

/// Error codes defined by JSON-RPC 2.0. Command failures use the positive codes of ErrorKind.
enum class ProtocolError : i32
{
    ParseError = -32700,     ///< The line is not valid JSON (or not UTF-8).
    InvalidRequest = -32600, ///< Not a JSON-RPC 2.0 request object (also: batch arrays, oversized lines).
    MethodNotFound = -32601, ///< No such method; `data.suggestions` lists close names.
    InvalidParams = -32602,  ///< `params` is not an object.
    InternalError = -32603,  ///< A bug in the server.
};

/// Splits a byte stream into lines (NDJSON framing). A line ends at `\n`; a trailing `\r` is dropped and
/// empty lines are skipped.
class LineFramer
{
public:
    /// A framer that rejects lines longer than @p maxLine bytes.
    explicit LineFramer(usize maxLine = kMaxMessageSize) noexcept
        : m_maxLine(maxLine)
    {
    }

    /// Feeds @p bytes and calls @p sink(std::string_view line) for each complete line. Returns false, and
    /// stops for good, once a line exceeds the limit.
    template <class Sink>
    bool feed(std::string_view bytes, Sink&& sink)
    {
        if (m_overflow)
        {
            return false;
        }
        while (!bytes.empty())
        {
            const usize newline = bytes.find('\n');
            if (newline == std::string_view::npos)
            {
                if (m_partial.size() + bytes.size() > m_maxLine)
                {
                    m_overflow = true;
                    return false;
                }
                m_partial.append(bytes);
                return true;
            }
            std::string_view line = bytes.substr(0, newline);
            bytes.remove_prefix(newline + 1);
            if (!m_partial.empty())
            {
                if (m_partial.size() + line.size() > m_maxLine + 1)
                {
                    m_overflow = true;
                    return false;
                }
                m_partial.append(line);
                line = m_partial;
            }
            if (line.ends_with('\r'))
            {
                line.remove_suffix(1);
            }
            if (line.size() > m_maxLine)
            {
                m_overflow = true;
                return false;
            }
            if (!line.empty())
            {
                sink(line);
            }
            m_partial.clear();
        }
        return true;
    }

    /// Bytes of an unfinished line.
    [[nodiscard]] usize pending() const noexcept { return m_partial.size(); }

    /// True once a line exceeded the limit.
    [[nodiscard]] bool overflowed() const noexcept { return m_overflow; }

private:
    std::string m_partial;
    usize m_maxLine;
    bool m_overflow = false;
};

/// A request (or notification) read from one line.
struct Request
{
    JsonDocument document;   ///< Owns the parsed line.
    std::string id;          ///< The id as compact JSON (e.g. `7` or `"a"`); empty for a notification.
    std::string_view method; ///< Method name (points into document).
    JsonValue params;        ///< `params` (an object, or absent).

    /// True if no reply is expected.
    [[nodiscard]] bool notification() const noexcept { return id.empty(); }
};

/// Parses and checks one message. On failure the error is a complete error response line (without the line
/// break) to send back; it is empty when the message was a notification, which never gets a reply.
[[nodiscard]] Expected<Request, std::string> parseRequest(std::string_view line);

/// A success response: `{"jsonrpc":"2.0","id":<id>,"result":<result>}`; @p result is compact JSON.
[[nodiscard]] std::string resultResponse(std::string_view id, std::string_view result);

/// An error response with a protocol or application @p code. @p data is compact JSON ("" for none); @p id
/// "" means `null`.
[[nodiscard]] std::string errorResponse(std::string_view id, i32 code, std::string_view message,
                                        std::string_view data = {});

/// The error response of a failed command (`code` = its ErrorKind, `data` = {kind, errors, suggestions,
/// retryable}).
[[nodiscard]] std::string errorResponse(std::string_view id, const CommandError& error);

/// A notification: `{"jsonrpc":"2.0","method":<method>,"params":<params>}`; @p params is compact JSON.
[[nodiscard]] std::string notification(std::string_view method, std::string_view params);

/// Compares two strings in time that depends only on their lengths (for tokens).
[[nodiscard]] bool constantTimeEqual(std::string_view a, std::string_view b) noexcept;

} // namespace illusion::editor::rpc
