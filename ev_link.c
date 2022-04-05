#include "common.h"
#include <ev.h>
#include "ev_link.h"

static void ev_link_recv(
    struct ev_loop *l,
    ev_io *w,
    int revents)
{
  int newstate = 0;
  struct dev *d;

  ev_link *h = w->data;
  if (link_recv(h->fd)) {
    for (d=h->devices; d != NULL; d=d->next) {
      newstate = link_online(d->device);
      if (d->state != newstate) {
        d->state = newstate;
        if (h->state_change_callback) {
          h->state_change_callback(d->device, d->state);
        }
      }
    }
  }
}

int ev_link_init(
    ev_link *h, 
    void (*cb)(char *, int))
{
  memset(h, 0, sizeof(*h));
  h->fd = link_socket();
  if (h->fd < 0)
    return 0;

  ev_io_init(&h->socket, ev_link_recv, h->fd, EV_READ);
  h->socket.data = h;
  h->state_change_callback = cb;

  return 1;
}


void ev_link_start(
    struct ev_loop *l, 
    ev_link *h)
{
  ev_io_start(l, &h->socket);
  link_send(h->fd);
}

void ev_link_stop(
    struct ev_loop *l,
    ev_link *h)
{
  ev_io_stop(l, &h->socket);
}

void ev_link_add_device(
    ev_link *h,
    char *device)
{
  struct dev *d = NULL;
  d = malloc(sizeof(*d));
  assert(d);

  strncpy(d->device, device, 31);
  d->state = link_online(device);
  d->next = h->devices;
  h->devices = d;
}


//void ev_link_destroy(struct ev_loop *l, ev_link *h);

