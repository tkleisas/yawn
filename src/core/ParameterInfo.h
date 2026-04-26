#pragma once

#include "WidgetHint.h"

namespace yawn {

struct ParameterInfo {
    const char* name    = "";
    float minValue      = 0.0f;
    float maxValue      = 1.0f;
    float defaultValue  = 0.0f;
    const char* unit    = "";
    bool isBoolean      = false;
    bool isPerVoice     = false;
    WidgetHint widgetHint = WidgetHint::Knob;
    const char* const* valueLabels = nullptr;
    int valueLabelCount = 0;

    using FormatFn = void (*)(float value, char* buf, int bufSize);
    FormatFn formatFn = nullptr;
};

} // namespace yawn
