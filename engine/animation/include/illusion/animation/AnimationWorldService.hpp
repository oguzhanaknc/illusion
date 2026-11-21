#pragma once

/// @file
/// The host's implementation of AnimationService over AnimationWorld.
///
/// @code
/// illusion::AnimationWorld animation(world, streamer);
/// illusion::AnimationWorldService animationService(animation);
/// services.provide<illusion::AnimationService>(animationService);
/// @endcode

#include <illusion/animation/AnimationService.hpp>
#include <illusion/animation/AnimationWorld.hpp>

namespace illusion
{

/// AnimationService over an AnimationWorld, which must outlive it. Created by the host; a shared module that
/// constructs one fails to link, since only the host has the library's code (ADR 0041).
class AnimationWorldService final : public AnimationService
{
public:
    /// Serves @p animation.
    explicit AnimationWorldService(AnimationWorld& animation) noexcept
        : m_animation(animation)
    {
    }
    ~AnimationWorldService() override;

    /// The AnimationWorld it serves.
    [[nodiscard]] AnimationWorld& animation() const noexcept { return m_animation; }

    /// @name AnimationService
    /// @{
    Expected<void, AnimationError> setFloat(Entity entity, std::string_view name, f32 value) override;
    Expected<void, AnimationError> setInt(Entity entity, std::string_view name, i32 value) override;
    Expected<void, AnimationError> setBool(Entity entity, std::string_view name, bool value) override;
    Expected<void, AnimationError> setTrigger(Entity entity, std::string_view name) override;
    [[nodiscard]] Expected<f32, AnimationError> parameter(Entity entity,
                                                          std::string_view name) const override;
    [[nodiscard]] Expected<std::string_view, AnimationError>
    currentState(Entity entity, std::string_view layer) const override;
    [[nodiscard]] Expected<f32, AnimationError> normalizedTime(Entity entity,
                                                               std::string_view layer) const override;
    Expected<void, AnimationError> play(Entity entity, std::string_view clip, f32 fadeSeconds) override;
    u32 pollEvents(std::span<AnimationEvent> out) override;
    [[nodiscard]] Expected<Mat4, AnimationError> jointTransform(Entity entity,
                                                                std::string_view joint) const override;
    Expected<void, AnimationError> setLookAtTarget(Entity entity, const Vec3& point) override;
    Expected<void, AnimationError> setIkTarget(Entity entity, std::string_view joint,
                                               const Vec3& point) override;
    Expected<void, AnimationError> setIkWeight(Entity entity, std::string_view joint, f32 weight) override;
    /// @}

private:
    AnimationWorld& m_animation;
};

} // namespace illusion
