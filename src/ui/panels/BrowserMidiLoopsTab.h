#pragma once
// BrowserMidiLoopsTab — MIDI Loops tab for the BrowserPanel
// (fw2::Widget). Grouped, category-filtered list of MIDI loops backed
// by LibraryDatabase. Mirrors BrowserPresetsTab's structure; the data
// model is MidiLoopRecord instead of PresetRecord.
//
// Double-click loads the loop into the selected MIDI slot; a drag-start
// hook lets App begin a drag-to-slot gesture (wired in App.cpp).

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/TextInput.h"
#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Renderer.h"
#include "ui/Theme.h"
#include "library/LibraryDatabase.h"
#include <SDL3/SDL_keycode.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

struct MidiLoopListEntry {
    std::string name;
    std::string category;
    std::string path;
    double      lengthBeats = 0;
    double      tempoBPM    = 0;
    int         noteCount   = 0;
    int         timeSigNum  = 4;
    bool        isHeader    = false;
    std::string headerLabel;   // category label for header rows
};

class BrowserMidiLoopsTab : public Widget {
public:
    BrowserMidiLoopsTab() {
        m_searchInput.setPlaceholder("Search loops...");
        m_searchInput.setOnCommit([this](const std::string&) { doSearch(); });
        m_searchInput.setOnChange([this](const std::string&) { doSearch(); });
        m_filterDropdown.setItems(std::vector<std::string>{
            "All", "Drums", "Bass", "Lead", "Chord", "Misc"});
        m_filterDropdown.setSelectedIndex(0);
        m_filterDropdown.setOnChange([this](int, const std::string&) { refreshList(); });
        Tooltip::attach(&m_filterDropdown, "Filter loops by category");
        setFocusable(false);
    }

    void setDatabase(library::LibraryDatabase* db) { m_db = db; }
    // Double-click → load into the selected slot.
    void setOnLoopDoubleClick(std::function<void(const std::string& path)> cb) {
        m_onLoopDoubleClick = std::move(cb);
    }
    // Mouse-down on a row → App may begin a drag-to-slot gesture.
    void setOnLoopDragStart(std::function<void(const std::string& path)> cb) {
        m_onLoopDragStart = std::move(cb);
    }

    void refreshList() {
        if (!m_db) return;
        m_entries.clear();
        m_searching = false;
        const std::string cat = currentCategory();
        auto loops = cat.empty() ? m_db->getAllMidiLoops()
                                 : m_db->getMidiLoopsByCategory(cat);
        buildGroupedList(loops, /*grouped*/ cat.empty());
        m_scrollOffset = 0;
        m_selectedIndex = -1;
    }

    // ─── Editing / keyboard-forward API (App.cpp passes SDL keycodes) ───
    bool isSearchEditing() const { return m_searchInput.isEditing(); }
    bool forwardKeyDown(int key) {
        if (key == SDLK_ESCAPE) {
            m_searchInput.endEdit(false);
            m_searchInput.setText("");
            m_searching = false;
            refreshList();
            return true;
        }
        KeyEvent ke;
        switch (static_cast<SDL_Keycode>(key)) {
            case SDLK_RETURN:    ke.key = Key::Enter;     break;
            case SDLK_BACKSPACE: ke.key = Key::Backspace; break;
            case SDLK_DELETE:    ke.key = Key::Delete;    break;
            case SDLK_HOME:      ke.key = Key::Home;      break;
            case SDLK_END:       ke.key = Key::End;       break;
            case SDLK_LEFT:      ke.key = Key::Left;      break;
            case SDLK_RIGHT:     ke.key = Key::Right;     break;
            default: return false;
        }
        return m_searchInput.dispatchKeyDown(ke);
    }
    bool forwardTextInput(const char* text) {
        m_searchInput.takeTextInput(text ? text : "");
        return true;
    }
    void cancelEditing() { m_searchInput.endEdit(false); }

protected:
    Size onMeasure(Constraints c, UIContext&) override {
        return c.constrain({c.maxW, c.maxH});
    }

    void onLayout(Rect bounds, UIContext& ctx) override {
        m_bounds = bounds;
        const float x = bounds.x, y = bounds.y, w = bounds.w;
        m_searchInput.measure(Constraints::tight(w - 80, 20), ctx);
        m_searchInput.layout(Rect{x + 4, y + 4, w - 80, 20}, ctx);
        m_filterDropdown.measure(Constraints::tight(68, 20), ctx);
        m_filterDropdown.layout(Rect{x + w - 72, y + 4, 68, 20}, ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const float mx = e.x, my = e.y;
        const float y = m_bounds.y;
        const float h = m_bounds.h;

        // Search input.
        {
            const auto& b = m_searchInput.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                MouseEvent se = e;
                se.lx = mx - b.x;
                se.ly = my - b.y;
                m_searchInput.dispatchMouseDown(se);
                return true;
            }
        }
        // Filter dropdown.
        {
            const auto& b = m_filterDropdown.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                if (e.button == MouseButton::Left) m_filterDropdown.toggle();
                return true;
            }
        }

        // List area.
        const float listY = y + 28;
        const float listH = h - 28 - kInfoAreaH;
        if (my < listY || my >= listY + listH) return false;

        int row = static_cast<int>((my - listY) / kRowHeight) + m_scrollOffset;
        if (row < 0 || row >= static_cast<int>(m_entries.size())) return false;

        auto& entry = m_entries[row];
        if (entry.isHeader) return true;

        auto now = std::chrono::steady_clock::now();
        bool isDoubleClick = (row == m_lastClickRow &&
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - m_lastClickTime).count() < 400);
        m_lastClickRow = row;
        m_lastClickTime = now;
        m_selectedIndex = row;

        if (isDoubleClick) {
            if (m_onLoopDoubleClick) m_onLoopDoubleClick(entry.path);
        } else {
            // Single click on a row arms a potential drag-to-slot.
            if (m_onLoopDragStart) m_onLoopDragStart(entry.path);
        }
        return true;
    }

    bool onScroll(ScrollEvent& e) override {
        m_scrollOffset -= static_cast<int>(e.dy * 3);
        if (m_scrollOffset < 0) m_scrollOffset = 0;
        return true;
    }

public:
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer || !ctx.textMetrics) return;
        auto& r  = *ctx.renderer;
        auto& tm = *ctx.textMetrics;

        const float x = m_bounds.x, y = m_bounds.y;
        const float w = m_bounds.w, h = m_bounds.h;
        const float fs      = theme().metrics.fontSizeSmall;
        const float fsSmall = fs * 0.9f;

        m_searchInput.render(ctx);
        m_filterDropdown.render(ctx);

        const float listY = y + 28;
        const float infoH = kInfoAreaH;
        const float listH = h - 28 - infoH;
        if (listH <= 0) return;

        int totalRows = static_cast<int>(m_entries.size());
        if (totalRows == 0) {
            const char* msg = "No MIDI loops found";
            float tw = tm.textWidth(msg, fs);
            tm.drawText(r, msg, x + (w - tw) * 0.5f, listY + listH * 0.4f, fs,
                        ::yawn::ui::Theme::textDim);
            return;
        }

        const float rowH = kRowHeight;
        int visibleRows = static_cast<int>(listH / rowH);
        int maxScroll = std::max(0, totalRows - visibleRows);
        if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;

        r.pushClip(x, listY, w, listH);
        const float lh      = tm.lineHeight(fs);
        const float lhSmall = tm.lineHeight(fsSmall);
        for (int i = 0; i < visibleRows + 1 && (i + m_scrollOffset) < totalRows; ++i) {
            int idx = i + m_scrollOffset;
            auto& entry = m_entries[idx];
            float ry = listY + i * rowH;

            if (entry.isHeader) {
                r.drawRect(x, ry, w, rowH, Color{38, 38, 44, 255});
                r.drawRect(x, ry + rowH - 1, w, 1, Color{55, 55, 62, 255});
                float textY = ry + (rowH - lh) * 0.5f;
                tm.drawText(r, entry.headerLabel, x + 8, textY, fs,
                            Color{140, 170, 210, 255});
            } else {
                if (idx == m_selectedIndex)
                    r.drawRect(x, ry, w, rowH, Color{50, 60, 80, 255});
                else if (i % 2 == 1)
                    r.drawRect(x, ry, w, rowH, Color{36, 36, 40, 255});

                float textY = ry + (rowH - lh) * 0.5f;
                tm.drawText(r, entry.name, x + 16, textY, fs,
                            ::yawn::ui::Theme::textPrimary);

                // Right-aligned bar count, e.g. "4 bars".
                int beatsPerBar = entry.timeSigNum > 0 ? entry.timeSigNum : 4;
                double bars = entry.lengthBeats / static_cast<double>(beatsPerBar);
                char info[24];
                if (std::fabs(bars - std::round(bars)) < 0.05)
                    std::snprintf(info, sizeof(info), "%d bars",
                                  static_cast<int>(std::lround(bars)));
                else
                    std::snprintf(info, sizeof(info), "%.1f bars", bars);
                float iw = tm.textWidth(info, fsSmall);
                tm.drawText(r, info, x + w - iw - 8, textY, fsSmall,
                            ::yawn::ui::Theme::textDim);
            }
        }

        if (totalRows > visibleRows) {
            float sbH = listH;
            float thumbFrac = static_cast<float>(visibleRows) / static_cast<float>(totalRows);
            float thumbH = std::max(8.0f, sbH * thumbFrac);
            float scrollFrac = (maxScroll > 0) ? static_cast<float>(m_scrollOffset) / maxScroll : 0.0f;
            float thumbY = listY + scrollFrac * (sbH - thumbH);
            r.drawRect(x + w - 4, thumbY, 3, thumbH, Color{80, 80, 90, 255});
        }
        r.popClip();

        // Info area.
        float infoY = listY + listH;
        r.drawRect(x, infoY, w, infoH, Color{35, 35, 40, 255});
        r.drawRect(x, infoY, w, 1, Color{55, 55, 62, 255});
        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_entries.size())) {
            auto& sel = m_entries[m_selectedIndex];
            if (!sel.isHeader) {
                float ty = infoY + 4;
                tm.drawText(r, sel.name, x + 8, ty, fs, ::yawn::ui::Theme::textPrimary);
                ty += lh + 2;
                std::string catLine = sel.category;
                if (!catLine.empty()) catLine[0] = static_cast<char>(std::toupper(
                    static_cast<unsigned char>(catLine[0])));
                tm.drawText(r, catLine, x + 8, ty, fsSmall, Color{140, 170, 210, 255});
                ty += lhSmall + 1;
                char det[64];
                int bpb = sel.timeSigNum > 0 ? sel.timeSigNum : 4;
                double bars = sel.lengthBeats / static_cast<double>(bpb);
                if (sel.tempoBPM > 0)
                    std::snprintf(det, sizeof(det), "%.1f bars  -  %d notes  -  %.0f BPM",
                                  bars, sel.noteCount, sel.tempoBPM);
                else
                    std::snprintf(det, sizeof(det), "%.1f bars  -  %d notes",
                                  bars, sel.noteCount);
                tm.drawText(r, det, x + 8, ty, fsSmall, ::yawn::ui::Theme::textDim);
            }
        }
    }

private:
    static constexpr float kRowHeight = 18.0f;
    static constexpr float kInfoAreaH = 60.0f;

    library::LibraryDatabase* m_db = nullptr;
    FwTextInput m_searchInput;
    FwDropDown  m_filterDropdown;

    std::vector<MidiLoopListEntry> m_entries;
    bool m_searching = false;
    int  m_scrollOffset = 0;
    int  m_selectedIndex = -1;
    int  m_lastClickRow = -1;
    std::chrono::steady_clock::time_point m_lastClickTime;

    std::function<void(const std::string&)> m_onLoopDoubleClick;
    std::function<void(const std::string&)> m_onLoopDragStart;

    std::string currentCategory() const {
        switch (m_filterDropdown.selectedIndex()) {
            case 1: return "drums";
            case 2: return "bass";
            case 3: return "lead";
            case 4: return "chord";
            case 5: return "misc";
            default: return "";  // All
        }
    }

    static std::string capitalize(std::string s) {
        if (!s.empty()) s[0] = static_cast<char>(std::toupper(
            static_cast<unsigned char>(s[0])));
        return s;
    }

    void buildGroupedList(const std::vector<library::MidiLoopRecord>& loops, bool grouped) {
        m_entries.clear();
        std::string lastCat;
        for (auto& l : loops) {
            if (grouped && l.category != lastCat) {
                MidiLoopListEntry header;
                header.isHeader    = true;
                header.headerLabel = capitalize(l.category.empty() ? "misc" : l.category);
                m_entries.push_back(std::move(header));
                lastCat = l.category;
            }
            MidiLoopListEntry e;
            e.name        = l.name;
            e.category    = l.category;
            e.path        = l.path;
            e.lengthBeats = l.lengthBeats;
            e.tempoBPM    = l.tempoBPM;
            e.noteCount   = l.noteCount;
            e.timeSigNum  = l.timeSigNum;
            m_entries.push_back(std::move(e));
        }
    }

    void doSearch() {
        std::string query = m_searchInput.text();
        if (query.empty()) { m_searching = false; refreshList(); return; }
        if (!m_db) return;
        m_searching = true;
        auto results = m_db->searchMidiLoops(query);
        const std::string cat = currentCategory();
        if (!cat.empty()) {
            results.erase(std::remove_if(results.begin(), results.end(),
                [&](const library::MidiLoopRecord& l) { return l.category != cat; }),
                results.end());
        }
        buildGroupedList(results, /*grouped*/ true);
        m_scrollOffset = 0;
    }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
