// Shared state of the hot-reloadable core's translation units.
#pragma once
#include "../common/host_api.h"

extern const Bg3HostApi* host;

// gi.cpp
void gi_on_dispatch(VkCommandBuffer cmd, const Bg3DispatchInfo* d);
void gi_on_dispatch_pre(VkCommandBuffer cmd, const Bg3DispatchInfo* d);
void gi_on_pass_begin(VkCommandBuffer cmd, const Bg3PassInfo* p);
void gi_on_cmd_end(VkCommandBuffer cmd);
void gi_on_pass_end(VkCommandBuffer cmd, const Bg3PassInfo* p);
void gi_on_frame(uint64_t frame);
void gi_release();
