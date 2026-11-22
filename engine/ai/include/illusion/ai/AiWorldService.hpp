#pragma once

/// @file
/// The host's implementation of AiService over AiWorld.
///
/// @code
/// illusion::AiWorld ai(world, navigation, &streamer);
/// illusion::AiWorldService aiService(ai);
/// services.provide<illusion::AiService>(aiService);
/// @endcode

#include <illusion/ai/AiService.hpp>
#include <illusion/ai/AiWorld.hpp>

namespace illusion
{

/// AiService over an AiWorld, which must outlive it. Created by the host; a shared module that constructs one
/// fails to link, since only the host has the library's code (ADR 0041).
class AiWorldService final : public AiService
{
public:
    /// Serves @p ai.
    explicit AiWorldService(AiWorld& ai) noexcept
        : m_ai(ai)
    {
    }
    ~AiWorldService() override;

    /// The AiWorld it serves.
    [[nodiscard]] AiWorld& ai() const noexcept { return m_ai; }

    /// @name AiService
    /// @{
    Expected<void, AiError> setBool(Entity entity, std::string_view key, bool value) override;
    Expected<void, AiError> setInt(Entity entity, std::string_view key, i32 value) override;
    Expected<void, AiError> setFloat(Entity entity, std::string_view key, f32 value) override;
    Expected<void, AiError> setVector(Entity entity, std::string_view key, const Vec3& value) override;
    Expected<void, AiError> setEntity(Entity entity, std::string_view key, Entity value) override;
    Expected<void, AiError> clearValue(Entity entity, std::string_view key) override;
    [[nodiscard]] Expected<BlackboardValue, AiError> value(Entity entity,
                                                           std::string_view key) const override;
    u32 pollActions(std::span<AiActionRequest> out) override;
    Expected<void, AiError> finishAction(Entity entity, u32 action, bool success) override;
    Expected<void, AiError> reportNoise(const AiNoise& noise) override;
    Expected<u32, AiError> perceived(Entity entity, std::span<AiPerceived> out) const override;
    [[nodiscard]] Expected<std::string_view, AiError> runningNode(Entity entity) const override;
    Expected<void, AiError> restart(Entity entity) override;
    /// @}

private:
    AiWorld& m_ai;
};

} // namespace illusion
