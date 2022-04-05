#include "common.h"
#include <ev.h>
#include "ev_icmp.h"

static void icmp_receive_cb(
    struct ev_loop *loop,
    ev_io *w,
    int revents)
{
  struct icmp_ev_handle *lh = w->data;
  struct icmp_socket *ic = lh->ic;
  ev_tstamp now = ev_now(loop);
  ev_tstamp then;
  int seqno;

  seqno = icmp_socket_recv(ic, &then);
  if (seqno < 0 && lh->cb)
    lh->cb(lh->data, seqno, -1.0);

  if (seqno && lh->cb)
    lh->cb(lh->data, seqno, now-then);

  if (ic->timeout) {
    if (ic->results_len == 0) 
      ev_timer_stop(loop, &lh->timeout);
    else {
      assert((ic->results->sent_time + ic->timeout) - now > 0.0);
      lh->timeout.repeat = (ic->results->sent_time + ic->timeout) - now;
      ev_timer_again (loop, &lh->timeout);
    }
  }
}


static void  icmp_timeout_cb(
  struct ev_loop *loop,
  ev_timer *w,
  int revents)
{
  struct icmp_ev_handle *lh = w->data;
  struct icmp_socket *ic = lh->ic;
  int seqno;
  ev_tstamp now = ev_now(loop);

  if (!ic->timeout)
    ev_timer_stop(loop, &lh->timeout);


  while ((seqno = icmp_socket_timeout(ic, now)) != 0) {
    if (lh->cb)
      lh->cb(lh->data, seqno, -1.0);
  }

  if (ic->results_len > 0) {
    lh->timeout.repeat = (ic->results->sent_time + ic->timeout) - now;
    ev_timer_again(loop, &lh->timeout);
  }
  else 
    ev_timer_stop(loop, &lh->timeout);

}

static void icmp_interval_cb(
  struct ev_loop *loop,
  ev_timer *w,
  int revents)
{
  struct icmp_ev_handle *lh = w->data;
  struct icmp_socket *ic = lh->ic;
  ev_tstamp now = ev_now(loop);

  if (icmp_socket_send(ic, now) < 0 && lh->cb)
    lh->cb(lh->data, 0, -1.0);

  if (ic->timeout) {
    if (ic->results_len == 1) {
      ev_timer_set(&lh->timeout, ic->timeout, 0.0);
      ev_timer_start(loop, &lh->timeout);
    }
  }

}


int ev_icmp_init(
    ev_icmp *h,
    void (*icmp_callback)(void *, int, double),
    char *addr,
    double interval,
    double timeout)
{
  assert(h);
  h->ic = icmp_socket_create(addr, interval, timeout);
  if (!h->ic)
    return 0;

  ev_io_init(&h->socket, icmp_receive_cb, h->ic->fd, EV_READ);
  ev_timer_init(&h->interval, icmp_interval_cb, 0.0, interval);
  ev_timer_init(&h->timeout, icmp_timeout_cb, timeout, 0.0);
  h->interval.data = h;
  h->timeout.data = h;
  h->socket.data = h;
  h->cb = icmp_callback;

  return 1;
}


void ev_icmp_destroy(
    struct ev_loop *l,
    ev_icmp *h)
{
  ev_io_stop(l, &h->socket);
  ev_timer_stop(l, &h->interval);
  ev_timer_stop(l, &h->timeout);
  icmp_socket_destroy(h->ic);

  return;
}


void ev_icmp_start(
    struct ev_loop *l,
    ev_icmp *h)
{
  icmp_socket_recreate(h->ic);
  ev_io_init(&h->socket, icmp_receive_cb, h->ic->fd, EV_READ);
  ev_timer_start(l, &h->interval); 
}


void ev_icmp_stop(
    struct ev_loop *l,
    ev_icmp *h)
{
  ev_io_stop(l, &h->socket);
  ev_timer_stop(l, &h->interval);
  ev_timer_stop(l, &h->timeout);
}
