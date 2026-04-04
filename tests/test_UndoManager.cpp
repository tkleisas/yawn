#include <gtest/gtest.h>
#include "util/UndoManager.h"

using namespace undo;

TEST(UndoManager, InitialState) {
    UndoManager mgr;
    EXPECT_FALSE(mgr.canUndo());
    EXPECT_FALSE(mgr.canRedo());
    EXPECT_EQ(mgr.undoSize(), 0u);
    EXPECT_EQ(mgr.redoSize(), 0u);
    EXPECT_EQ(mgr.undoDescription(), "");
    EXPECT_EQ(mgr.redoDescription(), "");
}

TEST(UndoManager, PushAndUndo) {
    UndoManager mgr;
    int value = 10;
    mgr.push({"Set to 20", [&]{ value = 10; }, [&]{ value = 20; }, ""});
    EXPECT_TRUE(mgr.canUndo());
    EXPECT_FALSE(mgr.canRedo());
    EXPECT_EQ(mgr.undoDescription(), "Set to 20");

    mgr.undo();
    EXPECT_EQ(value, 10);
    EXPECT_FALSE(mgr.canUndo());
    EXPECT_TRUE(mgr.canRedo());
    EXPECT_EQ(mgr.redoDescription(), "Set to 20");
}

TEST(UndoManager, Redo) {
    UndoManager mgr;
    int value = 10;
    mgr.push({"Set to 20", [&]{ value = 10; }, [&]{ value = 20; }, ""});
    value = 20;

    mgr.undo();
    EXPECT_EQ(value, 10);

    mgr.redo();
    EXPECT_EQ(value, 20);
    EXPECT_TRUE(mgr.canUndo());
    EXPECT_FALSE(mgr.canRedo());
}

TEST(UndoManager, MultipleUndoRedo) {
    UndoManager mgr;
    int value = 0;

    mgr.push({"Set 1", [&]{ value = 0; }, [&]{ value = 1; }, ""});
    value = 1;
    mgr.push({"Set 2", [&]{ value = 1; }, [&]{ value = 2; }, ""});
    value = 2;
    mgr.push({"Set 3", [&]{ value = 2; }, [&]{ value = 3; }, ""});
    value = 3;

    EXPECT_EQ(mgr.undoSize(), 3u);

    mgr.undo(); // 3 → 2
    EXPECT_EQ(value, 2);
    mgr.undo(); // 2 → 1
    EXPECT_EQ(value, 1);
    mgr.undo(); // 1 → 0
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(mgr.canUndo());
    EXPECT_EQ(mgr.redoSize(), 3u);

    mgr.redo(); // 0 → 1
    EXPECT_EQ(value, 1);
    mgr.redo(); // 1 → 2
    EXPECT_EQ(value, 2);
    EXPECT_EQ(mgr.undoSize(), 2u);
    EXPECT_EQ(mgr.redoSize(), 1u);
}

TEST(UndoManager, NewActionClearsRedoStack) {
    UndoManager mgr;
    int value = 0;

    mgr.push({"Set 1", [&]{ value = 0; }, [&]{ value = 1; }, ""});
    value = 1;
    mgr.push({"Set 2", [&]{ value = 1; }, [&]{ value = 2; }, ""});
    value = 2;

    mgr.undo(); // back to 1
    EXPECT_TRUE(mgr.canRedo());

    // New action should clear redo
    mgr.push({"Set 5", [&]{ value = 1; }, [&]{ value = 5; }, ""});
    EXPECT_FALSE(mgr.canRedo());
    EXPECT_EQ(mgr.undoSize(), 2u);
}

TEST(UndoManager, CoalesceSameMergeId) {
    UndoManager mgr;
    float vol = 0.5f;

    // Simulate dragging a fader: many small increments with same mergeId
    float origVol = vol;
    mgr.push({"Volume", [&, origVol]{ vol = origVol; }, [&]{ vol = 0.6f; }, "vol.0"});
    vol = 0.6f;

    mgr.push({"Volume", [&, origVol]{ vol = origVol; }, [&]{ vol = 0.7f; }, "vol.0"});
    vol = 0.7f;

    mgr.push({"Volume", [&, origVol]{ vol = origVol; }, [&]{ vol = 0.8f; }, "vol.0"});
    vol = 0.8f;

    // Should have coalesced into a single entry
    EXPECT_EQ(mgr.undoSize(), 1u);

    // Undo should restore original value
    mgr.undo();
    EXPECT_FLOAT_EQ(vol, 0.5f);

    // Redo should apply the final value
    mgr.redo();
    EXPECT_FLOAT_EQ(vol, 0.8f);
}

TEST(UndoManager, DifferentMergeIdsNotCoalesced) {
    UndoManager mgr;
    int a = 0, b = 0;

    mgr.push({"A", [&]{ a = 0; }, [&]{ a = 1; }, "a"});
    mgr.push({"B", [&]{ b = 0; }, [&]{ b = 1; }, "b"});

    EXPECT_EQ(mgr.undoSize(), 2u);
}

TEST(UndoManager, CoalesceBreaksOnDifferentId) {
    UndoManager mgr;
    float vol = 0.0f;
    float pan = 0.0f;

    mgr.push({"Vol", [&]{ vol = 0.0f; }, [&]{ vol = 0.5f; }, "vol"});
    mgr.push({"Vol", [&]{ vol = 0.0f; }, [&]{ vol = 0.6f; }, "vol"});
    // Interrupted by a different action
    mgr.push({"Pan", [&]{ pan = 0.0f; }, [&]{ pan = 0.3f; }, "pan"});
    // Resume vol changes — should NOT coalesce with previous vol
    mgr.push({"Vol", [&]{ vol = 0.6f; }, [&]{ vol = 0.9f; }, "vol"});

    EXPECT_EQ(mgr.undoSize(), 3u); // coalesced vol + pan + new vol
}

TEST(UndoManager, EmptyMergeIdNeverCoalesces) {
    UndoManager mgr;
    int v = 0;

    mgr.push({"A", [&]{ v = 0; }, [&]{ v = 1; }, ""});
    mgr.push({"B", [&]{ v = 1; }, [&]{ v = 2; }, ""});

    EXPECT_EQ(mgr.undoSize(), 2u);
}

TEST(UndoManager, Clear) {
    UndoManager mgr;
    int v = 0;
    mgr.push({"A", [&]{ v = 0; }, [&]{ v = 1; }, ""});
    mgr.undo();
    EXPECT_TRUE(mgr.canRedo());

    mgr.clear();
    EXPECT_FALSE(mgr.canUndo());
    EXPECT_FALSE(mgr.canRedo());
    EXPECT_EQ(mgr.undoSize(), 0u);
    EXPECT_EQ(mgr.redoSize(), 0u);
}

TEST(UndoManager, UndoOnEmptyIsNoop) {
    UndoManager mgr;
    mgr.undo(); // should not crash
    EXPECT_FALSE(mgr.canUndo());
}

TEST(UndoManager, RedoOnEmptyIsNoop) {
    UndoManager mgr;
    mgr.redo(); // should not crash
    EXPECT_FALSE(mgr.canRedo());
}

TEST(UndoManager, DescriptionUpdatesOnCoalesce) {
    UndoManager mgr;
    int v = 0;
    mgr.push({"Set to 1", [&]{ v = 0; }, [&]{ v = 1; }, "x"});
    EXPECT_EQ(mgr.undoDescription(), "Set to 1");

    mgr.push({"Set to 5", [&]{ v = 0; }, [&]{ v = 5; }, "x"});
    // Description should update to latest
    EXPECT_EQ(mgr.undoDescription(), "Set to 5");
    EXPECT_EQ(mgr.undoSize(), 1u);
}
