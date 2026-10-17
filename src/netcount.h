#pragma once

#include <stdint.h>

// Video packet loss, measured where moonlight-common-c receives packets (its
// own recovered and failed counters are only kept for audio). received counts
// the video packets that arrived; expected counts those the host sent, from
// the RTP sequence numbers, so expected minus received is the packets lost on
// the way. Compare two readings to measure a stretch of the stream.
void netcount_read(uint32_t *received, uint32_t *expected);
