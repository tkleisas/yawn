#pragma once
// BrowserPresetsTab — Presets tab for the BrowserPanel.
// Provides a grouped preset list backed by LibraryDatabase.

#include "ui/framework/Widget.h"
#include "ui/framework/Primitives.h"
#include "ui/framework/v2/ContextMenu.h"
#include "ui/framework/v2/DropDown.h"
#include "ui/framework/v2/Tooltip.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/Renderer.h"
#include "ui/Font.h"
#include "ui/Theme.h"
#include "library/LibraryDatabase.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw {

struct PresetListEntry {
    std::string name;
    std::string deviceId;
    std::string deviceName;
    std::string path;
    std::string genre;
    std::string instrument;
    bool        isHeader = false;
};

// NB: v1 panels hosting a single v2 widget like FwDropDown call the
// widget's high-level method (toggle/open/close) directly from their
// own onMouseDown, bypassing v2's gesture state machine. That's
// because App.cpp only routes mouseUp into the v1 tree when v1's
// Widget::capturedWidget() is non-null — v2's captureMouse() uses a
// separate slot, so v1 never re-enters here on release and the
// gesture state machine's click would never fire. A future v1→fw2
// event-translation helper lives in this file when a migration
// actually needs the full gesture pipeline.

class BrowserPresetsTab {
public:
    BrowserPresetsTab() {
        m_searchInput.setPlaceholder("Search presets...");
        m_searchInput.setOnCommit([this](const std::string&) { doSearch(); });
        // v2 FwDropDown — popup lives on fw2::LayerStack (App owns it),
        // so it paints above every panel and auto-dismisses on outside
        // click without explicit paintOverlay/hitPopup glue.
        m_filterDropdown.setItems(std::vector<std::string>{"All", "Instruments", "Effects"});
        m_filterDropdown.setSelectedIndex(0);
        m_filterDropdown.setOnChange([this](int, const std::string&) { refreshList(); });
        // First v2 tooltip on a real widget — hover the button to see
        // the bubble pop above/below.
        ::yawn::ui::fw2::Tooltip::attach(&m_filterDropdown, "Filter presets by category");
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

    // ── Events ──────────────────────────────────────────────────────────

    bool onMouseDown(MouseEvent& e, float x, float y, float w, float h, UIContext& ctx) {
        float mx = e.x, my = e.y;

        // Search input
        if (hitRect(mx, my, x + 4, y + 4, w - 80, 20)) {
            m_searchInput.onMouseDown(e);
            return true;
        }

        // Filter dropdown (closed button only — the open popup lives
        // on fw2::LayerStack and is handled before we see events).
        //
        // We call toggle() directly instead of going through the full
        // v2 gesture state machine via dispatchMouseDown + dispatchMouseUp.
        // Reason: v1's App.cpp only dispatches mouseUp into the widget
        // tree when Widget::capturedWidget() (v1's capture slot) is
        // non-null. v2's captureMouse() uses a separate slot, so v1
        // never routes the mouseUp back here — the gesture SM's click
        // never fires, toggle never runs. Direct toggle() matches
        // v1 FwDropDown behaviour exactly (open/close on mouse-down)
        // and is the right pattern for v1-hosted v2 widgets until we
        // have v2-native panels that do their own tree dispatch.
        float ddX = x + w - 72, ddY = y + 4;
        if (hitRect(mx, my, ddX, ddY, 68, 20)) {
            if (e.button == ::yawn::ui::fw::MouseButton::Left) {
                m_filterDropdown.toggle();
            } else if (e.button == ::yawn::ui::fw::MouseButton::Right) {
                // Demo v2 ContextMenu — right-click the filter to set
                // a preset category directly (bypasses the dropdown)
                // and exercise a submenu entry.
                namespace fw2 = ::yawn::ui::fw2;
                fw2::ContextMenu::show({
                    fw2::Menu::header("Filter"),
                    fw2::Menu::radio("filter", "All",         m_filterDropdown.selectedIndex() == 0,
                                      [this]{ m_filterDropdown.setSelectedIndex(0, fw2::ValueChangeSource::User); }),
                    fw2::Menu::radio("filter", "Instruments", m_filterDropdown.selectedIndex() == 1,
                                      [this]{ m_filterDropdown.setSelectedIndex(1, fw2::ValueChangeSource::User); }),
                    fw2::Menu::radio("filter", "Effects",     m_filterDropdown.selectedIndex() == 2,
                                      [this]{ m_filterDropdown.setSelectedIndex(2, fw2::ValueChangeSource::User); }),
                    fw2::Menu::separator(),
                    fw2::Menu::submenu("Quick actions", {
                        fw2::Menu::item("Refresh list", [this]{ refreshList(); }, "F5"),
                        fw2::Menu::item("Clear search", [this]{ m_searchInput.setText(""); refreshList(); }),
                    }),
                }, fw2::Point{e.x, e.y});
            }
            return true;
        }

        // List area
        float listY = y + 28;
        float listH = h - 28 - kInfoAreaH;
        if (my < listY || my >= listY + listH) return false;

        float rowH = kRowHeight;
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

    bool onScroll(ScrollEvent& e) {
        m_scrollOffset -= static_cast<int>(e.dy * 3);
        if (m_scrollOffset < 0) m_scrollOffset = 0;
        return true;
    }

    bool onMouseMove(MouseMoveEvent& e) {
        // v2 popup handles its own hover via LayerStack; the closed
        // button's hover state is paint-only, no translation needed.
        return m_searchInput.onMouseMove(e);
    }

    bool onMouseUp(MouseEvent& e) {
        // v2 dropdown toggles on mouse-down directly (no gesture SM
        // pipeline — see comment in onMouseDown) so there's nothing to
        // forward here.
        return m_searchInput.onMouseUp(e);
    }

    bool isSearchEditing() const { return m_searchInput.isEditing(); }
    bool forwardKeyDown(int key) {
        KeyEvent ke;
        ke.keyCode = key;
        if (key == 27) {
            m_searchInput.setText("");
            m_searching = false;
            refreshList();
        }
        return m_searchInput.onKeyDown(ke);
    }
    bool forwardTextInput(const char* text) {
        TextInputEvent te;
        std::strncpy(te.text, text, sizeof(te.text) - 1);
        te.text[sizeof(te.text) - 1] = '\0';
        bool handled = m_searchInput.onTextInput(te);
        doSearch();
        return handled;
    }
    void cancelEditing() {
        KeyEvent ke;
        ke.keyCode = 27;
        m_searchInput.onKeyDown(ke);
    }

    // ── Rendering ───────────────────────────────────────────────────────

    void paint(Renderer2D& r, Font& f, float x, float y, float w, float h, UIContext& ctx) {
        float scale = Theme::kSmallFontSize / f.pixelHeight() * 0.78f;
        float scaleSmall = scale * 0.9f;

        // Search input
        m_searchInput.layout(Rect{x + 4, y + 4, w - 80, 20}, ctx);
        m_searchInput.paint(ctx);

        // Filter dropdown — v2 widget. Popup is painted later by
        // LayerStack (App render loop), not here.
        auto& v2ctx = ::yawn::ui::fw2::UIContext::global();
        m_filterDropdown.layout(Rect{x + w - 72, y + 4, 68, 20}, v2ctx);
        m_filterDropdown.render(v2ctx);

        float listY = y + 28;
        float infoH = kInfoAreaH;
        float listH = h - 28 - infoH;
        if (listH <= 0) return;

        int totalRows = static_cast<int>(m_entries.size());

        if (totalRows == 0) {
            const char* msg = "No presets found";
            float tw = f.textWidth(msg, scale);
            f.drawText(r, msg, x + (w - tw) * 0.5f, listY + listH * 0.4f, scale, Theme::textDim);
            return;
        }

        // Clamp scroll
        float rowH = kRowHeight;
        int visibleRows = static_cast<int>(listH / rowH);
        int maxScroll = std::max(0, totalRows - visibleRows);
        if (m_scrollOffset > maxScroll) m_scrollOffset = maxScroll;

        r.pushClip(x, listY, w, listH);

        for (int i = 0; i < visibleRows + 1 && (i + m_scrollOffset) < totalRows; ++i) {
            int idx = i + m_scrollOffset;
            auto& entry = m_entries[idx];
            float ry = listY + i * rowH;

            if (entry.isHeader) {
                // Device group header
                r.drawRect(x, ry, w, rowH, Color{38, 38, 44, 255});
                r.drawRect(x, ry + rowH - 1, w, 1, Color{55, 55, 62, 255});
                float textY = ry + (rowH - f.lineHeight(scale)) * 0.5f;
                f.drawText(r, entry.deviceName.c_str(), x + 8, textY, scale, Color{140, 170, 210, 255});
            } else {
                // Preset row
                if (idx == m_selectedIndex)
                    r.drawRect(x, ry, w, rowH, Color{50, 60, 80, 255});
                else if (i % 2 == 1)
                    r.drawRect(x, ry, w, rowH, Color{36, 36, 40, 255});

                float textY = ry + (rowH - f.lineHeight(scale)) * 0.5f;
                f.drawText(r, entry.name.c_str(), x + 16, textY, scale, Theme::textPrimary);

                // Genre/instrument tags on the right
                if (!entry.genre.empty() || !entry.instrument.empty()) {
                    std::string tags;
                    if (!entry.genre.empty()) tags = entry.genre;
                    if (!entry.instrument.empty()) {
                        if (!tags.empty()) tags += " | ";
                        tags += entry.instrument;
                    }
                    float tw = f.textWidth(tags, scaleSmall);
                    f.drawText(r, tags.c_str(), x + w - tw - 8, textY, scaleSmall, Theme::textDim);
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
                f.drawText(r, sel.name.c_str(), x + 8, ty, scale, Theme::textPrimary);
                ty += f.lineHeight(scale) + 2;
                f.drawText(r, sel.deviceName.c_str(), x + 8, ty, scaleSmall, Color{140, 170, 210, 255});
                if (!sel.genre.empty()) {
                    ty += f.lineHeight(scaleSmall) + 1;
                    std::string label = "Genre: " + sel.genre;
                    f.drawText(r, label.c_str(), x + 8, ty, scaleSmall, Theme::textDim);
                }
                if (!sel.instrument.empty()) {
                    ty += f.lineHeight(scaleSmall) + 1;
                    std::string label = "Instrument: " + sel.instrument;
                    f.drawText(r, label.c_str(), x + 8, ty, scaleSmall, Theme::textDim);
                }
            }
        }

        // NB: no paintOverlay() here — v2 LayerStack paints the
        // dropdown popup above everything at end-of-frame (App.cpp).
    }

private:
    static constexpr float kRowHeight   = 18.0f;
    static constexpr float kInfoAreaH   = 60.0f;

    library::LibraryDatabase* m_db = nullptr;

    FwTextInput m_searchInput;
    ::yawn::ui::fw2::FwDropDown m_filterDropdown;

    std::vector<PresetListEntry> m_entries;
    bool m_searching = false;

    int m_scrollOffset = 0;
    int m_selectedIndex = -1;

    int m_lastClickRow = -1;
    std::chrono::steady_clock::time_point m_lastClickTime;

    std::function<void(const std::string&, const std::string&)> m_onPresetDoubleClick;

    static bool hitRect(float mx, float my, float rx, float ry, float rw, float rh) {
        return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh;
    }

    // Translate the filter dropdown's selection into a DB query token:
    //   0 → "" (all) — LibraryDatabase::getFilteredPresets returns all.
    //   1 → "instrument"
    //   2 → "effect"
    // Any other index (shouldn't happen) falls through to "".
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
        // Apply the current filter to search results too, so "Instruments"
        // + "bass" narrows search instead of ignoring the dropdown.
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

} // namespace fw
} // namespace ui
} // namespace yawn
