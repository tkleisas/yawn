#pragma once
// BrowserPresetsTab — Presets tab for the BrowserPanel (fw2::Widget).
// Provides a grouped preset list backed by LibraryDatabase.
//
// Migrated from v1 plain class to a proper fw2::Widget. Bounds come
// from m_bounds after layout(); parent BrowserPanel drives measure/
// layout/render each frame.

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
#include <SDL3/SDL_keycode.h>  // SDLK_* — forwarded key ints from App.cpp
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

struct PresetListEntry {
    std::string name;
    std::string deviceId;
    std::string deviceName;
    std::string path;
    std::string genre;
    std::string instrument;
    bool        isHeader = false;
};

class BrowserPresetsTab : public Widget {
public:
    BrowserPresetsTab() {
        m_searchInput.setPlaceholder("Search presets...");
        m_searchInput.setOnCommit([this](const std::string&) { doSearch(); });
        m_searchInput.setOnChange([this](const std::string&) { doSearch(); });
        m_filterDropdown.setItems(std::vector<std::string>{"All", "Instruments", "Effects"});
        m_filterDropdown.setSelectedIndex(0);
        m_filterDropdown.setOnChange([this](int, const std::string&) { refreshList(); });
        Tooltip::attach(&m_filterDropdown, "Filter presets by category");
        setFocusable(false);
    }

    void setDatabase(library::LibraryDatabase* db) { m_db = db; }
    void setOnPresetDoubleClick(std::function<void(const std::string& path, const std::string& deviceId)> cb) {
        m_onPresetDoubleClick = std::move(cb);
    }

    void refreshList() {
        if (!m_db) return;
        m_entries.clear();
        m_searching = false;

        auto presets = m_db->getFilteredPresets(currentFilterType());
        buildGroupedList(presets);
        m_scrollOffset = 0;
        m_selectedIndex = -1;
    }

    // ─── Editing / keyboard-forward API (App.cpp passes SDL keycodes) ───

    bool isSearchEditing() const { return m_searchInput.isEditing(); }
    bool forwardKeyDown(int key) {
        if (key == SDLK_ESCAPE) {
            m_searchInput.endEdit(/*commit*/false);
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
    void cancelEditing() {
        m_searchInput.endEdit(/*commit*/false);
    }

protected:
    // ─── fw2 Widget overrides ───────────────────────────────────────────

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

        // Search input — dispatch to fw2 widget; enters edit mode on
        // down + short-circuits gesture SM (no capture, no paired up).
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

        // Filter dropdown — call toggle() directly (bypass gesture SM)
        // because v1 App doesn't route paired mouseUp through v2 capture.
        // See the v1 file's comment for the detailed rationale; same
        // constraint still applies because the BrowserPanel is wrapped
        // by BrowserPanelWrapper in the v1 rootLayout.
        {
            const auto& b = m_filterDropdown.bounds();
            if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h) {
                if (e.button == MouseButton::Left) {
                    m_filterDropdown.toggle();
                } else if (e.button == MouseButton::Right) {
                    // Demo context menu — set category directly.
                    ContextMenu::show({
                        Menu::header("Filter"),
                        Menu::radio("filter", "All",         m_filterDropdown.selectedIndex() == 0,
                                    [this]{ m_filterDropdown.setSelectedIndex(0, ValueChangeSource::User); }),
                        Menu::radio("filter", "Instruments", m_filterDropdown.selectedIndex() == 1,
                                    [this]{ m_filterDropdown.setSelectedIndex(1, ValueChangeSource::User); }),
                        Menu::radio("filter", "Effects",     m_filterDropdown.selectedIndex() == 2,
                                    [this]{ m_filterDropdown.setSelectedIndex(2, ValueChangeSource::User); }),
                        Menu::separator(),
                        Menu::submenu("Quick actions", {
                            Menu::item("Refresh list", [this]{ refreshList(); }, "F5"),
                            Menu::item("Clear search", [this]{ m_searchInput.setText(""); refreshList(); }),
                        }),
                    }, Point{e.x, e.y});
                }
                return true;
            }
        }

        // List area
        const float listY = y + 28;
        const float listH = h - 28 - kInfoAreaH;
        if (my < listY || my >= listY + listH) return false;

        const float rowH = kRowHeight;
        int row = static_cast<int>((my - listY) / rowH) + m_scrollOffset;
        if (row < 0 || row >= static_cast<int>(m_entries.size())) return false;

        auto& entry = m_entries[row];
        if (entry.isHeader) return true; // clicking headers does nothing

        // Double-click detection
        auto now = std::chrono::steady_clock::now();
        bool isDoubleClick = (row == m_lastClickRow &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastClickTime).count() < 400);
        m_lastClickRow = row;
        m_lastClickTime = now;

        m_selectedIndex = row;

        if (isDoubleClick && m_onPresetDoubleClick) {
            m_onPresetDoubleClick(entry.path, entry.deviceId);
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

        // Search input + filter dropdown (laid out in onLayout).
        m_searchInput.render(ctx);
        m_filterDropdown.render(ctx);

        const float listY = y + 28;
        const float infoH = kInfoAreaH;
        const float listH = h - 28 - infoH;
        if (listH <= 0) return;

        int totalRows = static_cast<int>(m_entries.size());

        if (totalRows == 0) {
            const char* msg = "No presets found";
            float tw = tm.textWidth(msg, fs);
            tm.drawText(r, msg, x + (w - tw) * 0.5f, listY + listH * 0.4f, fs,
                        ::yawn::ui::Theme::textDim);
            return;
        }

        // Clamp scroll
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
                tm.drawText(r, entry.deviceName, x + 8, textY, fs,
                            Color{140, 170, 210, 255});
            } else {
                if (idx == m_selectedIndex)
                    r.drawRect(x, ry, w, rowH, Color{50, 60, 80, 255});
                else if (i % 2 == 1)
                    r.drawRect(x, ry, w, rowH, Color{36, 36, 40, 255});

                float textY = ry + (rowH - lh) * 0.5f;
                tm.drawText(r, entry.name, x + 16, textY, fs,
                            ::yawn::ui::Theme::textPrimary);

                if (!entry.genre.empty() || !entry.instrument.empty()) {
                    std::string tags;
                    if (!entry.genre.empty()) tags = entry.genre;
                    if (!entry.instrument.empty()) {
                        if (!tags.empty()) tags += " | ";
                        tags += entry.instrument;
                    }
                    float tw = tm.textWidth(tags, fsSmall);
                    tm.drawText(r, tags, x + w - tw - 8, textY, fsSmall,
                                ::yawn::ui::Theme::textDim);
                }
            }
        }

        // Scrollbar
        if (totalRows > visibleRows) {
            float sbH = listH;
            float thumbFrac = static_cast<float>(visibleRows) / static_cast<float>(totalRows);
            float thumbH = std::max(8.0f, sbH * thumbFrac);
            float scrollFrac = (maxScroll > 0) ? static_cast<float>(m_scrollOffset) / maxScroll : 0.0f;
            float thumbY = listY + scrollFrac * (sbH - thumbH);
            r.drawRect(x + w - 4, thumbY, 3, thumbH, Color{80, 80, 90, 255});
        }

        r.popClip();

        // Info area at bottom
        float infoY = listY + listH;
        r.drawRect(x, infoY, w, infoH, Color{35, 35, 40, 255});
        r.drawRect(x, infoY, w, 1, Color{55, 55, 62, 255});

        if (m_selectedIndex >= 0 && m_selectedIndex < static_cast<int>(m_entries.size())) {
            auto& sel = m_entries[m_selectedIndex];
            if (!sel.isHeader) {
                float ty = infoY + 4;
                tm.drawText(r, sel.name, x + 8, ty, fs,
                            ::yawn::ui::Theme::textPrimary);
                ty += lh + 2;
                tm.drawText(r, sel.deviceName, x + 8, ty, fsSmall,
                            Color{140, 170, 210, 255});
                if (!sel.genre.empty()) {
                    ty += lhSmall + 1;
                    std::string label = "Genre: " + sel.genre;
                    tm.drawText(r, label, x + 8, ty, fsSmall,
                                ::yawn::ui::Theme::textDim);
                }
                if (!sel.instrument.empty()) {
                    ty += lhSmall + 1;
                    std::string label = "Instrument: " + sel.instrument;
                    tm.drawText(r, label, x + 8, ty, fsSmall,
                                ::yawn::ui::Theme::textDim);
                }
            }
        }
        // Dropdown popup paints via LayerStack (App render loop).
    }

private:
    static constexpr float kRowHeight   = 18.0f;
    static constexpr float kInfoAreaH   = 60.0f;

    library::LibraryDatabase* m_db = nullptr;

    FwTextInput m_searchInput;
    FwDropDown  m_filterDropdown;

    std::vector<PresetListEntry> m_entries;
    bool m_searching = false;

    int m_scrollOffset = 0;
    int m_selectedIndex = -1;

    int m_lastClickRow = -1;
    std::chrono::steady_clock::time_point m_lastClickTime;

    std::function<void(const std::string&, const std::string&)> m_onPresetDoubleClick;

    std::string currentFilterType() const {
        switch (m_filterDropdown.selectedIndex()) {
            case 1: return "instrument";
            case 2: return "effect";
            default: return "";
        }
    }

    void buildGroupedList(const std::vector<library::PresetRecord>& presets) {
        m_entries.clear();
        std::string lastDevice;
        for (auto& p : presets) {
            if (p.deviceName != lastDevice) {
                PresetListEntry header;
                header.deviceName = p.deviceName.empty() ? p.deviceId : p.deviceName;
                header.deviceId = p.deviceId;
                header.isHeader = true;
                m_entries.push_back(std::move(header));
                lastDevice = p.deviceName;
            }
            PresetListEntry entry;
            entry.name       = p.name;
            entry.deviceId   = p.deviceId;
            entry.deviceName = p.deviceName;
            entry.path       = p.path;
            entry.genre      = p.genre;
            entry.instrument = p.instrument;
            m_entries.push_back(std::move(entry));
        }
    }

    void doSearch() {
        std::string query = m_searchInput.text();
        if (query.empty()) {
            m_searching = false;
            refreshList();
            return;
        }
        if (!m_db) return;

        m_searching = true;
        auto results = m_db->searchPresets(query);
        const std::string filter = currentFilterType();
        if (!filter.empty()) {
            results.erase(
                std::remove_if(results.begin(), results.end(),
                    [&](const library::PresetRecord& p) {
                        return p.deviceType != filter;
                    }),
                results.end());
        }
        buildGroupedList(results);
        m_scrollOffset = 0;
    }
};

} // namespace fw2
} // namespace ui
} // namespace yawn
