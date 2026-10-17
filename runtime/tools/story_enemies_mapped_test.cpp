// SPDX-License-Identifier: MPL-2.0
// Strong nonidentity tables override the weak test fallback. Canonical global
// pages are poisoned by the shared fixture; only mapped data may be touched.
#include "guest_addr.h"
extern "C" {
extern const GuestStep g_guest_code_steps[]={{0x02000000u,0x00100000}};
extern const unsigned g_guest_code_step_count=1;
extern const GuestStep g_guest_data_steps[]={{0x10000000u,0x00200000}};
extern const unsigned g_guest_data_step_count=1;
extern const uint32_t g_guest_code_lo=0x02000000u,g_guest_code_hi=0x03000000u;
extern const uint32_t g_guest_data_lo=0x10000000u,g_guest_data_hi=0x10500000u;
extern const GuestChanged g_guest_changed_code[]={{0,0}};
extern const unsigned g_guest_changed_code_count=0;
extern const char g_guest_build_name[]="SYNTHETIC_NONIDENTITY";
extern const char g_guest_build_title_id[]="synthetic-test-only";
}
#define STORY_ENEMIES_TEST_CODE_DELTA 0x00100000u
#include "story_enemies_test.cpp"
