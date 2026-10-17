#include <illusion/assets/Environment.hpp>

#include <doctest/doctest.h>

#include <cstring>
#include <numbers>
#include <span>
#include <string>

using namespace illusion;

namespace
{

constexpr double kPi = std::numbers::pi;

Image makeEnvironment(u32 width, Vec3 (*radiance)(Vec3))
{
    Image image = makeSolidImage(width, width / 2, Vec4(0.0f), ImageFormat::RGBA32F);
    auto* pixels = reinterpret_cast<f32*>(image.data.data());
    for (u32 y = 0; y < width / 2; ++y)
    {
        for (u32 x = 0; x < width; ++x)
        {
            const Vec3 d =
                equirectDirection(Vec2((static_cast<f32>(x) + 0.5f) / static_cast<f32>(width),
                                       (static_cast<f32>(y) + 0.5f) / static_cast<f32>(width / 2)));
            const Vec3 value = radiance(d);
            std::memcpy(pixels + (static_cast<usize>(y) * width + x) * 4, &value.x, 12);
        }
    }
    return image;
}

} // namespace

TEST_CASE("equirectangular mapping puts the image centre in front and round-trips")
{
    const Vec3 centre = equirectDirection(Vec2(0.5f, 0.5f));
    CHECK(centre.z == doctest::Approx(-1.0));
    const Vec3 east = equirectDirection(Vec2(0.75f, 0.5f));
    CHECK(east.x == doctest::Approx(1.0));
    CHECK(equirectDirection(Vec2(0.3f, 0.0f)).y == doctest::Approx(1.0));
    CHECK(equirectDirection(Vec2(0.3f, 1.0f)).y == doctest::Approx(-1.0));

    for (const Vec2 uv : {Vec2(0.1f, 0.2f), Vec2(0.5f, 0.5f), Vec2(0.9f, 0.7f), Vec2(0.25f, 0.95f)})
    {
        const Vec2 back = equirectUv(equirectDirection(uv));
        CHECK(back.x == doctest::Approx(uv.x).epsilon(1e-4));
        CHECK(back.y == doctest::Approx(uv.y).epsilon(1e-4));
    }
}

TEST_CASE("SH irradiance of a uniform environment is pi times its radiance")
{
    const Image image = makeEnvironment(128, [](Vec3) { return Vec3(1.0f, 0.5f, 2.0f); });
    const ShIrradiance sh = computeShIrradiance(image);
    for (const Vec3 n : {Vec3(0, 1, 0), Vec3(1, 0, 0), Vec3(0, 0, -1), glm::normalize(Vec3(1, -1, 1))})
    {
        const Vec3 e = sh.evaluate(n);
        CHECK(e.r == doctest::Approx(kPi).epsilon(0.01));
        CHECK(e.g == doctest::Approx(kPi * 0.5).epsilon(0.01));
        CHECK(e.b == doctest::Approx(kPi * 2.0).epsilon(0.01));
    }
    const Vec3 analytic = ShIrradiance::uniform(Vec3(1.0f)).evaluate(Vec3(0, 0, 1));
    CHECK(analytic.r == doctest::Approx(kPi).epsilon(1e-4));
}

TEST_CASE("SH irradiance follows a directional environment")
{
    // Radiance cos(theta) above the horizon: irradiance at the zenith is 2pi/3, at the nadir 0.
    const Image image = makeEnvironment(256, [](Vec3 d) { return Vec3(d.y > 0.0f ? d.y : 0.0f); });
    const ShIrradiance sh = computeShIrradiance(image);
    CHECK(sh.evaluate(Vec3(0, 1, 0)).r == doctest::Approx(2.0 * kPi / 3.0).epsilon(0.03));
    CHECK(sh.evaluate(Vec3(0, -1, 0)).r < 0.05f);
    CHECK(sh.evaluate(Vec3(1, 0, 0)).r == doctest::Approx(sh.evaluate(Vec3(0, 0, 1)).r).epsilon(0.01));
    CHECK(computeShIrradiance(Image{}).evaluate(Vec3(0, 1, 0)) == Vec3(0.0f));
}

TEST_CASE("readPixel decodes every image format to linear values")
{
    Image srgb = makeSolidImage(2, 2, Vec4(0.5f, 1.0f, 0.0f, 1.0f), ImageFormat::RGBA8Srgb);
    const Vec4 decoded = readPixel(srgb, 1, 1);
    CHECK(decoded.r == doctest::Approx(0.2158).epsilon(0.01)); // sRGB 128 -> linear 0.216
    CHECK(decoded.g == doctest::Approx(1.0));
    CHECK(readPixel(makeSolidImage(1, 1, Vec4(0.5f), ImageFormat::RGBA8), 0, 0).r ==
          doctest::Approx(128.0 / 255.0));
    CHECK(readPixel(makeSolidImage(1, 1, Vec4(7.0f), ImageFormat::RGBA32F), 0, 0).r == doctest::Approx(7.0));
}

TEST_CASE("the procedural sky has a bright sky, a dark ground and an optional sun")
{
    SkyDesc desc;
    desc.sunRadiance = Vec3(1000.0f);
    desc.sunDirection = Vec3(0.0f, 0.0f, -1.0f); // at the image centre
    desc.sunAngularRadius = 0.2f;                // several texels at this resolution
    const Image sky = makeSkyImage(64, desc);
    REQUIRE(sky.format == ImageFormat::RGBA32F);
    CHECK(sky.width() == 64);
    CHECK(sky.height() == 32);
    CHECK(readPixel(sky, 10, 1).b == doctest::Approx(desc.zenith.b).epsilon(0.1));
    CHECK(readPixel(sky, 10, 30).r == doctest::Approx(desc.ground.r).epsilon(0.05));
    CHECK(readPixel(sky, 32, 16).r == doctest::Approx(1000.0));
    CHECK(readPixel(makeSkyImage(64), 32, 16).r < 2.0f); // no sun disc by default
}

TEST_CASE("Radiance HDR files decode to float images")
{
    // 2x1 flat (uncompressed) RGBE: (1, 0.5, 0.25) and (2, 2, 2).
    std::string file = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    file.append("\x80\x40\x20\x81\x80\x80\x80\x82", 8);
    const auto bytes = std::as_bytes(std::span(file));
    const auto image = decodeImage(bytes, false);
    REQUIRE(image.has_value());
    CHECK(image->format == ImageFormat::RGBA32F);
    CHECK(image->width() == 2);
    const Vec4 first = readPixel(*image, 0, 0);
    CHECK(first.r == doctest::Approx(1.0));
    CHECK(first.g == doctest::Approx(0.5));
    CHECK(first.b == doctest::Approx(0.25));
    CHECK(readPixel(*image, 1, 0).g == doctest::Approx(2.0));
}
