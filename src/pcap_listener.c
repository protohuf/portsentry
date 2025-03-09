// SPDX-FileCopyrightText: 2024 Marcus Hufvudsson <mh@protohuf.com>
//
// SPDX-License-Identifier: CPL-1.0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netdb.h>

#include "port.h"
#include "portsentry.h"
#include "pcap_listener.h"
#include "config_data.h"
#include "io.h"
#include "util.h"
#include "pcap_device.h"

static uint8_t CreateAndAddDevice(struct ListenerModule *lm, const char *name);
static int AutoPrepDevices(struct ListenerModule *lm, const uint8_t includeLo);
static int PrepDevices(struct ListenerModule *lm);
static void PrintDevices(const struct ListenerModule *lm);

static uint8_t CreateAndAddDevice(struct ListenerModule *lm, const char *name) {
  struct Device *dev;

  assert(lm != NULL);

  if (FindDeviceByName(lm, name) != NULL) {
    Error("Device %s appears twice", name);
    return FALSE;
  }

  if ((dev = CreateDevice(name)) == NULL) {
    return FALSE;
  }

  if (AddDevice(lm, dev) == FALSE) {
    Error("Unable to add device %s", name);
    FreeDevice(dev);
    return FALSE;
  }

  return TRUE;
}

static int AutoPrepDevices(struct ListenerModule *lm, const uint8_t includeLo) {
  pcap_if_t *alldevs, *d;
  char errbuf[PCAP_ERRBUF_SIZE];

  if (pcap_findalldevs(&alldevs, errbuf) == PCAP_ERROR) {
    Error("Unable to retrieve network interfaces: %s", errbuf);
    return FALSE;
  }

  for (d = alldevs; d != NULL; d = d->next) {
    if (includeLo == FALSE && ((d->flags & PCAP_IF_LOOPBACK) != 0)) {
      continue;
    }

    // When using ALL or ALL_NLO (and thus use pcap_findalldevs()), don't include the "any" device
    if ((strncmp(d->name, "any", 3) == 0) && strlen(d->name) == 3) {
      continue;
    }

    Debug("Adding device %s", d->name);
    if (CreateAndAddDevice(lm, d->name) == FALSE) {
      Error("Unable to add device %s, skipping", d->name);
    }
  }

  pcap_freealldevs(alldevs);
  return TRUE;
}

static int PrepDevices(struct ListenerModule *lm) {
  int i;

  assert(lm != NULL);
  assert(GetNoInterfaces(&configData) > 0);

  if (strncmp(configData.interfaces[0], "ALL_NLO", (IF_NAMESIZE - 1)) == 0) {
    if (AutoPrepDevices(lm, FALSE) == FALSE) {
      return FALSE;
    }
  } else if (strncmp(configData.interfaces[0], "ALL", (IF_NAMESIZE - 1)) == 0) {
    if (AutoPrepDevices(lm, TRUE) == FALSE) {
      return FALSE;
    }
  } else {
    i = 0;
    while (configData.interfaces[i] != NULL) {
      if (CreateAndAddDevice(lm, configData.interfaces[i]) == FALSE) {
        Error("Unable to add device %s, skipping", configData.interfaces[i]);
      }
      i++;
    }
  }

  if (lm->root == NULL) {
    Error("No network devices could be added");
    return FALSE;
  }

  return TRUE;
}

static int RetrieveAddresses(struct ListenerModule *lm) {
  int status = TRUE;
  struct ifaddrs *ifaddrs = NULL, *ifa = NULL;
  struct Device *dev;
  char err[ERRNOMAXBUF];
  char host[NI_MAXHOST];

  if (getifaddrs(&ifaddrs) == -1) {
    Error("Unable to retrieve network addresses: %s", ErrnoString(err, ERRNOMAXBUF));
    status = FALSE;
    goto cleanup;
  }

  for (ifa = ifaddrs; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_addr == NULL) {
      continue;
    }

    if (ifa->ifa_addr->sa_family != AF_INET && ifa->ifa_addr->sa_family != AF_INET6) {
      continue;
    }

    for (dev = lm->root; dev != NULL; dev = dev->next) {
      if (strncmp(dev->name, ifa->ifa_name, strlen(dev->name)) == 0) {
        if (getnameinfo(ifa->ifa_addr, (ifa->ifa_addr->sa_family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6), host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST) == -1) {
          Crash(1, "Unable to retrieve network addresses for device %s: %s", dev->name, ErrnoString(err, ERRNOMAXBUF));
        }

        if (strncmp(host, "fe80", 4) == 0) {
          continue;
        } else if (strncmp(host, "169.254", 7) == 0) {
          continue;
        }

        Debug("Found address %s for device %s: %s", ifa->ifa_name, dev->name, host);

        if (ifa->ifa_addr->sa_family == AF_INET) {
          AddAddress(dev, host, AF_INET);
        } else if (ifa->ifa_addr->sa_family == AF_INET6) {
          AddAddress(dev, host, AF_INET6);
        } else {
          Error("Unknown address family %d for address %s, ignoring", ifa->ifa_addr->sa_family, host);
        }
      }
    }
  }

cleanup:
  if (ifaddrs != NULL) {
    freeifaddrs(ifaddrs);
  }

  return status;
}

int GetNoDevices(const struct ListenerModule *lm) {
  int count;
  struct Device *current;

  assert(lm != NULL);

  count = 0;
  current = lm->root;
  while (current != NULL) {
    count++;
    current = current->next;
  }

  return count;
}

int GetNoRunningDevices(const struct ListenerModule *lm) {
  int count;
  struct Device *current;

  assert(lm != NULL);

  count = 0;
  current = lm->root;
  while (current != NULL) {
    if (current->state == DEVICE_STATE_RUNNING) {
      count++;
    }
    current = current->next;
  }

  return count;
}

struct ListenerModule *AllocListenerModule(void) {
  struct ListenerModule *lm;

  if ((lm = malloc(sizeof(struct ListenerModule))) == NULL) {
    Error("Unable to allocate memory for listener module");
    return NULL;
  }

  memset(lm, 0, sizeof(struct ListenerModule));

  return lm;
}

void FreeListenerModule(struct ListenerModule *lm) {
  struct Device *current, *next;

  if (lm == NULL) {
    return;
  }

  current = lm->root;
  while (current != NULL) {
    next = current->next;
    FreeDevice(current);
    current = next;
  }

  free(lm);
}

int InitListenerModule(struct ListenerModule *lm) {
  struct Device *current;

  if (PrepDevices(lm) == FALSE) {
    return FALSE;
  }

  RetrieveAddresses(lm);

  current = lm->root;
  while (current != NULL) {
    StartDevice(current);
    current = current->next;
  }

  if (lm->root == NULL) {
    Error("No network devices could be initiated, stopping");
    return FALSE;
  }

  if ((configData.logFlags & LOGFLAG_VERBOSE) != 0) {
    PrintDevices(lm);
  }

  return TRUE;
}

uint8_t AddDevice(struct ListenerModule *lm, struct Device *add) {
  struct Device *current;

  if (lm == NULL || add == NULL) {
    return FALSE;
  }

  if (FindDeviceByName(lm, add->name) != NULL) {
    Verbose("Device %s already specified", add->name);
    return FALSE;
  }

  if (lm->root == NULL) {
    lm->root = add;
  } else {
    current = lm->root;
    while (current->next != NULL) {
      current = current->next;
    }

    current->next = add;
  }

  return TRUE;
}

uint8_t RemoveDevice(struct ListenerModule *lm, const struct Device *remove) {
  struct Device *current, *previous;

  if (lm == NULL || remove == NULL) {
    return FALSE;
  }

  current = lm->root;
  previous = NULL;
  while (current != NULL) {
    if (current == remove) {
      if (previous == NULL) {
        lm->root = current->next;
      } else {
        previous->next = current->next;
      }

      FreeDevice(current);
      return TRUE;
    }

    previous = current;
    current = current->next;
  }

  return FALSE;
}

struct Device *FindDeviceByName(const struct ListenerModule *lm, const char *name) {
  struct Device *current;

  assert(lm != NULL);
  assert(name != NULL);

  current = lm->root;
  while (current != NULL) {
    if (strncmp(current->name, name, (IF_NAMESIZE - 1)) == 0) {
      return current;
    }

    current = current->next;
  }

  return NULL;
}

struct Device *FindDeviceByIpAddr(const struct ListenerModule *lm, const char *ip_addr) {
  struct Device *current;
  int i;

  assert(lm != NULL);
  assert(ip_addr != NULL);

  current = lm->root;
  while (current != NULL) {
    for (i = 0; i < current->inet4_addrs_count; i++) {
      if (strcmp(current->inet4_addrs[i], ip_addr) == 0) {
        return current;
      }
    }

    for (i = 0; i < current->inet6_addrs_count; i++) {
      if (strcmp(current->inet6_addrs[i], ip_addr) == 0) {
        return current;
      }
    }

    current = current->next;
  }

  return NULL;
}

struct pollfd *SetupPollFds(const struct ListenerModule *lm, int *nfds) {
  struct pollfd *fds = NULL;
  struct Device *current = NULL;
  int i = 0;

  if ((fds = malloc(sizeof(struct pollfd) * GetNoRunningDevices(lm))) == NULL) {
    Error("Unable to allocate memory for pollfd");
    return NULL;
  }

  current = lm->root;
  while (current != NULL) {
    if (current->state == DEVICE_STATE_RUNNING) {
      fds[i].fd = current->fd;
      fds[i].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI;
      fds[i].revents = 0;
      i++;
    }

    current = current->next;
  }

  // Currently a redundant check, but it's here to make sure we don't have/pickup any issues
  if (i != GetNoRunningDevices(lm)) {
    Crash(1, "Number of running devices does not match number of pollfd");
  }

  *nfds = i;

  return fds;
}

struct pollfd *AddPollFd(struct pollfd *fds, int *nfds, const int fd) {
  struct pollfd *newFds = NULL;

  if ((newFds = malloc(sizeof(struct pollfd) * (*nfds + 1))) == NULL) {
    Error("Unable to allocate memory for pollfd");
    return NULL;
  }

  memcpy(newFds, fds, sizeof(struct pollfd) * (*nfds));

  newFds[*nfds].fd = fd;
  newFds[*nfds].events = POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI;
  newFds[*nfds].revents = 0;
  *nfds += 1;

  free(fds);

  return newFds;
}

struct pollfd *RemovePollFd(struct pollfd *fds, int *nfds, const int fd) {
  int i, j;
  struct pollfd *newFds = NULL;

  if ((newFds = malloc(sizeof(struct pollfd) * (*nfds - 1))) == NULL) {
    Error("Unable to allocate memory for pollfd");
    return NULL;
  }

  for (i = 0, j = 0; i < *nfds; i++) {
    if (fds[i].fd == fd) {
      continue;
    }

    newFds[j].fd = fds[i].fd;
    newFds[j].events = fds[i].events;
    newFds[j].revents = fds[i].revents;
    j++;
  }

  free(fds);
  *nfds -= 1;

  return newFds;
}

struct Device *GetDeviceByFd(const struct ListenerModule *lm, const int fd) {
  for (struct Device *current = lm->root; current != NULL; current = current->next) {
    if (current->fd == fd) {
      return current;
    }
  }

  return NULL;
}

static void PrintDevices(const struct ListenerModule *lm) {
  int i;
  struct Device *current;

  if (lm == NULL) {
    return;
  }

  current = lm->root;
  while (current != NULL) {
    Verbose("Ready Device: %s pcap handle: %p, fd: %d", current->name, (void *)current->handle, current->fd);

    for (i = 0; i < current->inet4_addrs_count; i++) {
      Verbose("  inet4 addr: %s", current->inet4_addrs[i]);
    }

    for (i = 0; i < current->inet6_addrs_count; i++) {
      Verbose("  inet6 addr: %s", current->inet6_addrs[i]);
    }

    current = current->next;
  }
}
