/* Test an independently shifted regional code/data map. */
#include "guest_addr.h"
const GuestStep g_guest_code_steps[]={{0,0x1000}};
const unsigned g_guest_code_step_count=1;
const GuestStep g_guest_data_steps[]={{0,0x10000}};
const unsigned g_guest_data_step_count=1;
const uint32_t g_guest_code_lo=0,g_guest_code_hi=0,g_guest_data_lo=0,g_guest_data_hi=0;
const GuestChanged g_guest_changed_code[]={{0,0}};
const unsigned g_guest_changed_code_count=0;
const char g_guest_build_name[]="regional-fixture";
const char g_guest_build_title_id[]="fixture";
