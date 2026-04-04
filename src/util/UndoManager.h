#pragma once
#include <deque>
#include <functional>
#include <string>

namespace undo {

struct UndoEntry {
    std::string description;
    std::function<void()> undoFn;
    std::function<void()> redoFn;
    std::string mergeId;  // non-empty → consecutive entries with same id are coalesced
};

class UndoManager {
public:
    // Push an undoable action. If mergeId matches the top of the undo stack,
    // the entries are coalesced (keep first undo, use latest redo).
    void push(UndoEntry entry) {
        // Coalesce with top if mergeIds match
        if (!entry.mergeId.empty() && !m_undoStack.empty() &&
            m_undoStack.back().mergeId == entry.mergeId) {
            m_undoStack.back().redoFn = std::move(entry.redoFn);
            m_undoStack.back().description = std::move(entry.description);
        } else {
            m_undoStack.push_back(std::move(entry));
        }
        // Any new action invalidates the redo stack
        m_redoStack.clear();
    }

    bool canUndo() const { return !m_undoStack.empty(); }
    bool canRedo() const { return !m_redoStack.empty(); }

    std::string undoDescription() const {
        return m_undoStack.empty() ? "" : m_undoStack.back().description;
    }
    std::string redoDescription() const {
        return m_redoStack.empty() ? "" : m_redoStack.back().description;
    }

    void undo() {
        if (m_undoStack.empty()) return;
        auto entry = std::move(m_undoStack.back());
        m_undoStack.pop_back();
        entry.undoFn();
        m_redoStack.push_back(std::move(entry));
    }

    void redo() {
        if (m_redoStack.empty()) return;
        auto entry = std::move(m_redoStack.back());
        m_redoStack.pop_back();
        entry.redoFn();
        m_undoStack.push_back(std::move(entry));
    }

    void clear() {
        m_undoStack.clear();
        m_redoStack.clear();
    }

    size_t undoSize() const { return m_undoStack.size(); }
    size_t redoSize() const { return m_redoStack.size(); }

private:
    std::deque<UndoEntry> m_undoStack;
    std::deque<UndoEntry> m_redoStack;
};

} // namespace undo
