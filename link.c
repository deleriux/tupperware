#include "common.h"

#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

struct devlist {
  int ifindex;
  char ifname[32];
  struct devlist *next;
} *devices = NULL;

static int add_device(
    int index,
    char *name)
{
  struct devlist *d;
  for (d=devices; d != NULL; d=d->next) {
    if(d->ifindex == index)
      break;
  }

  if (!d) {
    d = malloc(sizeof(*d));
    memset(d, 0, sizeof(*d));
    assert(d);
    d->ifindex = index;
    strncpy(d->ifname, name, 30);
    d->next = devices;
    devices = d;
  }
  else {
    memset(d->ifname, 0, 31);
    strncpy(d->ifname, name, 30);
  }
  return 1;
}

static int del_device(
    int index)
{
  struct devlist *d, *l;
  if (devices->ifindex == index) {
    d = devices;
    devices = devices->next;
    free(d);
    return 1;
  }

  l = devices;
  for (d=devices->next; d != NULL; d=d->next) {
    if (d->ifindex == index) {
      l->next = d->next;
      free(d);
      return 1;
    }
    l = d;
  }
  return 0;
}

static int append_attr(
    void *packet,
    int tag,
    int len,
    void *data)
{
  struct nlmsghdr *hdr = packet;
  struct rtattr *a = packet + NLMSG_ALIGN(hdr->nlmsg_len);
  void *d = RTA_DATA(a);
  a->rta_type = tag;
  a->rta_len = RTA_LENGTH(len);

  if (len)
    memcpy(d, data, len);

  hdr->nlmsg_len = NLMSG_ALIGN(hdr->nlmsg_len) +
                   a->rta_len;

  return hdr->nlmsg_len;
}

static int parse_error(
    struct nlmsghdr *h)
{
  struct nlmsgerr *err = NLMSG_DATA(h);
  return err->error;
}

static int parse_ifa(
    struct nlmsghdr *h)
{
  int rc = 0;
  struct ifinfomsg *ifa = NLMSG_DATA(h);
  struct rtattr *rta = NLMSG_DATA(h) + sizeof(*ifa);
  size_t rtalen = h->nlmsg_len - sizeof(*h) - sizeof(*ifa);

  for (rta; RTA_OK(rta, rtalen); rta=RTA_NEXT(rta, rtalen)) {
    if (rta->rta_type == IFLA_IFNAME) 
      break;
  }

  if (h->nlmsg_type == RTM_NEWLINK) {
    assert(rta && rta->rta_type == IFLA_IFNAME);
    if (ifa->ifi_flags & IFF_UP)
      rc = add_device(ifa->ifi_index, RTA_DATA(rta));
    else
      rc = del_device(ifa->ifi_index);
  }
  else if (h->nlmsg_type == RTM_DELLINK)
    rc = del_device(ifa->ifi_index);
  return rc;
}


int link_socket(
    void)
{
  int fd = -1;
  struct sockaddr_nl nl;

  fd = socket(AF_NETLINK, SOCK_DGRAM|SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0)
    return -1;

  nl.nl_family = AF_NETLINK;
  nl.nl_pad = 0;
  nl.nl_pid = 0;
  nl.nl_groups = RTMGRP_LINK;
  if (bind(fd, (struct sockaddr *)&nl, sizeof(nl)) < 0)
    return -1;
  return fd; 
}

int link_send(
    int fd)
{
  void *packet = alloca(512);
  struct nlmsghdr *nlhdr = packet;
  struct ifinfomsg *ifa = NLMSG_DATA(nlhdr);

  nlhdr->nlmsg_type = RTM_GETLINK;
  nlhdr->nlmsg_flags = NLM_F_REQUEST|NLM_F_DUMP;
  nlhdr->nlmsg_seq = 0;
  nlhdr->nlmsg_pid = getpid();
  nlhdr->nlmsg_len = NLMSG_LENGTH(sizeof(*ifa));

  ifa->ifi_family = AF_PACKET;
  ifa->ifi_type = 0;
  ifa->ifi_index = 0;
  ifa->ifi_flags = 0;
  ifa->ifi_change = 0xFFFFFFFF;
  
  append_attr(packet, IFLA_UNSPEC, 0, NULL);

  if (sendto(fd, packet, nlhdr->nlmsg_len, 0, NULL, 0) < 0)
    return -1;

  return 0;
}


int link_recv(
    int fd)
{
  int loop = 1;
  int rcvsz;
  void *data = alloca(256*1024);
  struct nlmsghdr *h = data;
  int rc = 0;

  do {
    memset(data, 0, 256*1024);
    rcvsz = recv(fd, data, 256*1024, 0);
    if (rcvsz < 0)
      return -1;

    for (h=data; NLMSG_OK(h, rcvsz); h=NLMSG_NEXT(h, rcvsz)) {
      if ((h->nlmsg_flags & NLM_F_MULTI) == 0)
        loop = 0;
      if (h->nlmsg_type == NLMSG_DONE) {
        loop = 0;
        break;
      }
      else if (h->nlmsg_type == NLMSG_ERROR) {
        rc = parse_error(h);
        if (rc < 0) {
          errno = rc;
          return rc;
        }
      }
      else if (h->nlmsg_type == RTM_NEWLINK ||
               h->nlmsg_type == RTM_DELLINK) {
        rc += parse_ifa(h);
      }
    }
  } while (loop);

  return rc;
}


int link_online(
    char *dev)
{
  struct devlist *d;
  for (d=devices; d != NULL; d=d->next) {
    if (strncmp(d->ifname, dev, 30) == 0)
      return 1;
  }
  return 0;  
}
