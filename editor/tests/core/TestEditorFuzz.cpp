#include "EditorCommandCases.hpp"
#include "EditorCoreTestSupport.hpp"

#include <illusion/editor/core/JsonSchema.hpp>

using namespace illusion;
using namespace illusion::editor;
using test::EditorFixture;

namespace
{

// Values that are wrong somewhere: other types, out of range numbers, dead and unknown references.
constexpr std::string_view kBadValues[] = {
    "0",
    "-1",
    "4294967296",
    "1e300",
    "0.5",
    R"("")",
    R"("x")",
    "true",
    "null",
    "[]",
    "{}",
    "[1,2]",
    R"(["$selection"])",
    R"("0000000000000001")",
    R"("/Nope/X")",
    R"("name:Nope")",
    R"("$selection")",
    R"("test.Nope")",
    R"({"weight":"x"})",
    R"({"illusion.Nope":{}})",
    R"("assets:/../x")",
};

std::string write(const JsonMutDocument& doc)
{
    std::string text;
    REQUIRE(doc.write(text, JsonWriteOptions{.pretty = false}));
    return text;
}

// Mutations of @p params: each member removed, each member (and each member of an object member) replaced by
// every bad value, an unknown member added, and params that are not objects.
std::vector<std::string> mutations(std::string_view params)
{
    std::vector<std::string> out{"[]", R"("x")", "5", "null", R"({"zzz":1})"};
    const JsonDocument source = test::parseJson(params);
    std::vector<std::string> keys;
    for (const JsonMember member : source.root().members())
    {
        keys.emplace_back(member.key);
    }
    for (const std::string& key : keys)
    {
        {
            JsonMutDocument doc;
            JsonMutValue root = doc.copy(source.root());
            root.remove(key);
            doc.setRoot(root);
            out.push_back(write(doc));
        }
        std::vector<std::string> inner;
        for (const JsonMember member : source.root().find(key).members())
        {
            inner.emplace_back(member.key);
        }
        for (const std::string_view bad : kBadValues)
        {
            const JsonDocument value = test::parseJson(bad);
            JsonMutDocument doc;
            JsonMutValue root = doc.copy(source.root());
            root.set(key, doc.copy(value.root()));
            doc.setRoot(root);
            out.push_back(write(doc));
            for (const std::string& innerKey : inner)
            {
                JsonMutDocument nested;
                JsonMutValue nestedRoot = nested.copy(source.root());
                nestedRoot.find(key).set(innerKey, nested.copy(value.root()));
                nested.setRoot(nestedRoot);
                out.push_back(write(nested));
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("invalid params of every command give structured errors and never reach an assert")
{
    test::ScopedAssertCatcher asserts;
    usize calls = 0;
    usize failures = 0;
    for (const test::CommandCase& entry : test::kCommandCases)
    {
        CAPTURE(entry.name);
        EditorFixture fixture(3);
        fixture.buildScene();
        EditorContext& editor = *fixture.editor;
        const CommandDescriptor& command = *editor.commands().find(entry.name);
        JsonMutDocument schemaDoc;
        schemaDoc.setRoot(writeCommandResultSchema(schemaDoc, command));
        const JsonDocument resultSchema = test::parseJson(write(schemaDoc));
        for (const std::string& params : mutations(entry.params))
        {
            CAPTURE(params);
            ++calls;
            Expected<std::string, CommandError> result = [&] {
                try
                {
                    return test::run(editor, entry.name, params);
                }
                catch (const test::AssertionFailure& failure)
                {
                    FAIL(failure.what());
                    return Expected<std::string, CommandError>(unexpected(CommandError{}));
                }
            }();
            if (result)
            {
                const JsonDocument parsed = test::parseJson(*result);
                CHECK(validateJson(resultSchema.root(), parsed.root()).empty());
                continue;
            }
            ++failures;
            CHECK(result.error().kind != ErrorKind::Internal);
            CHECK_FALSE(result.error().message.empty());
            if (result.error().kind == ErrorKind::ValidationFailed)
            {
                CHECK_FALSE(result.error().errors.empty());
            }
        }
        // Whatever succeeded can be undone completely.
        CHECK(editor.undo(10000).hasValue());
    }
    CHECK(calls > 1000);
    CHECK(failures > calls / 2);
}
