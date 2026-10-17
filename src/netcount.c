#include "netcount.h"

#include <stdbool.h>

// Video packets are a full shard: the packet size (1024 bytes or more) plus
// headers. Only the video socket gets packets this big, and once one has,
// only that socket is counted, so audio never is.
#define VIDEO_PACKET_MIN_BYTES 1000

// Written only by common-c's video receive thread, the only one that gets
// packets this big, so no lock is needed
static volatile uint32_t received = 0;
static volatile uint32_t expected = 0;
static bool have_sequence = false;
static uint16_t last_sequence = 0;
static int video_socket = -1;

// Wrapped at link time (-Wl,--wrap=recvUdpSocket), so common-c needs no change
int __real_recvUdpSocket(int s, char *buffer, int size, bool useSelect);
int __wrap_recvUdpSocket(int s, char *buffer, int size, bool useSelect) {
  int ret = __real_recvUdpSocket(s, buffer, size, useSelect);
  if (ret <= 0) {
    return ret;
  }
  if (s != video_socket) {
    if (ret < VIDEO_PACKET_MIN_BYTES) {
      return ret;
    }
    // A new stream's video socket: its sequence numbers start afresh
    video_socket = s;
    have_sequence = false;
  }
  // The RTP header's sequence number, big-endian at bytes 2 and 3. The host
  // numbers every video packet in turn, so a gap is a packet lost on the way.
  uint16_t sequence = (uint16_t)(((uint8_t)buffer[2] << 8) | (uint8_t)buffer[3]);
  received++;
  if (!have_sequence) {
    have_sequence = true;
    expected++;
  } else {
    uint16_t step = (uint16_t)(sequence - last_sequence);
    // A step backwards is a late or repeated packet, already counted as expected
    if (step == 0 || step >= 0x8000) {
      return ret;
    }
    expected += step;
  }
  last_sequence = sequence;
  return ret;
}

void netcount_read(uint32_t *received_out, uint32_t *expected_out) {
  *received_out = received;
  *expected_out = expected;
}
