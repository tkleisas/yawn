#pragma once
// fw2::InstrumentRackDisplayPanel — chain list / key+vel range view
// for the Instrument Rack. Migrated from v1
// fw::InstrumentRackDisplayPanel.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>

namespace yawn {
namespace ui {
namespace fw2 {

class InstrumentRackDisplayPanel : public Widget {
public:
    InstrumentRackDisplayPanel() { setName("InstrumentRackDisplay"); }

    struct ChainInfo {
        std::string name;
        uint8_t keyLow = 0, keyHigh = 127;
        uint8_t velLow = 1, velHigh = 127;
        float   volume = 1.0f;
        bool    enabled = true;
    };

    void setChainCount(int n)          { m_chainCount = std::clamp(n, 0, 8); }
    void setChain(int i, const ChainInfo& info) {
        if (i >= 0 && i < 8) m_chains[i] = info;
    }
    void setSelectedChain(int i)        { m_selected = std::clamp(i, 0, 7); }

    void setOnChainClick  (std::function<void(int)> cb) { m_onChainClick   = std::move(cb); }
    void setOnAddChain    (std::function<void()>    cb) { m_onAddChain     = std::move(cb); }
    void setOnRemoveChain (std::function<void(int)> cb) { m_onRemoveChain  = std::move(cb); }
    void setOnToggleChain (std::function<void(int)> cb) { m_onToggleChain  = std::move(cb); }
    // Right-click on a chain row — App uses this to surface the
    // per-chain fx context menu (add / remove effect entries),
    // mirroring DrumRack's setOnPadRightClick wiring.
    void setOnChainRightClick(
            std::function<void(int chainIdx, float sx, float sy)> cb) {
        m_onChainRightClick = std::move(cb);
    }

    Size onMeasure(Constraints c, UIContext&) override {
        // Bumped from 160 → 220 so the pad-grid cells and chain-list
        // labels are large enough to read without clipping (matches
        // the DrumRackDisplayPanel bump in v0.59.0).
        return c.constrain({c.maxW, 220.0f});
    }

    bool onMouseDown(MouseEvent& e) override {
        const float lx = e.x - m_bounds.x;
        const float ly = e.y - m_bounds.y;

        // Right-click on a chain row → surface the per-chain fx
        // context menu via the wired callback. Falls through if no
        // chain was hit (e.g. clicking the "Add Chain" button area).
        if (e.button == MouseButton::Right) {
            const float rowH = chainRowHeight();
            const float headerH = kHeaderH;
            const int idx = static_cast<int>((ly - headerH) / rowH);
            if (idx >= 0 && idx < m_chainCount && m_onChainRightClick) {
                m_onChainRightClick(idx, e.x, e.y);
                return true;
            }
            return false;
        }

        if (e.button != MouseButton::Left) return false;

        const float addBtnY = m_bounds.h - kAddBtnH;
        if (ly >= addBtnY && m_chainCount < 8) {
            if (m_onAddChain) m_onAddChain();
            return true;
        }

        const float rowH = chainRowHeight();
        const float headerH = 14.0f;
        const int idx = static_cast<int>((ly - headerH) / rowH);
        if (idx < 0 || idx >= m_chainCount) return false;

        if (lx < 16.0f) {
            if (m_onToggleChain) m_onToggleChain(idx);
            return true;
        }
        if (lx > m_bounds.w - 18.0f) {
            if (m_onRemoveChain) m_onRemoveChain(idx);
            return true;
        }
        if (m_onChainClick) m_onChainClick(idx);
        return true;
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;
        auto* tm = ctx.textMetrics;

        const float x = m_bounds.x, y = m_bounds.y, w = m_bounds.w, h = m_bounds.h;
        // Theme-driven font sizes so the chain rows read at the
        // user's font-scale preference instead of being stuck at the
        // hard-coded 7×(48/26)px from the v1 port.
        const float lblFs   = theme().metrics.fontSize;
        const float smallFs = theme().metrics.fontSizeSmall;

        r.drawRect(x, y, w, h, Color{30, 30, 35, 255});

        if (tm)
            tm->drawText(r, "Chains", x + 4, y + 2, lblFs,
                          Color{180, 180, 180, 255});
        const float headerH = kHeaderH;
        const float rowH = chainRowHeight();

        for (int i = 0; i < m_chainCount; ++i) {
            const float ry = y + headerH + i * rowH;
            const auto& ch = m_chains[i];

            const bool sel = (i == m_selected);
            r.drawRect(x, ry, w, rowH - 1,
                       sel ? Color{50, 55, 70, 255}
                            : Color{38, 38, 42, 255});

            const Color eDot = ch.enabled ? Color{80, 200, 100, 255}
                                           : Color{80, 80, 80, 255};
            const float dotCY = ry + rowH * 0.5f;
            r.drawRect(x + 4, dotCY - 3, 7, 7, eDot);

            const float lblLh = tm ? tm->lineHeight(lblFs) : lblFs;
            if (tm) {
                char label[64];
                std::snprintf(label, sizeof(label), "%d: %s",
                              i + 1, ch.name.c_str());
                tm->drawText(r, label, x + 16, ry + 2, lblFs,
                              Color{220, 220, 220, 255});
            }

            // Range bars sit BELOW the label baseline so the chain
            // name reads cleanly. The previous fixed `ry + 14` worked
            // for the v1 7-px font but overlaps the theme-scaled
            // label (~12-15 px line-height) — push the bars to
            // ry + lblLh + 2 instead so they always clear the label.
            const float barX = x + 16;
            const float barW = w - 36;
            float barY = ry + lblLh + 2.0f;
            const float barH = 4;
            r.drawRect(barX, barY, barW, barH, Color{25, 25, 30, 255});
            const float k0 = ch.keyLow / 127.0f, k1 = ch.keyHigh / 127.0f;
            const float kx0 = barX + k0 * barW;
            const float kx1 = barX + k1 * barW;
            r.drawRect(kx0, barY, std::max(1.0f, kx1 - kx0), barH,
                       Color{80, 140, 220, 255});

            barY += barH + 1;
            r.drawRect(barX, barY, barW, barH, Color{25, 25, 30, 255});
            const float v0 = ch.velLow / 127.0f, v1 = ch.velHigh / 127.0f;
            const float vx0 = barX + v0 * barW;
            const float vx1 = barX + v1 * barW;
            r.drawRect(vx0, barY, std::max(1.0f, vx1 - vx0), barH,
                       Color{220, 160, 50, 255});

            if (tm)
                tm->drawText(r, "x", x + w - 14, ry + 2, lblFs,
                              Color{160, 80, 80, 255});
        }

        if (m_chainCount == 0 && tm) {
            tm->drawText(r, "No chains", x + w * 0.5f - 24,
                          y + h * 0.5f - 6, lblFs,
                          Color{120, 120, 120, 255});
        }

        const float addY = y + h - kAddBtnH;
        const bool canAdd = m_chainCount < 8;
        const Color addCol = canAdd ? Color{80, 180, 80, 255}
                                     : Color{60, 60, 60, 255};
        r.drawRect(x + 2, addY, w - 4, kAddBtnH - 2,
                   Color{40, 40, 45, 255});
        if (tm) {
            const char* addTxt = "+ Add Chain";
            const float addTw = tm->textWidth(addTxt, smallFs);
            const float addTh = tm->lineHeight(smallFs);
            tm->drawText(r, addTxt, x + (w - addTw) * 0.5f,
                          addY + (kAddBtnH - addTh) * 0.5f,
                          smallFs, addCol);
        }
    }
#endif

private:
    float chainRowHeight() const {
        // Bumped row min/max to match the theme-scaled fonts — the
        // 7-px font under v1 fit in 20 px cleanly, the 12-15 px theme
        // font needs more vertical room or the label + range bars
        // overlap. Header / "Add Chain" footer heights bumped too.
        const float usable = m_bounds.h - kHeaderH - kAddBtnH;
        const int maxRows = std::max(m_chainCount, 1);
        return std::clamp(usable / maxRows, 32.0f, 44.0f);
    }
    // Bumped 18 → 24 so the "Chains" header label (theme-scaled
    // font, ~15-18 px tall) renders without clipping at the bottom.
    static constexpr float kHeaderH = 24.0f;
    static constexpr float kAddBtnH = 22.0f;

    int m_chainCount = 0;
    int m_selected = 0;
    ChainInfo m_chains[8];
    std::function<void(int)> m_onChainClick;
    std::function<void()>    m_onAddChain;
    std::function<void(int)> m_onRemoveChain;
    std::function<void(int)> m_onToggleChain;
    std::function<void(int /*chainIdx*/, float /*sx*/, float /*sy*/)>
        m_onChainRightClick;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
