/* Pre-included into every decompiled game unit on Android only (cmake/Decomp.cmake): bionic's C
 * library declares a few names the decompilation also uses, which macOS's does not (or not in the
 * same headers). The system headers are included here first, so their include guards keep them from
 * coming back later in the unit:
 * - <strings.h> defines bzero and bcopy as macros (__bionic_bzero, __bionic_bcopy), which would
 *   rewrite the decompilation's own bzero and JASystem::Calc::bcopy (the game's functions);
 * - <stdlib.h> includes <malloc.h>, which includes <stdio.h>, so C's remove(const char*) would
 *   overload the decompiled actors' file-static remove() (d_a_obj_volcano and others), and taking
 *   its address in the glue would be ambiguous. stdio's declaration is made under another name
 *   (declaration only; a decompiled unit never deletes host files, and a call would not compile). */
#pragma once
#define remove wwhd_bionic_stdio_remove
#include <stdio.h>
#include <cstdio>
#include <stdlib.h>
#include <cstdlib>
#undef remove
#include <string.h>
#include <strings.h>
#undef bzero
#undef bcopy
