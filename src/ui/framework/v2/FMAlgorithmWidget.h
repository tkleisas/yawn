#pragma once
// fw2::FMAlgorithmWidget — FM synth algorithm preview.
// Migrated from v1 fw::FMAlgorithmWidget. Used inside the FM Synth
// display panel — draws 4 operators (carriers green, modulators orange)
// and their modulation / output / feedback connections.

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Theme.h"

#ifndef YAWN_TEST_BUILD
#include "ui/Renderer.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace yawn {
namespace ui {
namespace fw2 {

class FMAlgorithmWidget : public Widget {
public:
    FMAlgorithmWidget() { setName("FMAlgorithmWidget"); }

    void setAlgorithm(int algo) { m_algorithm = std::clamp(algo, 0, 7); }
    void setFeedback(float fb)  { m_feedback = std::clamp(fb, 0.0f, 1.0f); }
    void setOpLevels(float o1, float o2, float o3, float o4) {
        m_opLevel[0] = o1; m_opLevel[1] = o2;
        m_opLevel[2] = o3; m_opLevel[3] = o4;
    }

    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, 60.0f});
    }

#ifdef YAWN_TEST_BUILD
    void render(UIContext&) override {}
#else
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer) return;
        auto& r = *ctx.renderer;

        r.drawRect(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                   Color{22, 22, 28, 255});
        r.drawRectOutline(m_bounds.x, m_bounds.y, m_bounds.w, m_bounds.h,
                          Color{50, 50, 60, 255});

        auto* tm = ctx.textMetrics;
        const float lblFs = 8.0f * (48.0f / 26.0f);   // ≈ 14.77 px
        char algoText[16];
        std::snprintf(algoText, sizeof(algoText), "Algo %d", m_algorithm + 1);
        if (tm)
            tm->drawText(r, algoText, m_bounds.x + 4, m_bounds.y + 2, lblFs,
                          Color{180, 180, 200, 200});

        drawAlgorithm(r, tm);
    }
#endif

private:
    int   m_algorithm = 0;
    float m_feedback  = 0.2f;
    float m_opLevel[4] = {1.0f, 0.8f, 0.6f, 0.4f};

    struct AlgoDef { bool mod[4][4]; bool carrier[4]; };
    static constexpr AlgoDef kAlgos[8] = {
        // 0: Serial 4→3→2→1(C)
        {{{0,1,0,0},{0,0,1,0},{0,0,0,1},{0,0,0,0}}, {1,0,0,0}},
        // 1: (3+4)→2→1(C)
        {{{0,1,0,0},{0,0,1,1},{0,0,0,0},{0,0,0,0}}, {1,0,0,0}},
        // 2: 2→1(C), 4→3(C)
        {{{0,1,0,0},{0,0,0,0},{0,0,0,1},{0,0,0,0}}, {1,0,1,0}},
        // 3: (2+3+4)→1(C)
        {{{0,1,1,1},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,0,0,0}},
        // 4: All carriers (additive)
        {{{0,0,0,0},{0,0,0,0},{0,0,0,0},{0,0,0,0}}, {1,1,1,1}},
        // 5: 4→3→1(C), 2→1(C)
        {{{0,1,1,0},{0,0,0,0},{0,0,0,1},{0,0,0,0}}, {1,0,0,0}},
        // 6: 4→(1+2+3)(C)
        {{{0,0,0,1},{0,0,0,1},{0,0,0,1},{0,0,0,0}}, {1,1,1,0}},
        // 7: 3→1(C), 4→2(C)
        {{{0,0,1,0},{0,0,0,1},{0,0,0,0},{0,0,0,0}}, {1,1,0,0}},
    };

#ifndef YAWN_TEST_BUILD
    void drawAlgorithm(::yawn::ui::Renderer2D& r, TextMetrics* tm) {
        const auto& algo = kAlgos[m_algorithm];
        const float areaX = m_bounds.x + 4;
        const float areaY = m_bounds.y + 14;
        const float areaW = m_bounds.w - 8;
        const float areaH = m_bounds.h - 28;
        if (areaW < 40 || areaH < 20) return;

        constexpr float boxW = 28, boxH = 22;
        float opX[4], opY[4];
        layoutOperators(algo, areaX, areaY, areaW, areaH, boxW, boxH, opX, opY);

        const Color connCol{220, 150, 50, 200};
        for (int dest = 0; dest < 4; ++dest)
            for (int src = 0; src < 4; ++src)
                if (algo.mod[dest][src])
                    drawArrow(r, opX[src] + boxW / 2, opY[src] + boxH,
                                 opX[dest] + boxW / 2, opY[dest], connCol);

        const Color outCol{70, 210, 90, 200};
        for (int op = 0; op < 4; ++op) {
            if (!algo.carrier[op]) continue;
            const float cx = opX[op] + boxW / 2;
            const float by = opY[op] + boxH;
            const float endY = std::min(by + 10, m_bounds.y + m_bounds.h - 4);
            for (float py = by + 1; py < endY; py += 2)
                r.drawRect(cx - 0.5f, py, 2.0f, 1.5f, outCol);
            r.drawTriangle(cx - 3, endY - 1, cx + 3, endY - 1, cx, endY + 2, outCol);
        }

        if (m_feedback > 0.01f) {
            const Color fbCol{255, 200, 60, 200};
            const float rx = opX[3] + boxW + 2;
            const float ry = opY[3] + boxH * 0.5f;
            for (int s = 0; s < 5; ++s)
                r.drawRect(rx + s * 1.5f, ry - s * 2.0f, 2.0f, 2.0f, fbCol);
            const float topY = ry - 10;
            for (float px = rx + 6; px >= opX[3] + boxW * 0.7f; px -= 2)
                r.drawRect(px, topY, 2.0f, 2.0f, fbCol);
            r.drawTriangle(opX[3] + boxW * 0.7f - 1, topY - 2,
                           opX[3] + boxW * 0.7f - 1, topY + 4,
                           opX[3] + boxW * 0.7f - 4, topY + 1, fbCol);
        }

        const float numFs = 10.0f * (48.0f / 26.0f);  // ≈ 18.46 px
        const float numH = tm ? tm->lineHeight(numFs) : numFs;
        for (int op = 0; op < 4; ++op) {
            const bool isCarrier = algo.carrier[op];
            const float lvl = m_opLevel[op];
            const Color boxBg = isCarrier
                ? Color{static_cast<uint8_t>(25 + lvl * 45), static_cast<uint8_t>(80 + lvl * 40), static_cast<uint8_t>(40 + lvl * 25), 255}
                : Color{static_cast<uint8_t>(75 + lvl * 40), static_cast<uint8_t>(50 + lvl * 30), static_cast<uint8_t>(20 + lvl * 20), 255};
            r.drawRoundedRect(opX[op], opY[op], boxW, boxH, 3.0f, boxBg);
            const Color border = isCarrier ? Color{80, 220, 100, 230} : Color{230, 160, 60, 230};
            r.drawRectOutline(opX[op], opY[op], boxW, boxH, border);

            char label[4];
            std::snprintf(label, sizeof(label), "%d", op + 1);
            if (tm) {
                const float tw = tm->textWidth(label, numFs);
                const float tx = opX[op] + (boxW - tw) * 0.5f;
                const float ty = opY[op] + (boxH - numH) * 0.5f - numH * 0.1f;
                tm->drawText(r, label, tx, ty, numFs, Color{240, 240, 255, 240});
            }
        }

        const float outLblFs = 7.0f * (48.0f / 26.0f);  // ≈ 12.92 px
        if (tm) {
            const float outTw = tm->textWidth("OUT", outLblFs);
            tm->drawText(r, "OUT", areaX + (areaW - outTw) / 2,
                          m_bounds.y + m_bounds.h - 12, outLblFs, outCol);
        }
    }

    void layoutOperators(const AlgoDef& algo, float areaX, float areaY,
                         float areaW, float areaH, float boxW, float boxH,
                         float* opX, float* opY) {
        int depth[4];
        for (int op = 0; op < 4; ++op)
            depth[op] = algo.carrier[op] ? 0 : -1;

        for (int pass = 0; pass < 4; ++pass)
            for (int dest = 0; dest < 4; ++dest)
                for (int src = 0; src < 4; ++src)
                    if (algo.mod[dest][src] && depth[dest] >= 0)
                        if (depth[src] < depth[dest] + 1)
                            depth[src] = depth[dest] + 1;

        for (int op = 0; op < 4; ++op)
            if (depth[op] < 0) depth[op] = 1;

        int maxDepth = 0;
        for (int op = 0; op < 4; ++op)
            if (depth[op] > maxDepth) maxDepth = depth[op];

        int countAt[5] = {}, idxAt[5] = {};
        for (int op = 0; op < 4; ++op) countAt[depth[op]]++;

        const float vGap = maxDepth > 0
            ? std::min((areaH - boxH) / maxDepth, boxH + 8.0f)
            : 0.0f;
        const float hGap = 8.0f;

        for (int op = 0; op < 4; ++op) {
            int d = depth[op];
            int cnt = countAt[d];
            int idx = idxAt[d]++;
            const float totalW = cnt * boxW + (cnt - 1) * hGap;
            const float startX = areaX + (areaW - totalW) / 2;
            opX[op] = startX + idx * (boxW + hGap);
            opY[op] = areaY + (maxDepth - d) * vGap;
        }
    }

    void drawArrow(::yawn::ui::Renderer2D& r,
                   float x1, float y1, float x2, float y2, Color col) {
        const float dx = x2 - x1, dy = y2 - y1;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 2) return;
        const int steps = std::max(3, static_cast<int>(len / 2.0f));
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) / steps;
            r.drawRect(x1 + dx * t - 0.75f, y1 + dy * t - 0.75f, 2.0f, 2.0f, col);
        }
        r.drawTriangle(x2 - 3, y2 - 2, x2 + 3, y2 - 2, x2, y2 + 2, col);
    }
#endif
};

} // namespace fw2
} // namespace ui
} // namespace yawn
