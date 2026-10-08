/* Identity address map: the weak default for a link without the recompiled code (unit tests, the
 * build checks with tools/recomp/stubgen.py). build/gen/table.c defines these for real, and its
 * strong definitions win wherever the game code is linked in. See runtime/include/guest_addr.h. */
#include "guest_addr.h"

__attribute__((weak)) const GuestStep g_guest_code_steps[] = {{0u, 0}};
__attribute__((weak)) const unsigned g_guest_code_step_count = 1u;
__attribute__((weak)) const GuestStep g_guest_data_steps[] = {{0u, 0}};
__attribute__((weak)) const unsigned g_guest_data_step_count = 1u;
__attribute__((weak)) const char g_guest_build_name[] = "USA";
__attribute__((weak)) const char g_guest_build_title_id[] = "0005000010143500";

__attribute__((weak)) const uint32_t g_guest_code_lo = 0, g_guest_code_hi = 0;
__attribute__((weak)) const uint32_t g_guest_data_lo = 0, g_guest_data_hi = 0;
__attribute__((weak)) const GuestChanged g_guest_changed_code[] = {{0u, 0u}};
__attribute__((weak)) const unsigned g_guest_changed_code_count = 0;
