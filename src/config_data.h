// SPDX-FileCopyrightText: 2024 Marcus Hufvudsson <mh@protohuf.com>
//
// SPDX-License-Identifier: BSD-2-Clause

#pragma once
#include <limits.h>
#include <stdint.h>
#include <net/if.h>

#include "portsentry.h"
#include "port.h"

extern const uint8_t LOGFLAG_NONE;
extern const uint8_t LOGFLAG_DEBUG;
extern const uint8_t LOGFLAG_VERBOSE;
extern const uint8_t LOGFLAG_OUTPUT_STDOUT;
extern const uint8_t LOGFLAG_OUTPUT_SYSLOG;

enum SentryMode { SENTRY_MODE_STEALTH = 0,
                  SENTRY_MODE_CONNECT };

enum SentryMethod { SENTRY_METHOD_PCAP = 0,
                    SENTRY_METHOD_RAW };

struct ConfigData {
  char killRoute[MAXBUF];
  char killHostsDeny[MAXBUF];
  char killRunCmd[MAXBUF];

  char **interfaces;

  struct Port *tcpPorts;
  size_t tcpPortsLength;
  struct Port *udpPorts;
  size_t udpPortsLength;

  char portBanner[MAXBUF];
  uint8_t portBannerPresent;

  char configFile[PATH_MAX];
  char blockedFile[PATH_MAX];
  char historyFile[PATH_MAX];
  char ignoreFile[PATH_MAX];

  int blockTCP;
  int blockUDP;
  int runCmdFirst;
  int resolveHost;
  uint16_t configTriggerCount;
  int disableLocalCheck;

  enum SentryMode sentryMode;
  enum SentryMethod sentryMethod;

  uint8_t logFlags;

  uint8_t daemon;
};

extern struct ConfigData configData;

void ResetConfigData(struct ConfigData *cd);
void PostProcessConfig(struct ConfigData *cd);
void PrintConfigData(const struct ConfigData cd);
char *GetSentryModeString(const enum SentryMode sentryMode);
void SetConfigData(const struct ConfigData *fileConfig, const struct ConfigData *cmdlineConfig);
int AddInterface(struct ConfigData *cd, const char *interface);
size_t GetNoInterfaces(const struct ConfigData *cd);
void FreeConfigData(struct ConfigData *cd);
int IsInterfacePresent(const struct ConfigData *cd, const char *interface);
