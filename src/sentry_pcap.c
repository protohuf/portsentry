#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <net/if_arp.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include "portsentry.h"
#include "sentry_pcap.h"
#include "listener.h"
#include "device.h"
#include "io.h"
#include "util.h"
#include "config_data.h"

#define POLL_TIMEOUT 500

static void HandlePacket(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);

#ifdef BSD
static int PrepPacket(const struct Device *device, const struct pcap_pkthdr *header, const u_char *packet, struct ip **ip, struct tcphdr **tcp, struct udphdr **udp);
static int SetSockaddrByPacket(struct sockaddr_in *client, const struct ip *ip, const struct tcphdr *tcp, const struct udphdr *udp);
static int SetPcapConnectionData(struct ConnectionData *cd, const struct ip *ip, const struct tcphdr *tcp, const struct udphdr *udp);
static void PrintPacket(const struct Device *device, const struct ip *ip, const struct tcphdr *tcp, const struct udphdr *udp, const struct pcap_pkthdr *header);
#else
static int PrepPacket(const struct Device *device, const struct pcap_pkthdr *header, const u_char *packet, struct iphdr **ip, struct tcphdr **tcp, struct udphdr **udp);
static int SetSockaddrByPacket(struct sockaddr_in *client, const struct iphdr *ip, const struct tcphdr *tcp, const struct udphdr *udp);
static int SetPcapConnectionData(struct ConnectionData *cd, const struct iphdr *ip, const struct tcphdr *tcp, const struct udphdr *udp);
static void PrintPacket(const struct Device *device, const struct iphdr *ip, const struct tcphdr *tcp, const struct udphdr *udp, const struct pcap_pkthdr *header);
#endif

int PortSentryPcap(void) {
  int status = FALSE, ret, nfds = 0, i;
  char err[ERRNOMAXBUF];
  struct ListenerModule *lm = NULL;
  struct pollfd *fds = NULL;
  struct Device *current = NULL;

  if ((lm = AllocListenerModule()) == NULL) {
    goto exit;
  }

  if (InitListenerModule(lm) == FALSE) {
    goto exit;
  }

  if ((fds = SetupPollFds(lm, &nfds)) == NULL) {
    Error("Unable to allocate memory for pollfd");
    goto exit;
  }

  Log("adminalert: PortSentry is now active and listening.");

  while (1) {
    ret = poll(fds, nfds, POLL_TIMEOUT);

    if (ret == -1) {
      Error("poll() failed %s", ErrnoString(err, sizeof(err)));
      goto exit;
    } else if (ret == 0) {
      continue;
    }

    for (i = 0; i < nfds; i++) {
      if (fds[i].revents & POLLIN) {
        if ((current = GetDeviceByFd(lm, fds[i].fd)) == NULL) {
          Error("Unable to find device by fd %d", fds[i].fd);
          goto exit;
        }

        do {
          ret = pcap_dispatch(current->handle, -1, HandlePacket, (u_char *)current);

          if (ret == PCAP_ERROR) {
            Error("pcap_dispatch() failed %s, ignoring", pcap_geterr(current->handle));
          } else if (ret == PCAP_ERROR_BREAK) {
            Error("Got PCAP_ERROR_BREAK, ignoring");
          }
        } while (ret > 0);
      }
    }
  }

  status = TRUE;

exit:
  if (fds)
    free(fds);
  if (lm)
    FreeListenerModule(lm);
  return status;
}

static void HandlePacket(u_char *args, const struct pcap_pkthdr *header, const u_char *packet) {
  struct ConnectionData cd;
  struct sockaddr_in client;
  struct Device *device = (struct Device *)args;
#ifdef BSD
  struct ip *ip;
#else
  struct iphdr *ip;
#endif
  struct tcphdr *tcp;
  struct udphdr *udp;

  if (PrepPacket(device, header, packet, &ip, &tcp, &udp) == FALSE) {
    return;
  }

  if (SetSockaddrByPacket(&client, ip, tcp, udp) == FALSE) {
    return;
  }

  if (SetPcapConnectionData(&cd, ip, tcp, udp) == FALSE) {
    return;
  }

  if (cd.protocol == IPPROTO_TCP && (((tcp->th_flags & TH_ACK) != 0) || ((tcp->th_flags & TH_RST) != 0))) {
    Debug("Got TCP packet with ACK=%d RST=%d, ignoring, offending packet was:", (tcp->th_flags & TH_ACK) != 0 ? 1 : 0, (tcp->th_flags & TH_RST) != 0 ? 1 : 0);
    if (configData.logFlags & LOGFLAG_DEBUG) {
      PrintPacket(device, ip, tcp, udp, header);
    }
    return;
  }

  // FIXME: In pcap we need to consider the interface
  if (IsPortInUse(cd.port, cd.protocol) != FALSE) {
    return;
  }

  RunSentry(&cd, &client, ip, tcp, NULL);
}

#ifdef BSD
static int PrepPacket(const struct Device *device, const struct pcap_pkthdr *header, const u_char *packet, struct ip **ip, struct tcphdr **tcp, struct udphdr **udp) {
#else
static int PrepPacket(const struct Device *device, const struct pcap_pkthdr *header, const u_char *packet, struct iphdr **ip, struct tcphdr **tcp, struct udphdr **udp) {
#endif
  int iplen, len_to_proto;
  uint8_t protocol;
  *ip = NULL;
  *tcp = NULL;
  *udp = NULL;
  (void)header;

  // FIXME: Clean me up
  if (pcap_datalink(device->handle) == DLT_EN10MB) {
#ifdef BSD
    *ip = (struct ip *)(packet + sizeof(struct ether_header));
#else
    *ip = (struct iphdr *)(packet + sizeof(struct ether_header));
#endif
  } else if (pcap_datalink(device->handle) == DLT_RAW) {
#ifdef BSD
    *ip = (struct ip *)(packet);
#else
    *ip = (struct iphdr *)(packet);
#endif
  } else if (
      pcap_datalink(device->handle) == DLT_NULL
#ifdef __OpenBSD__
      || pcap_datalink(device->handle) == DLT_LOOP
#endif
  ) {
    uint32_t nulltype = *packet;
    if (pcap_datalink(device->handle) == DLT_NULL) {
      if (nulltype != 2) {
        Error("adminalert: Packet on %s have unsupported nulltype set (nulltype: %d) on a DLT_NULL dev", device->name, nulltype);
        return FALSE;
      }
#ifdef __OpenBSD__
    } else if (pcap_datalink(device->handle) == DLT_LOOP) {
      /*
       * FIXME: On OpenBSD 7.4 the nulltype is 0 on the loopback interface receiving IPv4 packets.
       * According to libpcap documentation it's supposed to be a network byte-order AF_ value.
       * If this holds true for OpenBSD's then packets are for some reason classified as AF_UNSPEC.
       * Confirm this
       */
      if (nulltype != 0) {
        Error("adminalert: Packet on %s have unsupported nulltype set (nulltype: %d) on a DLT_LOOP dev", device->name, nulltype);
        return FALSE;
      }
#endif
    }
#ifdef BSD
    *ip = (struct ip *)(packet + 4);
#else
    *ip = (struct iphdr *)(packet + 4);
#endif
  } else {
    Error("adminalert: Packet on %s have unsupported datalink type set (datalink: %d)", device->name, pcap_datalink(device->handle));
    return FALSE;
  }

#ifdef BSD
  iplen = (*ip)->ip_hl * 4;
  protocol = (*ip)->ip_p;
#else
  iplen = (*ip)->ihl * 4;
  protocol = (*ip)->protocol;
#endif

  if (pcap_datalink(device->handle) == DLT_EN10MB) {
    len_to_proto = sizeof(struct ether_header) + iplen;
  } else if (pcap_datalink(device->handle) == DLT_RAW) {
    len_to_proto = iplen;
  } else if (
      pcap_datalink(device->handle) == DLT_NULL
#ifdef __OpenBSD__
      || pcap_datalink(device->handle) == DLT_LOOP
#endif
  ) {
    len_to_proto = 4 + iplen;
  } else {
    Error("adminalert: Packet on %s have unsupported datalink type set (datalink: %d)", device->name, pcap_datalink(device->handle));
    return FALSE;
  }

  if (protocol == IPPROTO_TCP) {
    *tcp = (struct tcphdr *)(packet + len_to_proto);
  } else if (protocol == IPPROTO_UDP) {
    *udp = (struct udphdr *)(packet + len_to_proto);
  } else {
    Error("adminalert: Packet on %s have unknown protocol %d. Showing Packet:", device->name, protocol);
    PrintPacket(device, *ip, *tcp, *udp, header);
    return FALSE;
  }
  return TRUE;
}

#ifdef BSD
static int SetSockaddrByPacket(struct sockaddr_in *client, const struct ip *ip, const struct tcphdr *tcp, const struct udphdr *udp) {
#else
static int SetSockaddrByPacket(struct sockaddr_in *client, const struct iphdr *ip, const struct tcphdr *tcp, const struct udphdr *udp) {
#endif
  uint8_t protocol;

  memset(client, 0, sizeof(struct sockaddr_in));
#ifdef BSD
  protocol = ip->ip_p;
  client->sin_addr.s_addr = ip->ip_src.s_addr;
#else
  protocol = ip->protocol;
  client->sin_addr.s_addr = ip->saddr;
#endif

  client->sin_family = AF_INET;
  if (protocol == IPPROTO_TCP) {
    client->sin_port = tcp->th_dport;
  } else if (protocol == IPPROTO_UDP) {
    client->sin_port = udp->uh_dport;
  } else {
    Error("adminalert: Unknown protocol %d detected during sockaddr resolution. Attempting to continue.", protocol);
    return FALSE;
  }

  return TRUE;
}

#ifdef BSD
static int SetPcapConnectionData(struct ConnectionData *cd, const struct ip *ip, const struct tcphdr *tcp, const struct udphdr *udp) {
  cd->protocol = ip->ip_p;
#else
static int SetPcapConnectionData(struct ConnectionData *cd, const struct iphdr *ip, const struct tcphdr *tcp, const struct udphdr *udp) {
  cd->protocol = ip->protocol;
#endif
  cd->sockfd = -1;
  cd->portInUse = FALSE;

  if (cd->protocol == IPPROTO_TCP) {
    cd->port = ntohs(tcp->th_dport);
  } else if (cd->protocol == IPPROTO_UDP) {
    cd->port = ntohs(udp->uh_dport);
  } else {
    Error("adminalert: Unknown protocol %d detected while setting connection data", cd->protocol);
    return FALSE;
  }
  return TRUE;
}

#ifdef BSD
static void PrintPacket(const struct Device *device, const struct ip *ip, const struct tcphdr *tcp, const struct udphdr *udp, const struct pcap_pkthdr *header) {
#else
static void PrintPacket(const struct Device *device, const struct iphdr *ip, const struct tcphdr *tcp, const struct udphdr *udp, const struct pcap_pkthdr *header) {
#endif
  int iplen;
  uint8_t protocol, ipVersion, hl;
  char saddr[16], daddr[16];

#ifdef BSD
  ntohstr(saddr, sizeof(saddr), ip->ip_src.s_addr);
  ntohstr(daddr, sizeof(daddr), ip->ip_dst.s_addr);
  iplen = ip->ip_hl * 4;
  protocol = ip->ip_p;
  ipVersion = ip->ip_v;
  hl = ip->ip_hl;
#else
  ntohstr(saddr, sizeof(saddr), ip->saddr);
  ntohstr(daddr, sizeof(daddr), ip->daddr);
  iplen = ip->ihl * 4;
  protocol = ip->protocol;
  ipVersion = ip->version;
  hl = ip->ihl;
#endif

  fprintf(stderr, "%s: %d [%d] ", device->name, header->caplen, header->len);
  fprintf(stderr, "ihl: %d IP len: %d proto: %s (%d) ver: %d saddr: %s daddr: %s ", hl, iplen,
          protocol == IPPROTO_TCP   ? "tcp"
          : protocol == IPPROTO_UDP ? "udp"
                                    : "other",
          protocol,
          ipVersion, saddr, daddr);

  if (protocol == IPPROTO_TCP) {
    fprintf(stderr, "sport: %d dport: %d", ntohs(tcp->th_sport), ntohs(tcp->th_dport));
  } else if (protocol == IPPROTO_UDP) {
    fprintf(stderr, "sport: %d dport: %d", ntohs(udp->uh_sport), ntohs(udp->uh_dport));
  }
  fprintf(stderr, "\n");
}
