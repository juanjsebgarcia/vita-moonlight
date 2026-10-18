#include "nettest.h"

#include "debug.h"

#include <stdio.h>
#include <string.h>

#include <netinet/in.h>
#include <psp2/kernel/threadmgr.h>
#include <Limelight.h>

#define NETTEST_KNOWN_PORTS (ML_PORT_FLAG_TCP_47984 | ML_PORT_FLAG_TCP_47989 | ML_PORT_FLAG_TCP_48010 | \
                             ML_PORT_FLAG_UDP_47998 | ML_PORT_FLAG_UDP_47999 | ML_PORT_FLAG_UDP_48000 | \
                             ML_PORT_FLAG_UDP_48010)
// One bit per port index, as in moonlight-common-c
#define PORT_INDEX_COUNT 32

static SceUID test_thread = -1;
static volatile bool test_done = false;
static volatile unsigned int test_result = 0;

static int nettest_main(SceSize args, void *argp) {
  unsigned int result = LiTestClientConnectivity(NETTEST_SERVER, NETTEST_REFERENCE_PORT, ML_PORT_FLAG_ALL);
  if (result == ML_TEST_RESULT_INCONCLUSIVE) {
    vita_debug_log("nettest: couldn't reach %s on TCP %d\n", NETTEST_SERVER, NETTEST_REFERENCE_PORT);
  } else if (result == 0) {
    vita_debug_log("nettest: every port got through\n");
  } else {
    char ports[128];
    LiStringifyPortFlags(result, ", ", ports, sizeof(ports));
    vita_debug_log("nettest: blocked %s\n", ports);
  }
  test_result = result;
  test_done = true;
  return sceKernelExitDeleteThread(0);
}

bool nettest_start(void) {
  if (test_thread >= 0) {
    return false;
  }
  test_done = false;
  // Self-deleting, so nothing needs cleaning up if nobody polls it
  test_thread = sceKernelCreateThread("nettest", nettest_main, 0x10000100, 0x10000, 0, 0, NULL);
  if (test_thread < 0) {
    vita_debug_log("nettest: sceKernelCreateThread 0x%x\n", test_thread);
    test_thread = -1;
    return false;
  }
  if (sceKernelStartThread(test_thread, 0, NULL) < 0) {
    sceKernelDeleteThread(test_thread);
    test_thread = -1;
    return false;
  }
  return true;
}

bool nettest_poll(unsigned int *result) {
  if (test_thread < 0 || !test_done) {
    return false;
  }
  test_thread = -1;
  *result = test_result;
  return true;
}

// Appends text, stopping at the end of the buffer
static void append(char *out, size_t size, size_t *used, const char *text) {
  int written = snprintf(out + *used, size - *used, "%s", text);
  if (written > 0) {
    *used += (size_t)written < size - *used ? (size_t)written : size - *used - 1;
  }
}

void nettest_describe_ports(unsigned int port_flags, char *out, size_t size) {
  size_t used = 0;
  if (size == 0) {
    return;
  }
  out[0] = '\0';
  port_flags &= NETTEST_KNOWN_PORTS;

  static const struct { int protocol; const char *name; } protocols[] = {
    {IPPROTO_TCP, "TCP"},
    {IPPROTO_UDP, "UDP"},
  };
  for (size_t p = 0; p < sizeof(protocols) / sizeof(protocols[0]); p++) {
    // Port indices list each protocol's ports in rising order
    unsigned short ports[PORT_INDEX_COUNT];
    int count = 0;
    for (int i = 0; i < PORT_INDEX_COUNT; i++) {
      if ((port_flags & (1U << i)) && LiGetProtocolFromPortFlagIndex(i) == protocols[p].protocol) {
        ports[count++] = LiGetPortFromPortFlagIndex(i);
      }
    }
    if (count == 0) {
      continue;
    }
    if (used > 0) {
      append(out, size, &used, "\n");
    }
    append(out, size, &used, protocols[p].name);
    for (int i = 0; i < count; ) {
      int last = i;
      while (last + 1 < count && ports[last + 1] == ports[last] + 1) {
        last++;
      }
      char part[24];
      if (last > i) {
        snprintf(part, sizeof(part), "%s%u–%u", i ? ", " : " ", ports[i], ports[last]);
      } else {
        snprintf(part, sizeof(part), "%s%u", i ? ", " : " ", ports[i]);
      }
      append(out, size, &used, part);
      i = last + 1;
    }
  }
}
