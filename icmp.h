#ifndef _ICMP_H_
#define _ICMP_H_
#include "common.h"

struct icmp_socket {
  char *addr;
  int fd;
  uint16_t seqno;
  double timeout;
  double interval;

  struct rlist {
    uint16_t sequence;
    double sent_time;
    double recv_time;
    struct rlist *next;
  } *results;
  size_t results_len;

};

struct icmp_socket * icmp_socket_create(const char *, double, double);
int icmp_socket_recreate(struct icmp_socket *);
int icmp_socket_recv(struct icmp_socket *, double *timestamp);
int icmp_socket_send(struct icmp_socket *, double timestamp);
int icmp_socket_timeout(struct icmp_socket *, double now);

void icmp_socket_destroy(struct icmp_socket *);

#endif
