#pragma once

// WWHD: progress reports go to the app (runtime/src/rando.cpp); the Qt dialog is not built
#include <string>

namespace wwhd_rando {
void progress_value(int percent);
void progress_label(const std::string& label);
}

#define UPDATE_DIALOG_VALUE(val) wwhd_rando::progress_value(val);
#define UPDATE_DIALOG_LABEL(label) wwhd_rando::progress_label(label);
#define UPDATE_DIALOG_TITLE(title) (void)(title);
