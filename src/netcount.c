#include "netcount.h"

#include <stdbool.h>

// Video packets are a full shard, the packet size plus headers; audio packets
// are a few hundred bytes at most
#define VIDEO_PACKET_MIN_BYTES 512

static volatile uint32_t video_packets = 0;

// Wrapped at link time (-Wl,--wrap=recvUdpSocket), so common-c needs no change
int __real_recvUdpSocket(int s, char *buffer, int size, bool useSelect);
int __wrap_recvUdpSocket(int s, char *buffer, int size, bool useSelect) {
  int ret = __real_recvUdpSocket(s, buffer, size, useSelect);
  // Only the video receive thread gets packets this big, so no lock is needed
  if (ret >= VIDEO_PACKET_MIN_BYTES) {
    video_packets++;
  }
  return ret;
}

uint32_t netcount_video_packets(void) {
  return video_packets;
}
