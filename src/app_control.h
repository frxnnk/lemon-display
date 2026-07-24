#pragma once

#include "ui_dashboard.h"

// App-level actions owned by main.cpp. HTTP/settings callers should use these
// instead of setting dashboard internals directly, because some modes have
// scheduler, layout, and redraw side effects.
void appApplyZ2Mode(Z2Mode mode);
