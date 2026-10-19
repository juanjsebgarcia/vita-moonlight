#pragma once

#include <stddef.h>
#include <stdint.h>

// A PC's address as the player types it: an IPv4 address or a name, with
// ":port" when the PC's HTTP port isn't the default (Sunshine moved to
// another port, several Sunshine instances, port forwarding). No platform
// calls, so it builds and tests on any machine.

// The GameStream HTTP port. HTTPS and the streaming ports come from the PC.
#define HOST_DEFAULT_PORT 47989

typedef enum {
  HOST_ADDRESS_OK,
  HOST_ADDRESS_NO_HOST,   // empty, or only ":port"
  HOST_ADDRESS_BAD_PORT,  // not a number from 1 to 65535
  HOST_ADDRESS_IPV6,      // the Vita has no IPv6
} host_address_result;

// Splits text into host and port, ignoring spaces around it. Without a port,
// the port is HOST_DEFAULT_PORT. On an error, host and port are untouched.
host_address_result host_address_parse(const char *text, char *host, size_t host_size, uint16_t *port);

// Writes host, then ":port" if the port isn't the default
void host_address_format(const char *host, uint16_t port, char *out, size_t size);
