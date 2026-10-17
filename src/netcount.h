#pragma once

#include <stdint.h>

// Video packets that actually arrived from the host, counted where
// moonlight-common-c receives them. Its own recovered and failed counters are
// only kept for audio, so this is the way to see video packet loss: compare
// it with the packets the host announced (RTP_VIDEO_STATS packetCountVideo
// plus packetCountFec) over the same stretch.
uint32_t netcount_video_packets(void);
