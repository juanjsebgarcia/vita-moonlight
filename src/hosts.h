#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "device.h"

// Looking after the paired PCs: waking them, renaming them, changing their
// address and forgetting them, without any menus or dialogs. Anything to
// tell the player comes back as text.

typedef enum {
  HOST_WAKE_SENT,
  HOST_WAKE_NO_MAC,       // never connected while on, so its MAC isn't known
  HOST_WAKE_NO_ADDRESS,
  HOST_WAKE_BAD_ADDRESS,  // not an IPv4 address Wake on LAN can broadcast to
  HOST_WAKE_SEND_FAILED,
} host_wake_result;

// Sends the magic packet to the host's subnet. On failure, writes why to err.
host_wake_result host_send_wake(const device_info_t *info, char *err, size_t err_len);

typedef enum {
  HOST_WAIT_ONLINE,
  HOST_WAIT_CANCELLED,
  HOST_WAIT_TIMED_OUT,
  HOST_WAIT_FAILED,  // the packet couldn't be sent
} host_wait_result;

// Wakes the host and waits until it answers, resending the packet now and
// then, showing progress and stopping if the player asks (src/ui_ops.h).
// When it answers at a new address, writes it to new_address (else empty).
// Writes what to tell the player to message, except when it comes online.
host_wait_result host_wake_and_wait(device_info_t *info, char *new_address, size_t new_address_size,
                                    char *message, size_t message_size);

// Forgets the host and its pairing keys. Returns false if it couldn't.
bool host_forget(device_info_t *info);

typedef enum {
  HOST_UNPAIR_DONE,            // the PC forgot this Vita, and this Vita the PC
  HOST_UNPAIR_PC_REFUSED,      // forgotten here; the PC answered but kept it (Sunshine)
  HOST_UNPAIR_PC_UNREACHABLE,  // forgotten here; the PC couldn't be asked
  HOST_UNPAIR_FORGET_FAILED,   // the PC may have been told, but it's still here
} host_unpair_result;

// Asks the PC to forget this Vita, if it answers, then forgets the host and
// its pairing keys whatever the PC said. Ends the connection to it, if there
// is one. info is gone afterwards, as with host_forget.
host_unpair_result host_unpair(device_info_t *info);

// Uses address for the host from now on, and saves it
void host_set_address(device_info_t *info, const char *address);

// Shows the host as label from now on, and saves it. Its name, which finds
// it on the network and holds its pairing keys, stays the same; giving the
// name itself as the label clears it.
void host_set_label(device_info_t *info, const char *label);
