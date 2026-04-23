#pragma once
// BrowserFilesTab — Files tab for the BrowserPanel (fw2::Widget).
// Provides a tree-view file browser backed by LibraryDatabase.
//
// Migrated from v1 plain class (taking v1 MouseEvent + Font&) to a
// proper fw2::Widget. Bounds come from m_bounds after layout(); the
// parent BrowserPanel just does measure/layout/render each frame like
// it does with the other fw2 children (follow-action knobs, etc.).

#include "ui/framework/v2/Widget.h"
#include "ui/framework/v2/Button.h"
#include "ui/framework/v2/TextInput.h"
#include "ui/framework/v2/UIContext.h"
#include "ui/framework/v2/Theme.h"
#include "ui/Renderer.h"
#include "ui/Theme.h"
#include "library/LibraryDatabase.h"
#include "library/LibraryScanner.h"
#include <SDL3/SDL_keycode.h>  // SDLK_* — forwarded key ints from App.cpp
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace yawn {
namespace ui {
namespace fw2 {

struct FileTreeNode {
    std::string name;
    std::string fullPath;
    bool        isDirectory = false;
    bool        expanded    = false;
    int         depth       = 0;
    int64_t     libraryPathId = -1;
    float       duration    = 0;
    int         sampleRate  = 0;
    int         channels    = 0;
    std::vector<FileTreeNode> children;
};

class BrowserFilesTab : public Widget {
public:
    BrowserFilesTab() {
        m_searchInput.setPlaceholder("Search files...");
        m_searchInput.setOnCommit([this](const std::string&) { doSearch(); });
        // Live search on every keystroke.
        m_searchInput.setOnChange([this](const std::string&) { doSearch(); });
        m_addFolderBtn.setLabel("+");
        setFocusable(false);
    }

    void setDatabase(library::LibraryDatabase* db) { m_db = db; }
    void setScanner(library::LibraryScanner* sc) { m_scanner = sc; }
    void setOnAddFolder(std::function<void()> cb) { m_onAddFolder = std::move(cb); }
    void setOnFileDoubleClick(std::function<void(const std::string&)> cb) { m_onFileDoubleClick = std::move(cb); }
    void setOnRemoveFolder(std::function<void(int64_t)> cb) { m_onRemoveFolder = std::move(cb); }
    void setOnRescanFolder(std::function<void(int64_t, const std::string&)> cb) { m_onRescanFolder = std::move(cb); }

    void refreshTree() {
        if (!m_db) return;
        m_fileTree.clear();
        m_searchResultNodes.clear();
        m_searching = false;

        auto paths = m_db->getLibraryPaths();
        for (auto& lp : paths) {
            FileTreeNode root;
            root.name = lp.path;
            // Show just the last folder name for display
            auto pos = lp.path.find_last_of("/\\");
            if (pos != std::string::npos && pos + 1 < lp.path.size())
                root.name = lp.path.substr(pos + 1);
            root.fullPath = lp.path;
            root.isDirectory = true;
            root.libraryPathId = lp.id;
            root.depth = 0;

            // Build children from database
            auto files = m_db->getAudioFilesForPath(lp.id);
            buildTree(root, files);
            m_fileTree.push_back(std::move(root));
        }
        flattenTree();
    }

    // ─── Editing / keyboard-forward API (App.cpp passes SDL keycodes) ───

    bool isSearchEditing() const { return m_searchInput.isEditing(); }
    bool forwardKeyDown(int key) {
        if (key == SDLK_ESCAPE) {
            // Escape: clear search + collapse, then cancel widget edit.
            m_searchInput.endEdit(/*commit*/false);
            m_searchInput.setText("");
            m_searching = false;
            flattenTree();
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
        // Position the search input + add-folder button in the top row.
        const float x = bounds.x, y = bounds.y, w = bounds.w;
        m_searchInput.measure(Constraints::tight(w - 30, 20), ctx);
        m_searchInput.layout(Rect{x + 4, y + 4, w - 30, 20}, ctx);
        m_addFolderBtn.measure(Constraints::tight(20, 20), ctx);
        m_addFolderBtn.layout(Rect{x + w - 24, y + 4, 20, 20}, ctx);
    }

    bool onMouseDown(MouseEvent& e) override {
        const float mx = e.x, my = e.y;
        const bool rightClick = (e.button == MouseButton::Right);
        const float x = m_bounds.x, y = m_bounds.y;
        const float w = m_bounds.w, h = m_bounds.h;

        // Search input — let fw2 widget handle it via dispatch. Its
        // onMouseDown enters edit mode and returns true, short-circuiting
        // the gesture SM (no capture needed, no paired mouseUp).
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

        // Add folder button
        {
            const auto& b = m_addFolderBtn.bounds();
            if (!rightClick && mx >= b.x && mx < b.x + b.w &&
                my >= b.y && my < b.y + b.h) {
                if (m_onAddFolder) m_onAddFolder();
                return true;
            }
        }

        // Tree/list area
        const float listY = y + 28;
        const float listH = h - 28;
        if (my < listY || my >= listY + listH) return false;

        auto& items = m_searching ? m_flatSearchResults : m_flatList;
        const float rowH = kRowHeight;
        int row = static_cast<int>((my - listY) / rowH) + m_scrollOffset;
        if (row < 0 || row >= static_cast<int>(items.size())) return false;

        auto* node = items[row];

        // Right-click on a root library folder → inline context menu
        if (rightClick && node->isDirectory && node->depth == 0) {
            m_contextNodeId = node->libraryPathId;
            m_contextNodePath = node->fullPath;
            m_showContextMenu = true;
            m_contextMenuX = mx;
            m_contextMenuY = my;
            return true;
        }

        // Context menu click
        if (m_showContextMenu) {
            m_showContextMenu = false;
            float cmY = m_contextMenuY;
            if (hitRect(mx, my, m_contextMenuX, cmY, 120, 20)) {
                if (m_onRescanFolder) m_onRescanFolder(m_contextNodeId, m_contextNodePath);
                return true;
            }
            cmY += 20;
            if (hitRect(mx, my, m_contextMenuX, cmY, 120, 20)) {
                if (m_onRemoveFolder) m_onRemoveFolder(m_contextNodeId);
                refreshTree();
                return true;
            }
            return true;
        }

        // Double-click detection
        auto now = std::chrono::steady_clock::now();
        bool isDoubleClick = (row == m_lastClickRow &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastClickTime).count() < 400);
        m_lastClickRow = row;
        m_lastClickTime = now;

        if (node->isDirectory) {
            node->expanded = !node->expanded;
            flattenTree();
        } else if (isDoubleClick && !rightClick) {
            if (m_onFileDoubleClick) m_onFileDoubleClick(node->fullPath);
        }

        m_selectedIndex = row;
        return true;
    }

    bool onScroll(ScrollEvent& e) override {
        m_scrollOffset -= static_cast<int>(e.dy * 3);
        if (m_scrollOffset < 0) m_scrollOffset = 0;
        return true;
    }

    // No mouseMove/mouseUp handling required — the search input short-
    // circuits the gesture SM in its own onMouseDown (enters edit mode,
    // no capture) and the tree-list rows only respond to mouseDown.

public:
    void render(UIContext& ctx) override {
        if (!isVisible()) return;
        if (!ctx.renderer || !ctx.textMetrics) return;
        auto& r  = *ctx.renderer;
        auto& tm = *ctx.textMetrics;

        const float x = m_bounds.x, y = m_bounds.y;
        const float w = m_bounds.w, h = m_bounds.h;
        const float fs = theme().metrics.fontSizeSmall;

        // Search input + add folder button (already laid out in onLayout)
        m_searchInput.render(ctx);
        m_addFolderBtn.render(ctx);

        // Scanning indicator
        if (m_scanner && m_scanner->isScanning()) {
            float pct = m_scanner->progress() * 100.0f;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Scanning %.0f%%", pct);
            const std::string s = buf;
            float tw = tm.textWidth(s, fs);
            tm.drawText(r, s, x + w - tw - 4, y + 26, fs,
                        Color{100, 180, 120, 255});
        }

        const float listY = y + 28;
        const float listH = h - 28;
        if (listH <= 0) return;

        auto& items = m_searching ? m_flatSearchResults : m_flatList;
        int totalRows = static_cast<int>(items.size());

        if (totalRows == 0) {
            const char* msg = m_fileTree.empty() ? "Click '+ Folder' to add a sample library"
                                                 : "No matching files";
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

        const float lh = tm.lineHeight(fs);
        for (int i = 0; i < visibleRows + 1 && (i + m_scrollOffset) < totalRows; ++i) {
            int idx = i + m_scrollOffset;
            auto* node = items[idx];
            float ry = listY + i * rowH;

            // Selection highlight / zebra
            if (idx == m_selectedIndex) {
                r.drawRect(x, ry, w, rowH, Color{50, 60, 80, 255});
            } else if (i % 2 == 1) {
                r.drawRect(x, ry, w, rowH, Color{36, 36, 40, 255});
            }

            float indent = 8.0f + node->depth * 16.0f;
            float textY = ry + (rowH - lh) * 0.5f;

            if (node->isDirectory) {
                // Expand/collapse triangle
                const char* tri = node->expanded ? "\xe2\x96\xbc" : "\xe2\x96\xb6";
                tm.drawText(r, tri, x + indent, textY, fs * 0.8f,
                            ::yawn::ui::Theme::textSecondary);
                Color nameCol = (node->depth == 0) ? Color{180, 200, 230, 255}
                                                   : ::yawn::ui::Theme::textPrimary;
                tm.drawText(r, node->name, x + indent + 14, textY, fs, nameCol);
            } else {
                // File icon (small dot)
                float dotY = ry + rowH * 0.5f - 2;
                r.drawRect(x + indent, dotY, 4, 4, Color{100, 180, 120, 255});
                tm.drawText(r, node->name, x + indent + 10, textY, fs,
                            ::yawn::ui::Theme::textPrimary);
                // Duration on right side
                if (node->duration > 0) {
                    char dur[16];
                    if (node->duration >= 60.0f)
                        std::snprintf(dur, sizeof(dur), "%d:%02d",
                                      static_cast<int>(node->duration) / 60,
                                      static_cast<int>(node->duration) % 60);
                    else
                        std::snprintf(dur, sizeof(dur), "%.1fs", node->duration);
                    const std::string ds = dur;
                    float dw = tm.textWidth(ds, fs * 0.9f);
                    tm.drawText(r, ds, x + w - dw - 8, textY, fs * 0.9f,
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

        // Context menu overlay
        if (m_showContextMenu) {
            float cmX = m_contextMenuX, cmY = m_contextMenuY;
            r.drawRect(cmX, cmY, 120, 40, Color{45, 45, 50, 255});
            r.drawRectOutline(cmX, cmY, 120, 40, Color{70, 70, 78, 255});
            tm.drawText(r, "Rescan", cmX + 8, cmY + 3, fs,
                        ::yawn::ui::Theme::textPrimary);
            tm.drawText(r, "Remove Folder", cmX + 8, cmY + 23, fs,
                        ::yawn::ui::Theme::textPrimary);
        }
    }

private:
    static constexpr float kRowHeight = 18.0f;

    library::LibraryDatabase* m_db = nullptr;
    library::LibraryScanner*  m_scanner = nullptr;

    FwTextInput m_searchInput;
    FwButton    m_addFolderBtn;

    std::vector<FileTreeNode>  m_fileTree;       // root nodes
    std::vector<FileTreeNode*> m_flatList;       // flattened visible nodes
    std::vector<FileTreeNode*> m_flatSearchResults;
    bool m_searching = false;

    int m_scrollOffset = 0;
    int m_selectedIndex = -1;

    // Double-click detection
    int m_lastClickRow = -1;
    std::chrono::steady_clock::time_point m_lastClickTime;

    // Context menu
    bool    m_showContextMenu = false;
    float   m_contextMenuX = 0, m_contextMenuY = 0;
    int64_t m_contextNodeId = -1;
    std::string m_contextNodePath;

    // Callbacks
    std::function<void()>                             m_onAddFolder;
    std::function<void(const std::string&)>           m_onFileDoubleClick;
    std::function<void(int64_t)>                      m_onRemoveFolder;
    std::function<void(int64_t, const std::string&)>  m_onRescanFolder;

    static bool hitRect(float mx, float my, float rx, float ry, float rw, float rh) {
        return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh;
    }

    void buildTree(FileTreeNode& parent, const std::vector<library::AudioFileRecord>& files) {
        namespace fs = std::filesystem;
        fs::path rootPath(parent.fullPath);

        // Map: relative dir path → list of files in that dir
        std::map<std::string, std::vector<const library::AudioFileRecord*>> dirFiles;
        for (auto& f : files) {
            fs::path fp(f.path);
            fs::path rel = fp.parent_path().lexically_relative(rootPath);
            dirFiles[rel.string()].push_back(&f);
        }

        buildSubTree(parent, rootPath, "", dirFiles, 1);
    }

    void buildSubTree(FileTreeNode& parent, const std::filesystem::path& rootPath,
                      const std::string& relDir,
                      std::map<std::string, std::vector<const library::AudioFileRecord*>>& dirFiles,
                      int depth) {
        namespace fs = std::filesystem;
        const char sep = static_cast<char>(fs::path::preferred_separator);

        std::set<std::string> subdirs;
        for (auto& [rel, files] : dirFiles) {
            if (rel == relDir) continue;
            std::string prefix = relDir.empty() ? std::string() : relDir + sep;
            if (!relDir.empty() && rel.find(prefix) != 0) continue;
            if (relDir.empty() && rel.find(sep) == std::string::npos && !rel.empty()) {
                subdirs.insert(rel);
            } else if (!relDir.empty()) {
                std::string remainder = rel.substr(prefix.size());
                auto sepPos = remainder.find(sep);
                if (sepPos == std::string::npos)
                    subdirs.insert(rel);
                else
                    subdirs.insert(prefix + remainder.substr(0, sepPos));
            }
        }

        for (auto& subdir : subdirs) {
            FileTreeNode dirNode;
            fs::path subPath(subdir);
            dirNode.name = subPath.filename().string();
            dirNode.fullPath = (rootPath / subdir).string();
            dirNode.isDirectory = true;
            dirNode.depth = depth;
            dirNode.libraryPathId = parent.libraryPathId;
            buildSubTree(dirNode, rootPath, subdir, dirFiles, depth + 1);
            if (!dirNode.children.empty())
                parent.children.push_back(std::move(dirNode));
        }

        auto it = dirFiles.find(relDir);
        if (it != dirFiles.end()) {
            for (auto* rec : it->second) {
                FileTreeNode fileNode;
                fileNode.name = rec->name + rec->extension;
                fileNode.fullPath = rec->path;
                fileNode.isDirectory = false;
                fileNode.depth = depth;
                fileNode.libraryPathId = parent.libraryPathId;
                fileNode.duration = rec->duration;
                fileNode.sampleRate = rec->sampleRate;
                fileNode.channels = rec->channels;
                parent.children.push_back(std::move(fileNode));
            }
        }
    }

    void flattenTree() {
        m_flatList.clear();
        for (auto& root : m_fileTree)
            flattenNode(root, m_flatList);
    }

    void flattenNode(FileTreeNode& node, std::vector<FileTreeNode*>& out) {
        out.push_back(&node);
        if (node.isDirectory && node.expanded) {
            for (auto& child : node.children)
                flattenNode(child, out);
        }
    }

    void doSearch() {
        std::string query = m_searchInput.text();
        if (query.empty()) {
            m_searching = false;
            return;
        }
        if (!m_db) return;

        m_searching = true;
        m_flatSearchResults.clear();
        m_searchResultNodes.clear();

        auto results = m_db->searchAudioFiles(query);
        m_searchResultNodes.reserve(results.size());
        for (auto& rec : results) {
            FileTreeNode node;
            node.name = rec.name + rec.extension;
            node.fullPath = rec.path;
            node.isDirectory = false;
            node.depth = 0;
            node.duration = rec.duration;
            node.sampleRate = rec.sampleRate;
            node.channels = rec.channels;
            m_searchResultNodes.push_back(std::move(node));
        }
        for (auto& n : m_searchResultNodes)
            m_flatSearchResults.push_back(&n);

        m_scrollOffset = 0;
    }

    // Storage for search result nodes (must outlive pointers in m_flatSearchResults)
    std::vector<FileTreeNode> m_searchResultNodes;
};

} // namespace fw2
} // namespace ui
} // namespace yawn
