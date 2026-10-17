#include <illusion/core/Version.hpp>

#include <doctest/doctest.h>

#include <compare>
#include <vector>

using namespace illusion;

namespace
{

SemanticVersion version(std::string_view text)
{
    const std::optional<SemanticVersion> parsed = SemanticVersion::parse(text);
    REQUIRE_MESSAGE(parsed.has_value(), text);
    return *parsed;
}

} // namespace

TEST_CASE("semantic versions parse and format")
{
    const SemanticVersion plain = version("1.22.333");
    CHECK(plain.major == 1);
    CHECK(plain.minor == 22);
    CHECK(plain.patch == 333);
    CHECK(plain.prerelease.empty());
    CHECK(plain.toString() == "1.22.333");

    const SemanticVersion pre = version("2.0.0-rc.1+build.5");
    CHECK(pre.prerelease == "rc.1");
    CHECK(pre.toString() == "2.0.0-rc.1"); // build metadata is not kept
    CHECK(version("0.0.0").toString() == "0.0.0");
    CHECK(version("1.0.0-x-y.0a.-").prerelease == "x-y.0a.-");
    CHECK(version("4294967295.0.0").major == 4294967295u);

    for (const char* bad :
         {"", "1", "1.2", "1.2.3.4", "v1.2.3", "01.2.3", "1.02.3", "1.2.03", "1.2.3-", "1.2.3-01",
          "1.2.3-a..b", "1.2.3+", "1.2.3+a_b", "1.2.3-a b", "-1.2.3", "1.2.x", "4294967296.0.0", " 1.2.3"})
    {
        CAPTURE(bad);
        CHECK_FALSE(SemanticVersion::parse(bad).has_value());
    }
}

TEST_CASE("semantic versions are ordered by precedence")
{
    // The example ordering from the semver 2.0.0 specification.
    const std::vector<SemanticVersion> ordered = {
        version("1.0.0-alpha"), version("1.0.0-alpha.1"), version("1.0.0-alpha.beta"),
        version("1.0.0-beta"),  version("1.0.0-beta.2"),  version("1.0.0-beta.11"),
        version("1.0.0-rc.1"),  version("1.0.0"),         version("1.0.1"),
        version("1.1.0"),       version("2.0.0"),
    };
    for (usize i = 0; i + 1 < ordered.size(); ++i)
    {
        CAPTURE(ordered[i].toString());
        CHECK(ordered[i] < ordered[i + 1]);
        CHECK(ordered[i + 1] > ordered[i]);
        CHECK(ordered[i] != ordered[i + 1]);
    }
    CHECK(version("1.0.0+a") == version("1.0.0+b"));
    CHECK(std::is_eq(version("1.2.3-rc.1") <=> version("1.2.3-rc.1")));
}

TEST_CASE("compatibility follows semver rules")
{
    CHECK(version("1.4.2").isCompatibleWith(version("1.2.0")));
    CHECK(version("1.2.0").isCompatibleWith(version("1.2.0")));
    CHECK_FALSE(version("1.1.9").isCompatibleWith(version("1.2.0")));
    CHECK_FALSE(version("2.0.0").isCompatibleWith(version("1.2.0")));
    // Before 1.0, minor versions are breaking.
    CHECK(version("0.3.5").isCompatibleWith(version("0.3.1")));
    CHECK_FALSE(version("0.4.0").isCompatibleWith(version("0.3.1")));
    CHECK_FALSE(version("1.2.0-rc.1").isCompatibleWith(version("1.2.0")));
}

TEST_CASE("the engine version matches the project version")
{
    const SemanticVersion& engine = engineVersion();
    CHECK(engine.isCompatibleWith(SemanticVersion{engine.major, engine.minor, 0, {}}));
    CHECK(SemanticVersion::parse(engine.toString()) == engine);
    CHECK(engine >= version("0.1.0"));
}
