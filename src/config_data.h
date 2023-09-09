#pragma once

#define LOGFLAG_NONE 0x00
#define LOGFLAG_DEBUG 0x1
#define LOGFLAG_VERBOSE 0x2
#define LOGFLAG_OUTPUT_STDOUT 0x4
#define LOGFLAG_OUTPUT_SYSLOG 0x8

enum SentryMode { SENTRY_MODE_NONE = 0, SENTRY_MODE_TCP, SENTRY_MODE_STCP, SENTRY_MODE_ATCP, SENTRY_MODE_UDP, SENTRY_MODE_SUDP, SENTRY_MODE_AUDP };

struct ConfigData {
  char killRoute[MAXBUF];
  char killHostsDeny[MAXBUF];
  char killRunCmd[MAXBUF];

  uint16_t tcpPorts[MAXSOCKS];
  int tcpPortsLength;
  uint16_t udpPorts[MAXSOCKS];
  int udpPortsLength;

  uint16_t tcpAdvancedPort;
  uint16_t udpAdvancedPort;

  uint16_t tcpAdvancedExcludePorts[UINT16_MAX];
  int tcpAdvancedExcludePortsLength;
  uint16_t udpAdvancedExcludePorts[UINT16_MAX];
  int udpAdvancedExcludePortsLength;

  char portBanner[MAXBUF];

  char configFile[PATH_MAX];
  char blockedFile[PATH_MAX];
  char historyFile[PATH_MAX];
  char ignoreFile[PATH_MAX];

  int blockTCP;
  int blockUDP;
  int runCmdFirst;
  int resolveHost;
  int configTriggerCount;

  enum SentryMode sentryMode;

  uint8_t logFlags;

  uint8_t daemon;
};

extern struct ConfigData configData;

void ResetConfigData(struct ConfigData *cd);
void PostProcessConfig(struct ConfigData *cd);
void PrintConfigData(const struct ConfigData cd);
char *GetSentryModeString(const enum SentryMode sentryMode);
void SetConfigData(const struct ConfigData *fileConfig, const struct ConfigData *cmdlineConfig);
