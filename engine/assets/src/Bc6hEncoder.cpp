#include "Bc6hEncoder.hpp"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace illusion::detail
{
namespace
{

constexpr i32 kMaxHalf = 0x7BFF; // 65504, the largest value an unsigned BC6H block decodes to
constexpr i32 kMaxEndpoint = 1023;
constexpr std::array<i32, 16> kWeights = {0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

using Texels = std::array<std::array<u16, 3>, 16>;
using Color = std::array<f64, 3>;

struct Fit
{
    std::array<i32, 3> endpoint0{};
    std::array<i32, 3> endpoint1{};
    std::array<u8, 16> indices{};
    i64 error = 0;
};

// Infinity saturates to the largest value; negative values and NaN become zero.
i32 sanitize(u16 half) noexcept
{
    if ((half & 0x8000u) != 0)
    {
        return 0;
    }
    if ((half & 0x7C00u) == 0x7C00u)
    {
        return (half & 0x03FFu) != 0 ? 0 : kMaxHalf;
    }
    return half;
}

// The decoder's steps for a 10-bit unsigned endpoint (BC6H specification, "unquantize" and "finish").
i32 unquantize(i32 endpoint) noexcept
{
    if (endpoint == 0)
    {
        return 0;
    }
    if (endpoint == kMaxEndpoint)
    {
        return 0xFFFF;
    }
    return ((endpoint << 16) + 0x8000) >> 10;
}

i32 interpolate(i32 unquantized0, i32 unquantized1, i32 weight) noexcept
{
    const i32 value = (unquantized0 * (64 - weight) + unquantized1 * weight + 32) >> 6;
    return (value * 31) >> 6;
}

i32 quantize(f64 value) noexcept
{
    const i32 guess = std::clamp(static_cast<i32>(std::lround(value / 31.0 - 0.5)), 0, kMaxEndpoint);
    i32 best = guess;
    f64 bestError = std::abs(interpolate(unquantize(guess), 0, 0) - value);
    for (const i32 candidate : {guess - 1, guess + 1})
    {
        if (candidate < 0 || candidate > kMaxEndpoint)
        {
            continue;
        }
        const f64 error = std::abs(interpolate(unquantize(candidate), 0, 0) - value);
        if (error < bestError)
        {
            best = candidate;
            bestError = error;
        }
    }
    return best;
}

Fit evaluate(const std::array<std::array<i32, 3>, 16>& texels, const Color& color0,
             const Color& color1) noexcept
{
    Fit fit;
    std::array<std::array<i32, 3>, 16> palette{};
    for (usize c = 0; c < 3; ++c)
    {
        fit.endpoint0[c] = quantize(std::clamp(color0[c], 0.0, static_cast<f64>(kMaxHalf)));
        fit.endpoint1[c] = quantize(std::clamp(color1[c], 0.0, static_cast<f64>(kMaxHalf)));
        const i32 unquantized0 = unquantize(fit.endpoint0[c]);
        const i32 unquantized1 = unquantize(fit.endpoint1[c]);
        for (usize k = 0; k < palette.size(); ++k)
        {
            palette[k][c] = interpolate(unquantized0, unquantized1, kWeights[k]);
        }
    }
    for (usize i = 0; i < texels.size(); ++i)
    {
        i64 bestError = -1;
        for (usize k = 0; k < palette.size(); ++k)
        {
            i64 error = 0;
            for (usize c = 0; c < 3; ++c)
            {
                const i64 difference = texels[i][c] - palette[k][c];
                error += difference * difference;
            }
            if (bestError < 0 || error < bestError)
            {
                bestError = error;
                fit.indices[i] = static_cast<u8>(k);
            }
        }
        fit.error += bestError;
    }
    return fit;
}

Color principalAxis(const std::array<std::array<i32, 3>, 16>& texels, const Color& mean) noexcept
{
    std::array<Color, 3> covariance{};
    for (const std::array<i32, 3>& texel : texels)
    {
        const Color d = {texel[0] - mean[0], texel[1] - mean[1], texel[2] - mean[2]};
        for (usize r = 0; r < 3; ++r)
        {
            for (usize c = 0; c < 3; ++c)
            {
                covariance[r][c] += d[r] * d[c];
            }
        }
    }
    // Power iteration from the covariance row of largest norm, which is never orthogonal to the axis.
    Color axis = covariance[0];
    f64 largest = 0.0;
    for (const Color& row : covariance)
    {
        const f64 norm = row[0] * row[0] + row[1] * row[1] + row[2] * row[2];
        if (norm > largest)
        {
            largest = norm;
            axis = row;
        }
    }
    if (largest <= 0.0)
    {
        return {0.0, 0.0, 0.0};
    }
    for (int iteration = 0; iteration < 8; ++iteration)
    {
        Color next{};
        for (usize r = 0; r < 3; ++r)
        {
            next[r] = covariance[r][0] * axis[0] + covariance[r][1] * axis[1] + covariance[r][2] * axis[2];
        }
        const f64 length = std::sqrt(next[0] * next[0] + next[1] * next[1] + next[2] * next[2]);
        if (length <= 0.0)
        {
            break;
        }
        axis = {next[0] / length, next[1] / length, next[2] / length};
    }
    const f64 length = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    return {axis[0] / length, axis[1] / length, axis[2] / length};
}

// Endpoints that minimize the squared error for the weights of @p fit's indices; false if they are
// undetermined (every texel uses the same weight).
bool leastSquares(const std::array<std::array<i32, 3>, 16>& texels, const Fit& fit, Color& color0,
                  Color& color1) noexcept
{
    f64 s00 = 0.0;
    f64 s01 = 0.0;
    f64 s11 = 0.0;
    Color r0{};
    Color r1{};
    for (usize i = 0; i < texels.size(); ++i)
    {
        const f64 a = kWeights[fit.indices[i]] / 64.0;
        const f64 b = 1.0 - a;
        s00 += b * b;
        s01 += a * b;
        s11 += a * a;
        for (usize c = 0; c < 3; ++c)
        {
            r0[c] += b * texels[i][c];
            r1[c] += a * texels[i][c];
        }
    }
    const f64 determinant = s00 * s11 - s01 * s01;
    if (determinant < 1e-9)
    {
        return false;
    }
    for (usize c = 0; c < 3; ++c)
    {
        color0[c] = (s11 * r0[c] - s01 * r1[c]) / determinant;
        color1[c] = (s00 * r1[c] - s01 * r0[c]) / determinant;
    }
    return true;
}

class BitWriter
{
public:
    explicit BitWriter(std::byte* out) noexcept
        : m_out(out)
    {
    }
    void write(u32 value, u32 bits) noexcept
    {
        for (u32 bit = 0; bit < bits; ++bit, ++m_position)
        {
            if (((value >> bit) & 1u) != 0)
            {
                m_out[m_position / 8] |= std::byte{static_cast<u8>(1u << (m_position % 8))};
            }
        }
    }

private:
    std::byte* m_out;
    u32 m_position = 0;
};

} // namespace

u16 toUnsignedHalf(f32 value) noexcept
{
    if (!(value > 0.0f))
    {
        return 0;
    }
    return glm::packHalf1x16(std::min(value, 65504.0f));
}

void encodeBc6hBlock(const Texels& input, u32 refinementPasses, std::byte* block) noexcept
{
    std::array<std::array<i32, 3>, 16> texels{};
    Color mean{};
    for (usize i = 0; i < texels.size(); ++i)
    {
        for (usize c = 0; c < 3; ++c)
        {
            texels[i][c] = sanitize(input[i][c]);
            mean[c] += texels[i][c] / 16.0;
        }
    }
    const Color axis = principalAxis(texels, mean);
    f64 low = 0.0;
    f64 high = 0.0;
    for (const std::array<i32, 3>& texel : texels)
    {
        const f64 t =
            (texel[0] - mean[0]) * axis[0] + (texel[1] - mean[1]) * axis[1] + (texel[2] - mean[2]) * axis[2];
        low = std::min(low, t);
        high = std::max(high, t);
    }
    Color color0{};
    Color color1{};
    for (usize c = 0; c < 3; ++c)
    {
        color0[c] = mean[c] + axis[c] * low;
        color1[c] = mean[c] + axis[c] * high;
    }
    Fit best = evaluate(texels, color0, color1);
    for (u32 pass = 0; pass < refinementPasses && best.error > 0; ++pass)
    {
        if (!leastSquares(texels, best, color0, color1))
        {
            break;
        }
        const Fit refined = evaluate(texels, color0, color1);
        if (refined.error >= best.error)
        {
            break;
        }
        best = refined;
    }

    // The anchor (texel 0) stores its index without the top bit; the weights are symmetric, so swapping the
    // endpoints and mirroring every index decodes to the same colours.
    if (best.indices[0] >= 8)
    {
        std::swap(best.endpoint0, best.endpoint1);
        for (u8& index : best.indices)
        {
            index = static_cast<u8>(15 - index);
        }
    }

    std::memset(block, 0, 16);
    BitWriter writer(block);
    writer.write(0x03, 5); // mode 11: one region, 10-bit endpoints, 4-bit indices
    for (const std::array<i32, 3>& endpoint : {best.endpoint0, best.endpoint1})
    {
        for (const i32 component : endpoint)
        {
            writer.write(static_cast<u32>(component), 10);
        }
    }
    writer.write(best.indices[0], 3);
    for (usize i = 1; i < best.indices.size(); ++i)
    {
        writer.write(best.indices[i], 4);
    }
}

} // namespace illusion::detail
