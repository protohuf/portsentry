/************************************************************************/
/*                                                                      */
/* PortSentry                                                           */
/*                                                                      */
/* This software is Copyright(c) 1997-2003 Craig Rowland                */
/*                                                                      */
/* This software is covered under the Common Public License v1.0        */
/* See the enclosed LICENSE file for more information.                  */
/*                                                                      */
/* Created: 10-12-1997                                                  */
/* Modified: 05-23-2003                                                 */
/*                                                                      */
/* Send all changes/modifications/bugfixes to:                          */
/* craigrowland at users dot sourceforge dot net                        */
/*                                                                      */
/* $Id: portsentry_io.c,v 1.36 2003/05/23 17:41:40 crowland Exp crowland $ */
/************************************************************************/
#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "config.h"
#include "config_data.h"
#include "io.h"
#include "portsentry.h"
#include "util.h"

static uint8_t isSyslogOpen = FALSE;
enum LogType { LogTypeNone,
               LogTypeError,
               LogTypeDebug,
               LogTypeVerbose };

static void LogEntry(enum LogType logType, char *logentry, va_list argsPtr);

static void LogEntry(enum LogType logType, char *logentry, va_list argsPtr) {
  char logbuffer[MAXBUF];

  vsnprintf(logbuffer, MAXBUF, logentry, argsPtr);

  if (configData.logFlags & LOGFLAG_OUTPUT_STDOUT) {
    if (logType == LogTypeError) {
      fprintf(stderr, "%s\n", logbuffer);
      fflush(stderr);
    } else {
      printf("%s%s\n", (logType == LogTypeDebug) ? "debug: " : "", logbuffer);
      fflush(stdout);
    }
  }

  if (configData.logFlags & LOGFLAG_OUTPUT_SYSLOG) {
    if (isSyslogOpen == FALSE) {
      openlog("portsentry", LOG_PID, LOG_DAEMON);
      isSyslogOpen = TRUE;
    }
    syslog((logType == LogTypeNone) ? LOG_NOTICE : (logType == LogTypeError) ? LOG_ERR
                                               : (logType == LogTypeDebug)   ? LOG_DEBUG
                                                                             : LOG_INFO,
           "%s%s", (logType == LogTypeDebug) ? "debug: " : "", logbuffer);
  }
}

void Log(char *logentry, ...) {
  va_list argsPtr;
  va_start(argsPtr, logentry);
  LogEntry(LogTypeNone, logentry, argsPtr);
  va_end(argsPtr);
}

void Error(char *logentry, ...) {
  va_list argsPtr;
  va_start(argsPtr, logentry);
  LogEntry(LogTypeError, logentry, argsPtr);
  va_end(argsPtr);
}

void Debug(char *logentry, ...) {
  va_list argsPtr;

  if ((configData.logFlags & LOGFLAG_DEBUG) == 0) {
    return;
  }

  va_start(argsPtr, logentry);
  LogEntry(LogTypeDebug, logentry, argsPtr);
  va_end(argsPtr);
}

void Verbose(char *logentry, ...) {
  va_list argsPtr;

  if ((configData.logFlags & LOGFLAG_VERBOSE) == 0) {
    return;
  }

  va_start(argsPtr, logentry);
  LogEntry(LogTypeVerbose, logentry, argsPtr);
  va_end(argsPtr);
}

void Exit(int status) {
  Log("PortSentry is shutting down");

  if (isSyslogOpen == TRUE) {
    closelog();
    isSyslogOpen = FALSE;
  }

  exit(status);
}

/* The daemonizing code copied from Advanced Programming */
/* in the UNIX Environment by W. Richard Stevens with minor changes */
int DaemonSeed(void) {
  int childpid;

  signal(SIGALRM, SIG_IGN);
  signal(SIGHUP, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, Exit);
  signal(SIGABRT, Exit);
  signal(SIGURG, Exit);
  signal(SIGKILL, Exit);

  if ((childpid = fork()) < 0)
    return (ERROR);
  else if (childpid > 0)
    exit(0);

  setsid();
  /* FIXME: This should perhaps he a fatal error, also maybe do better daemonizing */
  if (chdir("/") == -1) {
    Log("adminalert: Unable to change to root directory during daemonizing (ignoring)");
  }
  umask(077);

  /* close stdout, stdin, stderr */
  close(0);
  close(1);
  close(2);

  return (TRUE);
}

/* Compares an IP address against a listed address and its netmask*/
int CompareIPs(char *target, char *ignoreAddr, int ignoreNetmaskBits) {
  unsigned long int ipAddr, targetAddr;
  uint32_t netmaskAddr;

  ipAddr = inet_addr(ignoreAddr);
  targetAddr = inet_addr(target);
  netmaskAddr = htonl(0xFFFFFFFF << (32 - ignoreNetmaskBits));

  Debug("target %s", target);
  Debug("ignoreAddr %s", ignoreAddr);
  Debug("ignoreNetmaskBits %d", ignoreNetmaskBits);
  Debug("ipAddr %lu", ipAddr);
  Debug("targetAddr %lu", targetAddr);
  Debug("netmask %x", netmaskAddr);
  Debug("mix ipAddr %lu", (ipAddr & netmaskAddr));
  Debug("mix target %lu", (targetAddr & netmaskAddr));

  /* Network portion mask & op and return */
  if ((ipAddr & netmaskAddr) == (targetAddr & netmaskAddr))
    return (TRUE);
  else
    return (FALSE);
}

/* check hosts that should never be blocked */
int NeverBlock(char *target, char *filename) {
  FILE *input;
  char buffer[MAXBUF], tempBuffer[MAXBUF], netmaskBuffer[MAXBUF];
  char *slashPos;
  int dest = 0, netmaskBits = 0;
  size_t count = 0;

  Debug("NeverBlock: Opening ignore file: %s ", filename);

  if ((input = fopen(filename, "r")) == NULL)
    return (ERROR);

  Debug("NeverBlock: Doing lookup for host: %s ", target);

  while (fgets(buffer, MAXBUF, input) != NULL) {
    /* Reset destination counter */
    dest = 0;

    if ((buffer[0] == '#') || (buffer[0] == '\n'))
      continue;

    for (count = 0; count < strlen(buffer); count++) {
      /* Parse out digits, colons, and slashes. Everything else rejected */
      if ((isdigit(buffer[count])) || (buffer[count] == '.') ||
          (buffer[count] == ':') || (buffer[count] == '/')) {
        tempBuffer[dest++] = buffer[count];
      } else {
        tempBuffer[dest] = '\0';
        break;
      }
    }

    /* Return pointer to slash if it exists and copy data to buffer */
    slashPos = strchr(tempBuffer, '/');
    if (slashPos) {
      SafeStrncpy(netmaskBuffer, slashPos + 1, MAXBUF);
      /* Terminate tempBuffer string at delimeter for later use */
      *slashPos = '\0';
    } else { /* Copy in a 32 bit netmask if none given */
      SafeStrncpy(netmaskBuffer, "32", MAXBUF);
    }

    /* Convert netmaskBuffer to bits in netmask */
    netmaskBits = atoi(netmaskBuffer);
    if ((netmaskBits < 0) || (netmaskBits > 32)) {
      Log("adminalert: Invalid netmask in config file: %s  Ignoring entry.", buffer);
      continue;
    }

    if (CompareIPs(target, tempBuffer, netmaskBits)) {
      Debug("NeverBlock: Host: %s found in ignore file with netmask %s", target, netmaskBuffer);

      fclose(input);
      return (TRUE);
    }
  } /* end while() */

  Debug("NeverBlock: Host: %s NOT found in ignore file", target);

  fclose(input);
  return (FALSE);
}

static int WriteToLogFile(const char *filename, const char *target, const char *resolvedHost, const int port, const char *portType) {
  FILE *output;
  char err[ERRNOMAXBUF];
  struct tm tm, *tmptr;
  time_t current_time;
  current_time = time(0);
  tmptr = localtime_r(&current_time, &tm);

  Debug("WriteToLogFile: Opening: %s ", filename);

  if ((output = fopen(filename, "a")) == NULL) {
    Log("adminalert: Unable to open block log file: %s (%s)", filename, ErrnoString(err, sizeof(err)));
    return FALSE;
  }

#ifdef OpenBSD
  fprintf(output, "%lld - %02d/%02d/%04d %02d:%02d:%02d Host: %s/%s Port: %d %s Blocked\n",
          current_time, tmptr->tm_mon + 1, tmptr->tm_mday, tmptr->tm_year + 1900,
          tmptr->tm_hour, tmptr->tm_min, tmptr->tm_sec, resolvedHost, target, port, portType);
#else
  fprintf(output, "%ld - %02d/%02d/%04d %02d:%02d:%02d Host: %s/%s Port: %d %s Blocked\n",
          current_time, tmptr->tm_mon + 1, tmptr->tm_mday, tmptr->tm_year + 1900,
          tmptr->tm_hour, tmptr->tm_min, tmptr->tm_sec, resolvedHost, target, port, portType);
#endif

  fclose(output);

  return TRUE;
}

int WriteBlocked(char *target, char *resolvedHost, int port, char *blockedFilename, char *historyFilename, const char *portType) {
  int blockedStatus = TRUE, historyStatus = TRUE;

  blockedStatus = WriteToLogFile(blockedFilename, target, resolvedHost, port, portType);
  historyStatus = WriteToLogFile(historyFilename, target, resolvedHost, port, portType);

  return (blockedStatus && historyStatus);
}

int BindSocket(int sockfd, int port, int proto) {
  char err[ERRNOMAXBUF];
  struct sockaddr_in server;

  Debug("BindSocket: Binding to port: %d", port);

  bzero((char *)&server, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(port);

  if (bind(sockfd, (struct sockaddr *)&server, sizeof(server)) == -1) {
    Debug("BindSocket: Binding failed: %s", ErrnoString(err, sizeof(err)));
    return (ERROR);
  }

  if (proto == IPPROTO_TCP) {
    if (listen(sockfd, 5) == -1) {
      Debug("BindSocket: Listen failed: %s", ErrnoString(err, sizeof(err)));
      return (ERROR);
    }
  }

  return (TRUE);
}

/* Open a TCP Socket */
int OpenTCPSocket(void) {
  int sockfd;
  const int enable = 1;

  Debug("OpenTCPSocket: opening TCP socket");

  if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    return (ERROR);

  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
    return ERROR;

  return (sockfd);
}

/* Open a UDP Socket */
int OpenUDPSocket(void) {
  int sockfd;

  Debug("openUDPSocket opening UDP socket");

  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    return (ERROR);
  else
    return (sockfd);
}

int OpenRAWTCPSocket(void) {
  int sockfd;

  Debug("OpenRAWTCPSocket: opening RAW TCP socket");

  if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP)) < 0)
    return (ERROR);
  else
    return (sockfd);
}

int OpenRAWUDPSocket(void) {
  int sockfd;

  Debug("OpenRAWUDPSocket: opening RAW UDP socket");

  if ((sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP)) < 0)
    return (ERROR);
  else
    return (sockfd);
}

/* This will use a system() call to change the route of the target host to */
/* a dead IP address on your LOCAL SUBNET. */
int KillRoute(char *target, int port, char *killString, char *detectionType) {
  char cleanAddr[MAXBUF], commandStringTemp[MAXBUF];
  char commandStringTemp2[MAXBUF], commandStringFinal[MAXBUF];
  char portString[MAXBUF];
  int killStatus = ERROR, substStatus = ERROR;

  if (strlen(killString) == 0)
    return (TRUE);

  CleanIpAddr(cleanAddr, target);
  snprintf(portString, MAXBUF, "%d", port);

  substStatus =
      SubstString(cleanAddr, "$TARGET$", killString, commandStringTemp);
  if (substStatus == 0) {
    Log("adminalert: No target variable specified in KILL_ROUTE option. Skipping.");
    return (ERROR);
  } else if (substStatus == ERROR) {
    Log("adminalert: Error trying to parse $TARGET$ Token for KILL_ROUTE. Skipping.");
    return (ERROR);
  }

  if (SubstString(portString, "$PORT$", commandStringTemp,
                  commandStringTemp2) == ERROR) {
    Log("adminalert: Error trying to parse $PORT$ Token for KILL_ROUTE. Skipping.");
    return (ERROR);
  }

  if (SubstString(detectionType, "$MODE$", commandStringTemp2,
                  commandStringFinal) == ERROR) {
    Log("adminalert: Error trying to parse $MODE$ Token for KILL_ROUTE. Skipping.");
    return (ERROR);
  }

  Debug("KillRoute: running route command: %s", commandStringFinal);

  /* Kill the bastard and report a status */
  killStatus = system(commandStringFinal);

  if (killStatus == 127) {
    Error("adminalert: There was an error trying to block host (exec fail) %s", target);
    return (ERROR);
  } else if (killStatus < 0) {
    Error("adminalert: There was an error trying to block host (system fail) %s", target);
    return (ERROR);
  } else {
    Log("attackalert: Host %s has been blocked via dropped route using command: \"%s\"", target, commandStringFinal);
    return (TRUE);
  }
}

/* This will run a specified command with TARGET as the option if one is given.
 */
int KillRunCmd(char *target, int port, char *killString, char *detectionType) {
  char cleanAddr[MAXBUF], commandStringTemp[MAXBUF];
  char commandStringTemp2[MAXBUF], commandStringFinal[MAXBUF];
  char portString[MAXBUF];
  int killStatus = ERROR;

  if (strlen(killString) == 0)
    return (TRUE);

  CleanIpAddr(cleanAddr, target);
  snprintf(portString, MAXBUF, "%d", port);

  /* Tokens are not required, but we check for an error anyway */
  if (SubstString(cleanAddr, "$TARGET$", killString, commandStringTemp) == ERROR) {
    Log("adminalert: Error trying to parse $TARGET$ Token for KILL_RUN_CMD. Skipping.");
    return (ERROR);
  }

  if (SubstString(portString, "$PORT$", commandStringTemp, commandStringTemp2) == ERROR) {
    Log("adminalert: Error trying to parse $PORT$ Token for KILL_RUN_CMD. Skipping.");
    return (ERROR);
  }

  if (SubstString(detectionType, "$MODE$", commandStringTemp2, commandStringFinal) == ERROR) {
    Log("adminalert: Error trying to parse $MODE$ Token for KILL_RUN_CMD. Skipping.");
    return (ERROR);
  }

  /* Kill the bastard and report a status */
  killStatus = system(commandStringFinal);

  if (killStatus == 127) {
    Error("adminalert: There was an error trying to run command (exec fail) %s", target);
    return (ERROR);
  } else if (killStatus < 0) {
    Error("adminalert: There was an error trying to run command (system fail) %s", target);
    return (ERROR);
  } else {
    /* report success */
    Log("attackalert: External command run for host: %s using command: \"%s\"", target, commandStringFinal);
    return (TRUE);
  }
}

/* this function will drop the host into the TCP wrappers hosts.deny file to deny
 * all access. The drop route metod is preferred as this stops UDP attacks as well
 * as TCP. You may find though that host.deny will be a more permanent home.. */
int KillHostsDeny(char *target, int port, char *killString, char *detectionType) {
  FILE *output;
  char cleanAddr[MAXBUF], commandStringTemp[MAXBUF];
  char commandStringTemp2[MAXBUF], commandStringFinal[MAXBUF];
  char portString[MAXBUF];
  int substStatus = ERROR;

  if (strlen(killString) == 0)
    return (TRUE);

  CleanIpAddr(cleanAddr, target);

  snprintf(portString, MAXBUF, "%d", port);

  Debug("KillHostsDeny: parsing string for block: %s", killString);

  substStatus =
      SubstString(cleanAddr, "$TARGET$", killString, commandStringTemp);
  if (substStatus == 0) {
    Log("adminalert: No target variable specified in KILL_HOSTS_DENY option. Skipping.");
    return (ERROR);
  } else if (substStatus == ERROR) {
    Log("adminalert: Error trying to parse $TARGET$ Token for KILL_HOSTS_DENY. Skipping.");
    return (ERROR);
  }

  if (SubstString(portString, "$PORT$", commandStringTemp, commandStringTemp2) == ERROR) {
    Log("adminalert: Error trying to parse $PORT$ Token for KILL_HOSTS_DENY. Skipping.");
    return (ERROR);
  }

  if (SubstString(detectionType, "$MODE$", commandStringTemp2, commandStringFinal) == ERROR) {
    Log("adminalert: Error trying to parse $MODE$ Token for KILL_HOSTS_DENY. Skipping.");
    return (ERROR);
  }

  Debug("KillHostsDeny: result string for block: %s", commandStringFinal);

  if ((output = fopen(WRAPPER_HOSTS_DENY, "a")) == NULL) {
    Log("adminalert: cannot open hosts.deny file: %s for blocking.", WRAPPER_HOSTS_DENY);
    Error("securityalert: There was an error trying to block host %s", target);
    return (FALSE);
  } else {
    fprintf(output, "%s\n", commandStringFinal);
    fclose(output);
    Log("attackalert: Host %s has been blocked via wrappers with string: \"%s\"", target, commandStringFinal);
    return (TRUE);
  }
}

/* check if the host is already blocked */
int IsBlocked(char *target, char *filename) {
  FILE *input;
  char buffer[MAXBUF], tempBuffer[MAXBUF], err[ERRNOMAXBUF];
  char *ipOffset;
  size_t count;

  Debug("IsBlocked: Opening block file: %s ", filename);

  if ((input = fopen(filename, "r")) == NULL) {
    Error("adminalert: Cannot open blocked file: %s for reading: %s. Will create.", filename, ErrnoString(err, sizeof(err)));
    return (FALSE);
  }

  while (fgets(buffer, MAXBUF, input) != NULL) {
    if ((ipOffset = strstr(buffer, target)) != NULL) {
      for (count = 0; count < strlen(ipOffset); count++) {
        if ((isdigit(ipOffset[count])) || (ipOffset[count] == '.')) {
          tempBuffer[count] = ipOffset[count];
        } else {
          tempBuffer[count] = '\0';
          break;
        }
      }
      if (strcmp(target, tempBuffer) == 0) {
        Debug("isBlocked: Host: %s found in blocked file", target);
        fclose(input);
        return (TRUE);
      }
    }
  }
  Debug("IsBlocked: Host: %s NOT found in blocked file", target);
  fclose(input);
  return (FALSE);
}

/*********************************************************************************
 * String substitute function
 *
 * This function takes:
 *
 * 1) A token to use for replacement.
 * 2) A token to find.
 * 3) A string with the tokens in it.
 * 4) A string to write the replaced result.
 *
 * It returns the number of substitutions made during the operation.
 **********************************************************************************/
int SubstString(const char *replace, const char *find, const char *target, char *result) {
  int count = 0, findCount = 0, findLen = 0, numberOfSubst = 0;
  char tempString[MAXBUF], *tempStringPtr;
  size_t replaceCount = 0;

  Debug("SubstString: Processing string: %s %lu", target, strlen(target));
  Debug("SubstString: Processing search text: %s %lu", replace, strlen(replace));
  Debug("SubstString: Processing replace text: %s %lu", find, strlen(find));

  /* string not found in target */
  if (strstr(target, find) == NULL) {
    strncpy(result, target, MAXBUF);
    Debug("SubstString: Result string: %s", result);
    return (numberOfSubst);
  } else if ((strlen(target)) + (strlen(replace)) + (strlen(find)) > MAXBUF) { /* String/victim/target too long */
    return (ERROR);
  }

  memset(tempString, '\0', MAXBUF);
  memset(result, '\0', MAXBUF);
  findLen = strlen(find);
  tempStringPtr = tempString;

  for (count = 0; count < MAXBUF; count++) {
    if (*target == '\0') {
      break;
    } else if ((strncmp(target, find, findLen)) != 0) {
      *tempStringPtr++ = *target++;
    } else {
      numberOfSubst++;
      for (replaceCount = 0; replaceCount < strlen(replace); replaceCount++)
        *tempStringPtr++ = replace[replaceCount];
      for (findCount = 0; findCount < findLen; findCount++)
        target++;
    }
  }

  strncpy(result, tempString, MAXBUF);
  Debug("SubstString: Result string: %s", result);
  return (numberOfSubst);
}

int testFileAccess(char *filename, char *mode) {
  FILE *testFile;

  if ((testFile = fopen(filename, mode)) == NULL) {
    return (FALSE);
  } else {
    fclose(testFile);
    return (TRUE);
  }
}

void XmitBannerIfConfigured(const int proto, const int socket, const struct sockaddr_in *client) {
  ssize_t result = 0;
  char err[ERRNOMAXBUF];

  assert(proto == IPPROTO_TCP || proto == IPPROTO_UDP);

  if (configData.portBannerPresent == FALSE)
    return;

  errno = 0;

  if (proto == IPPROTO_TCP) {
    result = write(socket, configData.portBanner, strlen(configData.portBanner));
  } else if (proto == IPPROTO_UDP) {
    if (client == NULL) {
      Error("adminalert: No client address specified for UDP banner transmission (ignoring)");
      return;
    }
    result = sendto(socket, configData.portBanner, strlen(configData.portBanner), 0, (struct sockaddr *)client, sizeof(struct sockaddr_in));
  }

  if (result == -1) {
    Error("adminalert: Could not write banner to socket (ignoring): %s", ErrnoString(err, sizeof(err)));
  }
}

/* Read packet IP and transport headers and set ipPtr/transportPtr to their correct location
 * transportPtr is either a struct tcphdr * or struct udphdr *
 */
#ifdef BSD
int PacketRead(int socket, char *packetBuffer, size_t packetBufferSize, struct ip **ipPtr, void **transportPtr) {
#else
int PacketRead(int socket, char *packetBuffer, size_t packetBufferSize, struct iphdr **ipPtr, void **transportPtr) {
#endif
  char err[ERRNOMAXBUF];
  size_t ipHeaderLength;
  ssize_t result;
  struct in_addr addr;

  if ((result = read(socket, packetBuffer, packetBufferSize)) == -1) {
    Error("adminalert: Could not read from socket %d: %s. Aborting", socket, ErrnoString(err, sizeof(err)));
    return ERROR;
#ifdef BSD
  } else if (result < (ssize_t)sizeof(struct ip)) {
#else
  } else if (result < (ssize_t)sizeof(struct iphdr)) {
#endif
    Error("adminalert: Packet read from socket %d is too small (%lu bytes). Aborting", socket, result);
    return ERROR;
  }

#ifdef BSD
  *ipPtr = (struct ip *)packetBuffer;
#else
  *ipPtr = (struct iphdr *)packetBuffer;
#endif

#ifdef BSD
  if (((*ipPtr)->ip_hl < 5) || ((*ipPtr)->ip_hl > 15)) {
    addr.s_addr = (u_int)(*ipPtr)->ip_src.s_addr;
    Log("attackalert: Illegal IP header length detected in TCP packet: %d from (possible) host: %s", (*ipPtr)->ip_hl, inet_ntoa(addr));
#else
  if (((*ipPtr)->ihl < 5) || ((*ipPtr)->ihl > 15)) {
    addr.s_addr = (u_int)(*ipPtr)->saddr;
    Log("attackalert: Illegal IP header length detected in TCP packet: %d from (possible) host: %s", (*ipPtr)->ihl, inet_ntoa(addr));
#endif
    return (FALSE);
  }

#ifdef BSD
  ipHeaderLength = (*ipPtr)->ip_hl * 4;
#else
  ipHeaderLength = (*ipPtr)->ihl * 4;
#endif

  if (ipHeaderLength > packetBufferSize) {
    Error("adminalert: IP header length (%lu) is larger than packet buffer size (%lu). Aborting", ipHeaderLength, packetBufferSize);
    return FALSE;
  }

  *transportPtr = (void *)(packetBuffer + ipHeaderLength);
  return TRUE;
}
