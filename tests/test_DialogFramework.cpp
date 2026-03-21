#include <gtest/gtest.h>
#include "ui/framework/Dialog.h"
#include "ui/framework/TabPanel.h"
#include "ui/framework/FormLayout.h"

using namespace yawn::ui::fw;

class SizedWidget : public Widget {
public:
    Size preferred;
    SizedWidget(float w, float h) : preferred{w, h} {}
    Size measure(const Constraints& c, const UIContext&) override {
        return c.constrain(preferred);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
// Dialog tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(Dialog, DefaultSize) {
    Dialog dlg("Test", 400, 300);
    UIContext ctx;
    Size s = dlg.measure(Constraints::loose(800, 600), ctx);
    EXPECT_FLOAT_EQ(s.w, 400);
    EXPECT_FLOAT_EQ(s.h, 300);
}

TEST(Dialog, TitleAccess) {
    Dialog dlg("Settings");
    EXPECT_EQ(dlg.title(), "Settings");
    dlg.setTitle("Preferences");
    EXPECT_EQ(dlg.title(), "Preferences");
}

TEST(Dialog, CloseViaEscape) {
    Dialog dlg("Test");
    DialogResult capturedResult = DialogResult::None;
    dlg.setOnResult([&](DialogResult r) { capturedResult = r; });

    KeyEvent e;
    e.keyCode = 27; // Escape
    dlg.onKeyDown(e);

    EXPECT_EQ(capturedResult, DialogResult::Cancel);
    EXPECT_FALSE(dlg.visible());
}

TEST(Dialog, CloseViaEnterWithOKCancel) {
    Dialog dlg("Test");
    dlg.setShowOKCancel(true);
    DialogResult capturedResult = DialogResult::None;
    dlg.setOnResult([&](DialogResult r) { capturedResult = r; });

    KeyEvent e;
    e.keyCode = 13; // Enter
    dlg.onKeyDown(e);

    EXPECT_EQ(capturedResult, DialogResult::OK);
}

TEST(Dialog, ContentLayout) {
    Dialog dlg("Test", 400, 300);
    SizedWidget content(350, 200);
    dlg.setContent(&content);

    UIContext ctx;
    dlg.measure(Constraints::loose(800, 600), ctx);
    dlg.layout(Rect{100, 50, 400, 300}, ctx);

    // Content should be inside the dialog, below the title bar
    EXPECT_GT(content.bounds().y, 50); // Below title bar
    EXPECT_GE(content.bounds().x, 100); // Within dialog
}

TEST(Dialog, TitleBarRects) {
    Dialog dlg("Test", 400, 300);
    UIContext ctx;
    dlg.measure(Constraints::loose(800, 600), ctx);
    dlg.layout(Rect{100, 50, 400, 300}, ctx);

    Rect tb = dlg.titleBarRect();
    EXPECT_FLOAT_EQ(tb.x, 100);
    EXPECT_FLOAT_EQ(tb.y, 50);
    EXPECT_FLOAT_EQ(tb.w, 400);
    EXPECT_FLOAT_EQ(tb.h, Dialog::kTitleBarHeight);
}

TEST(Dialog, FooterRects) {
    Dialog dlg("Test", 400, 300);
    dlg.setShowOKCancel(true);

    UIContext ctx;
    dlg.measure(Constraints::loose(800, 600), ctx);
    dlg.layout(Rect{100, 50, 400, 300}, ctx);

    Rect footer = dlg.footerRect();
    EXPECT_FLOAT_EQ(footer.y, 50 + 300 - Dialog::kFooterHeight);
    EXPECT_FLOAT_EQ(footer.h, Dialog::kFooterHeight);
}

TEST(Dialog, ResultDefault) {
    Dialog dlg("Test");
    EXPECT_EQ(dlg.result(), DialogResult::None);
}

TEST(Dialog, CloseMethodSetsResult) {
    Dialog dlg("Test");
    dlg.close(DialogResult::OK);
    EXPECT_EQ(dlg.result(), DialogResult::OK);
    EXPECT_FALSE(dlg.visible());
}

// ═══════════════════════════════════════════════════════════════════════════
// TabPanel tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(TabPanel, AddTabs) {
    TabPanel tabs;
    SizedWidget page1(100, 100), page2(100, 100);

    tabs.addTab("Audio", &page1);
    tabs.addTab("MIDI", &page2);

    EXPECT_EQ(tabs.tabCount(), 2);
    EXPECT_EQ(tabs.activeTab(), 0);
    EXPECT_EQ(tabs.tabLabel(0), "Audio");
    EXPECT_EQ(tabs.tabLabel(1), "MIDI");
}

TEST(TabPanel, FirstTabAutoActive) {
    TabPanel tabs;
    SizedWidget page(100, 100);
    tabs.addTab("Audio", &page);

    EXPECT_EQ(tabs.activeTab(), 0);
    EXPECT_TRUE(page.visible());
}

TEST(TabPanel, SwitchTab) {
    TabPanel tabs;
    SizedWidget page1(100, 100), page2(100, 100);
    tabs.addTab("Audio", &page1);
    tabs.addTab("MIDI", &page2);

    EXPECT_TRUE(page1.visible());
    EXPECT_FALSE(page2.visible());

    tabs.setActiveTab(1);
    EXPECT_EQ(tabs.activeTab(), 1);
    EXPECT_FALSE(page1.visible());
    EXPECT_TRUE(page2.visible());
}

TEST(TabPanel, TabChangedCallback) {
    TabPanel tabs;
    SizedWidget page1(100, 100), page2(100, 100);
    tabs.addTab("Audio", &page1);
    tabs.addTab("MIDI", &page2);

    int oldIdx = -1, newIdx = -1;
    tabs.setOnTabChanged([&](int o, int n) { oldIdx = o; newIdx = n; });

    tabs.setActiveTab(1);
    EXPECT_EQ(oldIdx, 0);
    EXPECT_EQ(newIdx, 1);
}

TEST(TabPanel, RemoveTab) {
    TabPanel tabs;
    SizedWidget page1(100, 100), page2(100, 100);
    tabs.addTab("Audio", &page1);
    tabs.addTab("MIDI", &page2);

    tabs.removeTab(0);
    EXPECT_EQ(tabs.tabCount(), 1);
    EXPECT_EQ(tabs.tabLabel(0), "MIDI");
}

TEST(TabPanel, LayoutWithTopTabs) {
    TabPanel tabs(TabPosition::Top);
    SizedWidget page(300, 200);
    tabs.addTab("Audio", &page);

    UIContext ctx;
    tabs.measure(Constraints::loose(400, 400), ctx);
    tabs.layout(Rect{0, 0, 400, 400}, ctx);

    // Content should be below tab header
    EXPECT_FLOAT_EQ(page.bounds().y, TabPanel::kTabHeight);
    EXPECT_FLOAT_EQ(page.bounds().h, 400 - TabPanel::kTabHeight);
}

TEST(TabPanel, LayoutWithLeftTabs) {
    TabPanel tabs(TabPosition::Left);
    SizedWidget page(300, 200);
    tabs.addTab("Audio", &page);

    UIContext ctx;
    tabs.measure(Constraints::loose(500, 400), ctx);
    tabs.layout(Rect{0, 0, 500, 400}, ctx);

    // Content should be to the right of tab sidebar
    EXPECT_FLOAT_EQ(page.bounds().x, TabPanel::kTabLeftWidth);
    EXPECT_FLOAT_EQ(page.bounds().w, 500 - TabPanel::kTabLeftWidth);
}

TEST(TabPanel, TabContent) {
    TabPanel tabs;
    SizedWidget page(100, 100);
    tabs.addTab("Audio", &page);
    EXPECT_EQ(tabs.tabContent(0), &page);
    EXPECT_EQ(tabs.tabContent(-1), nullptr);
    EXPECT_EQ(tabs.tabContent(99), nullptr);
}

TEST(TabPanel, SetTabLabel) {
    TabPanel tabs;
    SizedWidget page(100, 100);
    tabs.addTab("Audio", &page);
    tabs.setTabLabel(0, "Audio Settings");
    EXPECT_EQ(tabs.tabLabel(0), "Audio Settings");
}

TEST(TabPanel, OutOfBoundsTabSwitch) {
    TabPanel tabs;
    SizedWidget page(100, 100);
    tabs.addTab("Audio", &page);

    tabs.setActiveTab(99);  // Should be ignored
    EXPECT_EQ(tabs.activeTab(), 0);

    tabs.setActiveTab(-1);  // Should be ignored
    EXPECT_EQ(tabs.activeTab(), 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// FormLayout tests
// ═══════════════════════════════════════════════════════════════════════════

TEST(FormLayout, AddRows) {
    FormLayout form;
    SizedWidget w1(100, 24), w2(100, 24);
    form.addRow("Volume", &w1);
    form.addRow("Pan", &w2);

    EXPECT_EQ(form.rowCount(), 2);
    EXPECT_EQ(form.widgetAt(0), &w1);
    EXPECT_EQ(form.widgetAt(1), &w2);
}

TEST(FormLayout, SectionsAndSeparators) {
    FormLayout form;
    SizedWidget w1(100, 24);
    form.addSection("Audio");
    form.addRow("Volume", &w1);
    form.addSeparator();

    EXPECT_EQ(form.rowCount(), 3);
    EXPECT_EQ(form.widgetAt(0), nullptr);  // Section has no widget
}

TEST(FormLayout, LayoutPositionsWidgets) {
    FormLayout form;
    SizedWidget w1(100, 24), w2(100, 24);
    form.addRow("Volume", &w1);
    form.addRow("Pan", &w2);

    UIContext ctx;
    form.measure(Constraints::loose(400, 400), ctx);
    form.layout(Rect{0, 0, 400, 400}, ctx);

    // Widgets should be positioned on the right side
    float labelW = 400 * FormLayout::kLabelWidthRatio;
    EXPECT_GT(w1.bounds().x, labelW);
    EXPECT_GT(w2.bounds().x, labelW);

    // Second widget should be below first
    EXPECT_GT(w2.bounds().y, w1.bounds().y);
}

TEST(FormLayout, SectionCreatesGap) {
    FormLayout form;
    SizedWidget w1(100, 24), w2(100, 24);
    form.addRow("Volume", &w1);
    form.addSection("MIDI");
    form.addRow("Channel", &w2);

    UIContext ctx;
    form.measure(Constraints::loose(400, 400), ctx);
    form.layout(Rect{0, 0, 400, 400}, ctx);

    // Gap between rows with section should be larger than normal
    float gap = w2.bounds().y - (w1.bounds().y + w1.bounds().h);
    EXPECT_GT(gap, FormLayout::kRowGap * 2);
}

TEST(FormLayout, RowInfo) {
    FormLayout form;
    SizedWidget w1(100, 24);
    form.addSection("Audio");
    form.addRow("Volume", &w1);

    UIContext ctx;
    form.measure(Constraints::loose(400, 400), ctx);
    form.layout(Rect{0, 0, 400, 400}, ctx);

    auto info0 = form.rowInfo(0);
    EXPECT_EQ(info0.label, "Audio");
    EXPECT_TRUE(info0.isSection);

    auto info1 = form.rowInfo(1);
    EXPECT_EQ(info1.label, "Volume");
    EXPECT_FALSE(info1.isSection);
}

TEST(FormLayout, NullWidgetAt) {
    FormLayout form;
    EXPECT_EQ(form.widgetAt(-1), nullptr);
    EXPECT_EQ(form.widgetAt(99), nullptr);
}

TEST(FormLayout, LabelWidthRatio) {
    FormLayout form;
    form.setLabelWidthRatio(0.5f);
    SizedWidget w1(100, 24);
    form.addRow("Volume", &w1);

    UIContext ctx;
    form.measure(Constraints::loose(400, 400), ctx);
    form.layout(Rect{0, 0, 400, 400}, ctx);

    EXPECT_FLOAT_EQ(form.labelWidth(), 200);
}
