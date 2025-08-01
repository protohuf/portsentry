// SPDX-FileCopyrightText: 2024 Marcus Hufvudsson <mh@protohuf.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once
#include <net/if.h>
#include <pcap.h>

enum DeviceState {
  DEVICE_STATE_STOPPED = 0,
  DEVICE_STATE_RUNNING,
  DEVICE_STATE_ERROR,
};

struct Device {
  pcap_t *handle;
  char name[IF_NAMESIZE];
  int fd;
  enum DeviceState state;

  char **inet4_addrs;
  size_t inet4_addrs_count;
  char **inet6_addrs;
  size_t inet6_addrs_count;

  struct Device *next;
};

struct Device *CreateDevice(const char *name);
uint8_t FreeDevice(struct Device *device);
int StartDevice(struct Device *device);
uint8_t StopDevice(struct Device *device);

int AddAddress(struct Device *device, const char *address, const int type);
int AddressExists(const struct Device *device, const char *address, const int type);
size_t GetNoAddresses(const struct Device *device);
int RemoveAddress(struct Device *device, const char *address);
void RemoveAllAddresses(struct Device *device);
int SetAllAddresses(struct Device *device);

int SetupFilter(const struct Device *device);
