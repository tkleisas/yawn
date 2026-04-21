#include "UIContext.h"

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
UIContext* g_global = nullptr;

UIContext& defaultGlobal() {
    static UIContext g_default;
    return g_default;
}
} // anon

UIContext& UIContext::global() {
    return g_global ? *g_global : defaultGlobal();
}

void UIContext::setGlobal(UIContext* ctx) {
    g_global = ctx;
}

} // namespace fw2
} // namespace ui
} // namespace yawn
