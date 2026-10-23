#include <illusion/editor/client/Transcript.hpp>

#include <cmath>
#include <regex>

namespace illusion::editor::client
{
namespace
{

std::string compact(const JsonMutDocument& doc)
{
    std::string text;
    if (!doc.write(text, JsonWriteOptions{.pretty = false}))
    {
        return {};
    }
    return text;
}

std::string shown(JsonValue value)
{
    std::string text = value.isAbsent() ? std::string("nothing") : value.dump();
    constexpr usize kMax = 200;
    if (text.size() > kMax)
    {
        text.resize(kMax);
        text += "...";
    }
    return text;
}

std::string escapePointer(std::string_view key)
{
    std::string out;
    for (const char c : key)
    {
        if (c == '~')
        {
            out += "~0";
        }
        else if (c == '/')
        {
            out += "~1";
        }
        else
        {
            out += c;
        }
    }
    return out;
}

std::string at(const std::string& pointer)
{
    return pointer.empty() ? std::string("/") : pointer;
}

bool numbersEqual(JsonValue a, JsonValue b)
{
    if (a.isInteger() && b.isInteger())
    {
        if (a.asI64() && b.asI64())
        {
            return *a.asI64() == *b.asI64();
        }
        return a.asU64() == b.asU64();
    }
    return a.asF64() == b.asF64();
}

// The operator of a one-member object whose key starts with '$' ("" otherwise).
std::string_view operatorOf(JsonValue matcher)
{
    if (!matcher.isObject() || matcher.size() != 1)
    {
        return {};
    }
    const JsonMember member = *matcher.members().begin();
    return member.key.starts_with('$') ? member.key : std::string_view();
}

std::optional<std::string> matchOperator(std::string_view op, JsonValue argument, JsonValue actual,
                                         const std::string& pointer)
{
    const std::string where = at(pointer) + ": ";
    if (op == "$exists")
    {
        const bool wanted = argument.asBool().value_or(true);
        if (wanted == !actual.isAbsent())
        {
            return std::nullopt;
        }
        return where + (wanted ? "expected a value, got nothing" : "expected nothing, got " + shown(actual));
    }
    if (actual.isAbsent())
    {
        return where + "expected a value, got nothing";
    }
    if (op == "$approx")
    {
        const JsonValue target = argument.isArray() ? argument[0] : argument;
        const f64 tolerance = argument.isArray() ? argument[1].asF64().value_or(1e-4) : 1e-4;
        if (!target.isNumber() || !actual.isNumber())
        {
            return where + "expected a number near " + shown(target) + ", got " + shown(actual);
        }
        if (std::abs(*actual.asF64() - *target.asF64()) <= tolerance)
        {
            return std::nullopt;
        }
        return where + "expected " + shown(target) + " +/- " + std::to_string(tolerance) + ", got " +
               shown(actual);
    }
    if (op == "$regex")
    {
        const std::optional<std::string_view> pattern = argument.asString();
        const std::optional<std::string_view> text = actual.asString();
        if (!pattern || !text)
        {
            return where + "expected a string matching " + shown(argument) + ", got " + shown(actual);
        }
        try
        {
            if (std::regex_match(text->begin(), text->end(), std::regex(pattern->begin(), pattern->end())))
            {
                return std::nullopt;
            }
        }
        catch (const std::regex_error&)
        {
            return where + "invalid regular expression " + shown(argument);
        }
        return where + "expected a string matching " + shown(argument) + ", got " + shown(actual);
    }
    if (op == "$length")
    {
        if (!actual.isArray() && !actual.isObject() && !actual.isString())
        {
            return where + "expected an array, object or string, got " + shown(actual);
        }
        const u64 size = actual.isString() ? actual.asString()->size() : actual.size();
        const u64 min =
            argument.isObject() ? argument.find("min").asU64().value_or(0) : argument.asU64().value_or(0);
        const u64 max = argument.isObject() ? argument.find("max").asU64().value_or(~u64{0})
                                            : argument.asU64().value_or(~u64{0});
        if (size >= min && size <= max)
        {
            return std::nullopt;
        }
        return where + "expected length " + shown(argument) + ", got " + std::to_string(size);
    }
    if (op == "$oneOf")
    {
        for (const JsonValue option : argument.elements())
        {
            if (!matchJson(option, actual, pointer))
            {
                return std::nullopt;
            }
        }
        return where + "expected one of " + shown(argument) + ", got " + shown(actual);
    }
    if (op == "$contains")
    {
        if (!actual.isArray())
        {
            return where + "expected an array, got " + shown(actual);
        }
        for (const JsonValue element : actual.elements())
        {
            if (!matchJson(argument, element, pointer))
            {
                return std::nullopt;
            }
        }
        return where + "expected an element matching " + shown(argument) + ", got " + shown(actual);
    }
    return where + "unknown matcher " + std::string(op);
}

// Replaces ${name} in @p value; unknown names are reported in @p error.
JsonMutValue substitute(JsonMutDocument& doc, JsonValue value,
                        const std::map<std::string, std::string>& variables, std::string& error)
{
    if (value.isString())
    {
        const std::string_view text = *value.asString();
        if (text.starts_with("${") && text.ends_with('}') && text.find("${", 2) == std::string_view::npos)
        {
            const auto found = variables.find(std::string(text.substr(2, text.size() - 3)));
            if (found == variables.end())
            {
                error = "unknown variable " + std::string(text);
                return doc.makeNull();
            }
            Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(found->second);
            return parsed ? doc.copy(parsed->root()) : doc.makeString(found->second);
        }
        std::string out;
        for (usize i = 0; i < text.size();)
        {
            const usize begin = text.find("${", i);
            if (begin == std::string_view::npos)
            {
                out.append(text.substr(i));
                break;
            }
            const usize end = text.find('}', begin);
            if (end == std::string_view::npos)
            {
                out.append(text.substr(i));
                break;
            }
            out.append(text.substr(i, begin - i));
            const auto found = variables.find(std::string(text.substr(begin + 2, end - begin - 2)));
            if (found == variables.end())
            {
                error = "unknown variable " + std::string(text.substr(begin, end - begin + 1));
                return doc.makeNull();
            }
            Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(found->second);
            if (parsed && parsed->root().isString())
            {
                out.append(*parsed->root().asString());
            }
            else
            {
                out.append(found->second);
            }
            i = end + 1;
        }
        return doc.makeString(out);
    }
    if (value.isArray())
    {
        JsonMutValue array = doc.makeArray();
        for (const JsonValue element : value.elements())
        {
            array.push(substitute(doc, element, variables, error));
        }
        return array;
    }
    if (value.isObject())
    {
        JsonMutValue object = doc.makeObject();
        for (const JsonMember member : value.members())
        {
            object.add(member.key, substitute(doc, member.value, variables, error));
        }
        return object;
    }
    return doc.copy(value);
}

// @p value with variables replaced, as a parsed document.
Expected<JsonDocument, std::string> substituted(JsonValue value,
                                                const std::map<std::string, std::string>& variables)
{
    JsonMutDocument doc;
    std::string error;
    doc.setRoot(value.isAbsent() ? doc.makeObject() : substitute(doc, value, variables, error));
    if (!error.empty())
    {
        return unexpected(std::move(error));
    }
    Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(compact(doc));
    if (!parsed)
    {
        return unexpected(std::string("cannot substitute variables"));
    }
    return std::move(parsed).value();
}

} // namespace

std::optional<std::string> matchJson(JsonValue expected, JsonValue actual, const std::string& pointer)
{
    if (const std::string_view op = operatorOf(expected); !op.empty())
    {
        return matchOperator(op, (*expected.members().begin()).value, actual, pointer);
    }
    const std::string where = at(pointer) + ": ";
    if (actual.isAbsent())
    {
        return where + "expected " + shown(expected) + ", got nothing";
    }
    if (expected.isObject())
    {
        if (!actual.isObject())
        {
            return where + "expected an object, got " + shown(actual);
        }
        for (const JsonMember member : expected.members())
        {
            if (std::optional<std::string> mismatch = matchJson(member.value, actual.find(member.key),
                                                                pointer + "/" + escapePointer(member.key)))
            {
                return mismatch;
            }
        }
        return std::nullopt;
    }
    if (expected.isArray())
    {
        if (!actual.isArray() || actual.size() != expected.size())
        {
            return where + "expected an array of " + std::to_string(expected.size()) + ", got " +
                   shown(actual);
        }
        for (usize i = 0; i < expected.size(); ++i)
        {
            if (std::optional<std::string> mismatch =
                    matchJson(expected[i], actual[i], pointer + "/" + std::to_string(i)))
            {
                return mismatch;
            }
        }
        return std::nullopt;
    }
    const bool equal = expected.isNumber()
                           ? actual.isNumber() && numbersEqual(expected, actual)
                           : expected.type() == actual.type() && expected.dump() == actual.dump();
    if (equal)
    {
        return std::nullopt;
    }
    return where + "expected " + shown(expected) + ", got " + shown(actual);
}

TranscriptRunner::TranscriptRunner(RunnerOptions options)
    : m_options(std::move(options))
    , m_variables(m_options.variables)
{
}

Expected<void, std::string> TranscriptRunner::ensureStarted(const std::vector<std::string>& extra)
{
    if (m_client.connected())
    {
        return {};
    }
    LaunchOptions launch = m_options.launch;
    launch.arguments.insert(launch.arguments.end(), extra.begin(), extra.end());
    Expected<RpcClient, std::string> client = RpcClient::launch(launch);
    if (!client)
    {
        return unexpected(std::move(client).error());
    }
    m_client = std::move(client).value();
    return {};
}

std::optional<i32> TranscriptRunner::stop()
{
    return m_client.close();
}

RunReport TranscriptRunner::run(std::string_view jsonl, std::string_view name)
{
    RunReport report;
    std::string currentCheck;
    bool checkFailed = false;
    const auto log = [&](std::string_view line) {
        if (m_options.log)
        {
            m_options.log(line);
        }
    };
    const auto finishCheck = [&] {
        if (!currentCheck.empty())
        {
            (checkFailed ? report.failedChecks : report.passedChecks).push_back(currentCheck);
        }
    };

    usize lineNumber = 0;
    for (usize begin = 0; begin < jsonl.size();)
    {
        usize end = jsonl.find('\n', begin);
        if (end == std::string_view::npos)
        {
            end = jsonl.size();
        }
        std::string_view text = jsonl.substr(begin, end - begin);
        begin = end + 1;
        ++lineNumber;
        while (!text.empty() && (text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
        {
            text.remove_suffix(1);
        }
        if (text.find_first_not_of(" \t") == std::string_view::npos)
        {
            continue;
        }

        StepFailure failure;
        failure.line = lineNumber;
        Expected<JsonDocument, JsonError> parsed = JsonDocument::parse(text);
        if (!parsed || !parsed->root().isObject())
        {
            failure.description = std::string(name);
            failure.reason = "line is not a JSON object";
            report.failures.push_back(std::move(failure));
            break;
        }
        const JsonValue step = parsed->root();
        const std::string check(step.find("check").asString().value_or(""));
        if (check != currentCheck)
        {
            finishCheck();
            currentCheck = check;
            checkFailed = false;
            if (!check.empty())
            {
                log("check " + check + ": " + std::string(step.find("title").asString().value_or("")));
            }
        }
        if (!check.empty() && checkFailed)
        {
            continue;
        }
        failure.check = check;

        const auto fail = [&](std::string description, std::string reason, std::string actual = {}) {
            failure.description = std::move(description);
            failure.reason = std::move(reason);
            failure.actual = std::move(actual);
            log("  FAIL line " + std::to_string(lineNumber) + " " + failure.description + ": " +
                failure.reason);
            report.failures.push_back(failure);
            checkFailed = true;
        };

        ++report.steps;
        if (const JsonValue restart = step.find("restart"); !restart.isAbsent())
        {
            std::vector<std::string> arguments;
            if (const JsonValue list = restart.find("arguments"); list.isArray())
            {
                Expected<JsonDocument, std::string> values = substituted(list, m_variables);
                if (!values)
                {
                    fail("restart", values.error());
                    break;
                }
                for (const JsonValue argument : values->root().elements())
                {
                    arguments.emplace_back(argument.asString().value_or(argument.dump()));
                }
            }
            m_client.close();
            m_restartArguments = std::move(arguments);
            if (Expected<void, std::string> started = ensureStarted(m_restartArguments); !started)
            {
                fail("restart", started.error());
                break;
            }
            log("  restarted the editor");
            continue;
        }

        std::string method(step.find("call").asString().value_or(""));
        JsonValue paramsValue = step.find("params");
        std::string framesParams;
        if (const JsonValue frames = step.find("frames"); !frames.isAbsent())
        {
            method = "play.step";
            framesParams = R"({"frames":)" + frames.dump() + "}";
        }
        if (method.empty())
        {
            if (step.find("comment").isAbsent())
            {
                fail(std::string(name), "a step needs \"call\", \"frames\", \"restart\" or \"comment\"");
                break;
            }
            --report.steps;
            continue;
        }
        if (Expected<void, std::string> started = ensureStarted(m_restartArguments); !started)
        {
            fail(method, "cannot start the editor: " + started.error());
            break;
        }
        Expected<JsonDocument, std::string> params =
            framesParams.empty() ? substituted(paramsValue, m_variables)
                                 : substituted(JsonDocument::parse(framesParams)->root(), m_variables);
        if (!params)
        {
            fail(method, params.error());
            if (check.empty())
            {
                break;
            }
            continue;
        }
        JsonValue expectValue = step.find("expect");
        if (step.find("requires").asString() == std::optional<std::string_view>("gpu") && !m_options.gpu)
        {
            expectValue = step.find("otherwise");
        }
        static const JsonDocument kUnsupported =
            std::move(JsonDocument::parse(R"({"error":{"code":1400}})").value());
        static const JsonDocument kSuccess = std::move(JsonDocument::parse(R"({"result":{}})").value());
        if (expectValue.isAbsent())
        {
            expectValue =
                step.find("requires").isAbsent() || m_options.gpu ? kSuccess.root() : kUnsupported.root();
        }
        Expected<JsonDocument, std::string> expect = substituted(expectValue, m_variables);
        if (!expect)
        {
            fail(method, expect.error());
            if (check.empty())
            {
                break;
            }
            continue;
        }
        std::string paramsText;
        (void)params->root().write(paramsText, JsonWriteOptions{.pretty = false});
        const Expected<std::string, RpcError> reply = m_client.call(method, paramsText);
        if (!reply && reply.error().json.empty())
        {
            fail(method, reply.error().message);
            break;
        }
        const std::string replyText = reply ? *reply : reply.error().json;
        const Expected<JsonDocument, JsonError> replyDoc = JsonDocument::parse(replyText);
        if (!replyDoc)
        {
            fail(method, "the reply is not JSON", replyText);
            break;
        }
        const JsonValue wantError = expect->root().find("error");
        std::optional<std::string> mismatch;
        if (reply && !wantError.isAbsent())
        {
            mismatch = "expected an error, but the call succeeded";
        }
        else if (!reply && wantError.isAbsent())
        {
            mismatch = "failed with " + std::to_string(reply.error().code) + ": " + reply.error().message;
        }
        else
        {
            mismatch = matchJson(reply ? expect->root().find("result") : wantError, replyDoc->root());
        }
        if (mismatch)
        {
            fail(method, *mismatch, replyText);
            if (check.empty())
            {
                break;
            }
            continue;
        }
        bool captured = true;
        for (const JsonMember capture : step.find("capture").members())
        {
            const JsonValue value = replyDoc->root().pointer(capture.value.asString().value_or(""));
            if (value.isAbsent())
            {
                fail(method, "nothing to capture at " + std::string(capture.value.asString().value_or("")),
                     replyText);
                captured = false;
                break;
            }
            m_variables[std::string(capture.key)] = value.dump();
        }
        if (!captured && check.empty())
        {
            break;
        }
        if (captured)
        {
            log("  ok line " + std::to_string(lineNumber) + " " + method);
        }
    }
    finishCheck();
    return report;
}

} // namespace illusion::editor::client
