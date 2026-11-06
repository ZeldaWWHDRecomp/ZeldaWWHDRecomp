#pragma once

int mainRandomize();

#ifdef WWHD_APP
#include <seedgen/config.hpp>
int wwhdRandomize(const Config& config); // WWHD: one run with a config the app loaded (rando_jni.cpp)
#endif
