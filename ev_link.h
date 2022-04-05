#ifndef _EV_LINK_H_
#define _EV_LINK_H_
#include <ev.h>
#include "link.h"

typedef struct link_ev_handle {
  int fd;
  ev_io socket;
  void (*state_change_callback)(char *dev, int state);
  struct dev {
    char device[32];
    int state;
    struct dev *next;
  } *devices;
} ev_link;

int ev_link_init(ev_link *h, void (*cb)(char *, int));
void ev_link_destroy(struct ev_loop *l, ev_link *h);
void ev_link_start(struct ev_loop *l, ev_link *h);
void ev_link_stop(struct ev_loop *l, ev_link *h);
void ev_link_add_device(ev_link *h, char *dev);

#endif
