#include "netcount.h"

#include <stdbool.h>

// Video packets are a full shard: the packet size (1024 bytes or more) plus
// headers. Only the video socket gets packets this big, and once one has,
// only that socket's full-size packets are counted.
#define VIDEO_PACKET_MIN_BYTES 1000
// Remembers this many recent sequence numbers, to tell a late packet (which
// fills a gap) from a repeat (which doesn't)
#define SEEN_WINDOW 4096

// Written only by common-c's video receive thread, so no lock is needed
static volatile uint32_t received = 0;
static volatile uint32_t gaps = 0;
static volatile uint32_t late = 0;
static bool have_sequence = false;
static uint16_t last_sequence = 0;
static int video_socket = -1;
// seen[n % SEEN_WINDOW] holds n + 1 for the last sequence number n that
// landed there, so 0 means empty
static uint32_t seen[SEEN_WINDOW];

static bool already_seen(uint16_t sequence) {
  return seen[sequence % SEEN_WINDOW] == (uint32_t)sequence + 1;
}

static void mark_seen(uint16_t sequence) {
  seen[sequence % SEEN_WINDOW] = (uint32_t)sequence + 1;
}

// Wrapped at link time (-Wl,--wrap=recvUdpSocket), so common-c needs no change
int __real_recvUdpSocket(int s, char *buffer, int size, bool useSelect);
int __wrap_recvUdpSocket(int s, char *buffer, int size, bool useSelect) {
  int ret = __real_recvUdpSocket(s, buffer, size, useSelect);
  if (ret < VIDEO_PACKET_MIN_BYTES) {
    return ret;
  }
  if (s != video_socket) {
    // A new stream's video socket: its sequence numbers start afresh
    video_socket = s;
    have_sequence = false;
    for (int i = 0; i < SEEN_WINDOW; i++) {
      seen[i] = 0;
    }
  }
  // The RTP header's sequence number, big-endian at bytes 2 and 3. The host
  // numbers every video packet in turn, so a gap is a packet lost on the way,
  // unless it turns up late.
  uint16_t sequence = (uint16_t)(((uint8_t)buffer[2] << 8) | (uint8_t)buffer[3]);
  if (already_seen(sequence)) {
    return ret;
  }
  mark_seen(sequence);
  received++;
  if (!have_sequence) {
    have_sequence = true;
    last_sequence = sequence;
    return ret;
  }
  uint16_t step = (uint16_t)(sequence - last_sequence);
  if (step >= 0x8000) {
    // Behind the newest packet and not seen before: it fills a gap
    late++;
    return ret;
  }
  gaps += step - 1;
  last_sequence = sequence;
  return ret;
}

void netcount_read(uint32_t *received_out, uint32_t *lost_out) {
  *received_out = received;
  *lost_out = gaps > late ? gaps - late : 0;
}
