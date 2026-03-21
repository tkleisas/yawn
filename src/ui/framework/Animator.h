#pragma once
// Animator — Lightweight tween animation system.
//
// Usage:
//   Animator animator;
//   float opacity = 0;
//   animator.animate(opacity, 1.0f, 0.3f, Ease::easeInOut);
//   // Each frame:
//   animator.update(dt);

#include <cmath>
#include <functional>
#include <vector>

namespace yawn {
namespace ui {
namespace fw {

// ─── Easing functions ───────────────────────────────────────────────────────

namespace Ease {
    inline float linear(float t) { return t; }

    inline float easeIn(float t) { return t * t; }
    inline float easeOut(float t) { return t * (2 - t); }
    inline float easeInOut(float t) {
        return t < 0.5f ? 2 * t * t : -1 + (4 - 2 * t) * t;
    }

    inline float easeInCubic(float t) { return t * t * t; }
    inline float easeOutCubic(float t) { float u = t - 1; return u * u * u + 1; }
    inline float easeInOutCubic(float t) {
        return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
    }

    inline float easeOutBounce(float t) {
        if (t < 1.0f / 2.75f) {
            return 7.5625f * t * t;
        } else if (t < 2.0f / 2.75f) {
            t -= 1.5f / 2.75f;
            return 7.5625f * t * t + 0.75f;
        } else if (t < 2.5f / 2.75f) {
            t -= 2.25f / 2.75f;
            return 7.5625f * t * t + 0.9375f;
        } else {
            t -= 2.625f / 2.75f;
            return 7.5625f * t * t + 0.984375f;
        }
    }

    inline float easeOutElastic(float t) {
        if (t <= 0 || t >= 1) return t;
        return std::pow(2.0f, -10 * t) * std::sin((t - 0.075f) * (2 * 3.14159265f) / 0.3f) + 1;
    }
}

using EaseFn = float(*)(float);

// ─── Animation ──────────────────────────────────────────────────────────────

struct Animation {
    float* target    = nullptr;   // Pointer to the value being animated
    float  startVal  = 0;
    float  endVal    = 0;
    float  duration  = 0;         // Total duration in seconds
    float  elapsed   = 0;         // Time elapsed so far
    EaseFn easeFn    = Ease::linear;
    std::function<void()> onComplete;

    bool finished() const { return elapsed >= duration; }

    void update(float dt) {
        elapsed += dt;
        float t = (duration > 0) ? (elapsed / duration) : 1.0f;
        if (t > 1.0f) t = 1.0f;

        float eased = easeFn(t);
        *target = startVal + (endVal - startVal) * eased;
    }
};

// ─── Animator ───────────────────────────────────────────────────────────────

class Animator {
public:
    // Animate a float value from its current value to target.
    void animate(float& value, float target, float durationSec,
                 EaseFn ease = Ease::easeInOut,
                 std::function<void()> onComplete = nullptr) {
        // Cancel any existing animation on this address
        cancel(&value);

        if (durationSec <= 0) {
            value = target;
            if (onComplete) onComplete();
            return;
        }

        Animation anim;
        anim.target    = &value;
        anim.startVal  = value;
        anim.endVal    = target;
        anim.duration  = durationSec;
        anim.elapsed   = 0;
        anim.easeFn    = ease;
        anim.onComplete = std::move(onComplete);
        m_animations.push_back(std::move(anim));
    }

    // Cancel animation on a specific value.
    void cancel(float* value) {
        m_animations.erase(
            std::remove_if(m_animations.begin(), m_animations.end(),
                           [value](const Animation& a) { return a.target == value; }),
            m_animations.end());
    }

    // Cancel all animations.
    void cancelAll() { m_animations.clear(); }

    // Update all animations. Call once per frame with delta time.
    void update(float dt) {
        for (auto& anim : m_animations) {
            anim.update(dt);
        }

        // Remove finished animations (fire callbacks first)
        for (auto it = m_animations.begin(); it != m_animations.end();) {
            if (it->finished()) {
                // Ensure final value is exact
                *it->target = it->endVal;
                if (it->onComplete) it->onComplete();
                it = m_animations.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Check if any animations are running.
    bool hasAnimations() const { return !m_animations.empty(); }

    // Check if a specific value is being animated.
    bool isAnimating(const float* value) const {
        for (const auto& a : m_animations) {
            if (a.target == value) return true;
        }
        return false;
    }

    int animationCount() const { return static_cast<int>(m_animations.size()); }

private:
    std::vector<Animation> m_animations;
};

} // namespace fw
} // namespace ui
} // namespace yawn
