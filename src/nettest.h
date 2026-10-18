#pragma once

#include <stdbool.h>
#include <stddef.h>

// The network test: whether the network the Vita is on lets Moonlight's
// streaming ports out to the internet. It asks Moonlight's public test
// server (the one moonlight-qt uses) to answer on every port, with
// moonlight-common-c's LiTestClientConnectivity(). It says nothing about
// the PC's own firewall or router.

#define NETTEST_SERVER "qt.conntest.moonlight-stream.org"
// Checked first, to tell "no internet" from "these ports are blocked"
#define NETTEST_REFERENCE_PORT 443

// Starts the test on a thread of its own, taking a few seconds. Returns false
// if it couldn't start, or one is already running.
bool nettest_start(void);

// Returns true once the test nettest_start() began has finished, with its
// result: 0 when every port got through, ML_TEST_RESULT_INCONCLUSIVE when
// the test server couldn't be reached at all, or else the ML_PORT_FLAG_*
// bits of the blocked ports
bool nettest_poll(unsigned int *result);

// Writes ML_PORT_FLAG_* bits as one line per protocol, with neighbouring
// ports as a range, such as "TCP 47984, 47989\nUDP 47998–48000". Empty when
// no known port is set.
void nettest_describe_ports(unsigned int port_flags, char *out, size_t size);
