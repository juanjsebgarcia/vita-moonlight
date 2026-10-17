#pragma once

#include <stdint.h>

// Video packet loss, measured where moonlight-common-c receives packets (its
// own recovered and failed counters are only kept for audio), from the RTP
// sequence numbers the host gives every video packet in turn. Compare two
// readings to measure a stretch of the stream.
typedef struct {
  uint32_t received;  // video packets that arrived, repeats not included
  uint32_t gaps;      // skipped over in the sequence, whether or not they arrived later
  uint32_t late;      // arrived after a newer packet, out of order, filling a gap
  uint32_t repeats;   // arrived more than once
} netcount;

// Packets that never arrived between two readings: the gaps less those
// filled late. Kept as a difference of differences, since late can run
// ahead of gaps for a moment.
uint32_t netcount_lost(const netcount *start, const netcount *end);

void netcount_read(netcount *out);

// Call before a stream starts, while no video is being received
void netcount_reset(void);
