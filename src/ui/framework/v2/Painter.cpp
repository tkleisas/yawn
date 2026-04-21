#include "Painter.h"

#include <typeindex>
#include <unordered_map>

namespace yawn {
namespace ui {
namespace fw2 {

namespace {
// Meyers singleton — survives static init ordering issues since the
// first call from registerAllFw2Painters() constructs it lazily.
std::unordered_map<std::type_index, PaintFn>& registry() {
    static std::unordered_map<std::type_index, PaintFn> r;
    return r;
}
} // anon

void registerPainter(const std::type_info& t, PaintFn fn) {
    registry()[std::type_index(t)] = fn;
}

PaintFn findPainter(const std::type_info& t) {
    auto it = registry().find(std::type_index(t));
    return it == registry().end() ? nullptr : it->second;
}

void clearPainters() {
    registry().clear();
}

} // namespace fw2
} // namespace ui
} // namespace yawn
