#include <gtest/gtest.h>

#include "ui/framework/v2/TextInput.h"

using namespace yawn::ui::fw2;

TEST(FwTextInputSelection, TypingReplacesSelectedText) {
    FwTextInput t;
    t.setText("My Preset");
    t.beginEdit();
    t.selectAll();
    ASSERT_TRUE(t.hasSelection());
    t.takeTextInput("New Name");
    EXPECT_EQ(t.text(), "New Name");
    EXPECT_EQ(t.cursor(), 8);
    EXPECT_FALSE(t.hasSelection());
}

TEST(FwTextInputSelection, TypingAppendsWhenSelectionCleared) {
    FwTextInput t;
    t.setText("My Preset");
    t.beginEdit();
    t.selectAll();
    t.setCursor(t.cursor());   // collapse selection
    t.takeTextInput("!");
    EXPECT_EQ(t.text(), "My Preset!");
}

TEST(FwTextInputSelection, SetTextClearsSelection) {
    FwTextInput t;
    t.setText("old");
    t.beginEdit();
    t.selectAll();
    t.setText("new");
    EXPECT_FALSE(t.hasSelection());
    t.takeTextInput("!");
    EXPECT_EQ(t.text(), "new!");
}
