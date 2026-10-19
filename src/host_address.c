#include "host_address.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

host_address_result host_address_parse(const char *text, char *host, size_t host_size, uint16_t *port) {
  const char *start = text;
  while (*start && isspace((unsigned char)*start)) {
    start++;
  }
  const char *end = start + strlen(start);
  while (end > start && isspace((unsigned char)end[-1])) {
    end--;
  }
  if (memchr(start, '[', end - start) != NULL) {
    return HOST_ADDRESS_IPV6;
  }
  const char *colon = memchr(start, ':', end - start);
  if (colon != NULL && memchr(colon + 1, ':', end - colon - 1) != NULL) {
    return HOST_ADDRESS_IPV6;
  }

  const char *host_end = colon != NULL ? colon : end;
  if (host_end == start) {
    return HOST_ADDRESS_NO_HOST;
  }
  long value = HOST_DEFAULT_PORT;
  if (colon != NULL) {
    const char *digits = colon + 1;
    // At most five digits, so the value can't overflow
    if (digits == end || end - digits > 5) {
      return HOST_ADDRESS_BAD_PORT;
    }
    value = 0;
    for (const char *p = digits; p < end; p++) {
      if (!isdigit((unsigned char)*p)) {
        return HOST_ADDRESS_BAD_PORT;
      }
      value = value * 10 + (*p - '0');
    }
    if (value < 1 || value > 65535) {
      return HOST_ADDRESS_BAD_PORT;
    }
  }
  size_t len = host_end - start;
  if (len >= host_size) {
    return HOST_ADDRESS_NO_HOST;
  }
  memcpy(host, start, len);
  host[len] = '\0';
  *port = (uint16_t)value;
  return HOST_ADDRESS_OK;
}

void host_address_format(const char *host, uint16_t port, char *out, size_t size) {
  if (port == 0 || port == HOST_DEFAULT_PORT) {
    snprintf(out, size, "%s", host);
  } else {
    snprintf(out, size, "%s:%u", host, (unsigned)port);
  }
}
