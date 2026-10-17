#pragma once

#include <stdint.h>

// Video packet loss, measured where moonlight-common-c receives packets (its
// own recovered and failed counters are only kept for audio), from the RTP
// sequence numbers the host gives every video packet in turn. Compare two
// readings to measure a stretch of the stream.
typedef struct {
  uint32_t received;  // video packets that arrived, repeats not included
  uint32_t lost;      // never arrived
  uint32_t late;      // arrived after a newer packet, out of order
  uint32_t repeats;   // arrived more than once
} netcount;

void netcount_read(netcount *out);
