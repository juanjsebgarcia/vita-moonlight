#pragma once

#include <stdint.h>

// Video packet loss, measured where moonlight-common-c receives packets (its
// own recovered and failed counters are only kept for audio). received counts
// the video packets that arrived; lost counts gaps in their RTP sequence
// numbers, less packets that turned up late to fill one. Compare two readings
// to measure a stretch of the stream.
void netcount_read(uint32_t *received, uint32_t *lost);
