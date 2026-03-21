#pragma once
// Core geometric types and DPI-aware sizing for the YAWN UI framework.

#include <algorithm>
#include <cmath>

namespace yawn {
namespace ui {
namespace fw {

namespace detail {
    constexpr float cmin(float a, float b) { return a < b ? a : b; }
    constexpr float cmax(float a, float b) { return a > b ? a : b; }
    constexpr float cclamp(float v, float lo, float hi) { return cmin(cmax(v, lo), hi); }
}

// ─── Point ──────────────────────────────────────────────────────────────────

struct Point {
    float x = 0;
    float y = 0;

    constexpr Point() = default;
    constexpr Point(float x, float y) : x(x), y(y) {}

    constexpr Point operator+(const Point& o) const { return {x + o.x, y + o.y}; }
    constexpr Point operator-(const Point& o) const { return {x - o.x, y - o.y}; }
    constexpr Point operator*(float s) const { return {x * s, y * s}; }
    constexpr bool  operator==(const Point& o) const { return x == o.x && y == o.y; }
    constexpr bool  operator!=(const Point& o) const { return !(*this == o); }

    float length() const { return std::sqrt(x * x + y * y); }
    float distanceTo(const Point& o) const { return (*this - o).length(); }
};

// ─── Size ───────────────────────────────────────────────────────────────────

struct Size {
    float w = 0;
    float h = 0;

    constexpr Size() = default;
    constexpr Size(float w, float h) : w(w), h(h) {}

    constexpr bool operator==(const Size& o) const { return w == o.w && h == o.h; }
    constexpr bool operator!=(const Size& o) const { return !(*this == o); }

    constexpr Size operator+(const Size& o) const { return {w + o.w, h + o.h}; }
    constexpr Size operator*(float s) const { return {w * s, h * s}; }

    constexpr bool isEmpty() const { return w <= 0 || h <= 0; }

    static constexpr Size zero() { return {0, 0}; }
};

// ─── Insets ─────────────────────────────────────────────────────────────────

struct Insets {
    float top    = 0;
    float right  = 0;
    float bottom = 0;
    float left   = 0;

    constexpr Insets() = default;
    constexpr Insets(float all) : top(all), right(all), bottom(all), left(all) {}
    constexpr Insets(float v, float h) : top(v), right(h), bottom(v), left(h) {}
    constexpr Insets(float t, float r, float b, float l) : top(t), right(r), bottom(b), left(l) {}

    constexpr float horizontal() const { return left + right; }
    constexpr float vertical()   const { return top + bottom; }

    constexpr bool operator==(const Insets& o) const {
        return top == o.top && right == o.right && bottom == o.bottom && left == o.left;
    }
    constexpr bool operator!=(const Insets& o) const { return !(*this == o); }

    static constexpr Insets zero() { return {}; }
};

// ─── Rect ───────────────────────────────────────────────────────────────────

struct Rect {
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;

    constexpr Rect() = default;
    constexpr Rect(float x, float y, float w, float h) : x(x), y(y), w(w), h(h) {}
    constexpr Rect(Point origin, Size size) : x(origin.x), y(origin.y), w(size.w), h(size.h) {}

    constexpr Point  origin() const { return {x, y}; }
    constexpr Size   size()   const { return {w, h}; }
    constexpr float  right()  const { return x + w; }
    constexpr float  bottom() const { return y + h; }
    constexpr Point  center() const { return {x + w * 0.5f, y + h * 0.5f}; }

    constexpr bool isEmpty() const { return w <= 0 || h <= 0; }

    constexpr bool contains(float px, float py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    constexpr bool contains(Point p) const { return contains(p.x, p.y); }

    constexpr bool intersects(const Rect& o) const {
        return x < o.x + o.w && x + w > o.x &&
               y < o.y + o.h && y + h > o.y;
    }

    constexpr Rect intersection(const Rect& o) const {
        float ix = detail::cmax(x, o.x);
        float iy = detail::cmax(y, o.y);
        float iw = detail::cmin(x + w, o.x + o.w) - ix;
        float ih = detail::cmin(y + h, o.y + o.h) - iy;
        if (iw <= 0 || ih <= 0) return {};
        return {ix, iy, iw, ih};
    }

    constexpr Rect united(const Rect& o) const {
        if (isEmpty()) return o;
        if (o.isEmpty()) return *this;
        float ux = detail::cmin(x, o.x);
        float uy = detail::cmin(y, o.y);
        float ur = detail::cmax(x + w, o.x + o.w);
        float ub = detail::cmax(y + h, o.y + o.h);
        return {ux, uy, ur - ux, ub - uy};
    }

    // Returns a new rect shrunk by the given insets.
    constexpr Rect inset(const Insets& i) const {
        return {x + i.left, y + i.top, w - i.horizontal(), h - i.vertical()};
    }

    // Returns a new rect expanded by the given insets.
    constexpr Rect outset(const Insets& i) const {
        return {x - i.left, y - i.top, w + i.horizontal(), h + i.vertical()};
    }

    // Returns a new rect offset by (dx, dy).
    constexpr Rect translated(float dx, float dy) const {
        return {x + dx, y + dy, w, h};
    }

    constexpr bool operator==(const Rect& o) const {
        return x == o.x && y == o.y && w == o.w && h == o.h;
    }
    constexpr bool operator!=(const Rect& o) const { return !(*this == o); }

    static constexpr Rect zero() { return {}; }
};

// ─── Alignment / Layout enums ───────────────────────────────────────────────

enum class Direction { Row, Column };

enum class Justify {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
};

enum class Align {
    Start,
    Center,
    End,
    Stretch,
};

enum class TextAlign { Left, Center, Right };

// ─── SizePolicy ─────────────────────────────────────────────────────────────
// Controls how a child participates in flex layout.

struct SizePolicy {
    float flexWeight = 0;  // 0 = fixed to preferred size; >0 = flex weight
    float minSize    = 0;  // Minimum extent along main axis
    float maxSize    = 1e6f;  // Maximum extent along main axis

    static constexpr SizePolicy fixed() { return {0, 0, 1e6f}; }
    static constexpr SizePolicy flex(float weight = 1.0f) { return {weight, 0, 1e6f}; }
    static constexpr SizePolicy flexMin(float weight, float mn) { return {weight, mn, 1e6f}; }
};

// ─── DPI / Scaling ──────────────────────────────────────────────────────────
// Design pixels (dp): UI is authored at 1x (96 DPI). The scale factor converts
// dp to physical pixels for the current display.

struct ScaleContext {
    float scale = 1.0f;    // 1.0 at 96 DPI; 1.5 at 144 DPI; 2.0 at 192 DPI

    constexpr float dp(float value) const { return value * scale; }
    constexpr float px(float dpValue) const { return dpValue * scale; }
    constexpr float fromPx(float pxValue) const { return scale > 0 ? pxValue / scale : pxValue; }

    constexpr Size  dp(Size s)  const { return {s.w * scale, s.h * scale}; }
    constexpr Rect  dp(Rect r)  const { return {r.x * scale, r.y * scale, r.w * scale, r.h * scale}; }
    constexpr Insets dp(Insets i) const { return {i.top * scale, i.right * scale, i.bottom * scale, i.left * scale}; }
};

// ─── Constraints ────────────────────────────────────────────────────────────
// Passed to measure() to indicate available space.

struct Constraints {
    float minW = 0;
    float minH = 0;
    float maxW = 1e6f;
    float maxH = 1e6f;

    constexpr Constraints() = default;
    constexpr Constraints(float minW, float minH, float maxW, float maxH)
        : minW(minW), minH(minH), maxW(maxW), maxH(maxH) {}

    // Tight: exact size required.
    static constexpr Constraints tight(float w, float h) { return {w, h, w, h}; }
    static constexpr Constraints tight(Size s) { return {s.w, s.h, s.w, s.h}; }

    // Loose: up to a maximum.
    static constexpr Constraints loose(float maxW, float maxH) { return {0, 0, maxW, maxH}; }
    static constexpr Constraints loose(Size s) { return {0, 0, s.w, s.h}; }

    // Unbounded: no limits.
    static constexpr Constraints unbounded() { return {0, 0, 1e6f, 1e6f}; }

    // Clamp a size to satisfy these constraints.
    constexpr Size constrain(Size s) const {
        return {detail::cclamp(s.w, minW, maxW), detail::cclamp(s.h, minH, maxH)};
    }

    constexpr bool isTight() const { return minW == maxW && minH == maxH; }
    constexpr bool isUnbounded() const { return maxW >= 1e6f || maxH >= 1e6f; }

    // Shrink constraints by insets (for padding).
    constexpr Constraints deflate(const Insets& i) const {
        float hw = i.horizontal(), vw = i.vertical();
        return {detail::cmax(0.0f, minW - hw), detail::cmax(0.0f, minH - vw),
                detail::cmax(0.0f, maxW - hw), detail::cmax(0.0f, maxH - vw)};
    }
};

} // namespace fw
} // namespace ui
} // namespace yawn
