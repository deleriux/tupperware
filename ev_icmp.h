#ifndef _EV_ICMP_H_
#define _EV_ICMP_H_
#include <ev.h>
#include "icmp.h"

typedef struct icmp_ev_handle {
  struct icmp_socket *ic;
  ev_io socket;
  ev_timer interval;
  ev_timer timeout;
  void *data;
  void (*cb)(void *, int seq, double rtt);
} ev_icmp;

int ev_icmp_init(ev_icmp *h, void (*cb)(void *,int,double), 
                               char *, double i, double t);
void ev_icmp_destroy(struct ev_loop *l, ev_icmp *h);
void ev_icmp_start(struct ev_loop *l, ev_icmp *h);
void ev_icmp_stop(struct ev_loop *l, ev_icmp *h);

#endif
