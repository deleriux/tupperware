#include "common.h"
#include "icmp.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip_icmp.h>
#include <netdb.h>
#include <errno.h>
#include <err.h>

#define ICMP_PAYLOAD "tupperware"

static int create_echo_packet(unsigned short seqno, void *data, int sz);
static int create_icmp_socket(const char *addr);
static int recreate_icmp_socket(int *fd);

static int create_echo_packet(
    unsigned short seqno, 
    void *data,
    int sz)
{
  if (sz < sizeof(struct icmphdr) + strlen(ICMP_PAYLOAD))
    return 0;

  if (!data)
    return 0;

  char *payload = data + sizeof(struct icmphdr);
  struct icmphdr *rq = data;

  rq->type = ICMP_ECHO;
  rq->code = 0;
  rq->un.echo.id = 0;
  rq->un.echo.sequence = htons(seqno);
  memcpy(payload, ICMP_PAYLOAD, strlen(ICMP_PAYLOAD));

  return 1;
}

static int recreate_icmp_socket(
    int *fd)
{
  struct sockaddr addr;
  socklen_t len;
  int yes = 1;
  int f = -1;

  if (getpeername(*fd, &addr, &len) < 0)
    return -1;

  f = socket(addr.sa_family, SOCK_DGRAM|SOCK_CLOEXEC, IPPROTO_ICMP);
  if (f < 0) {
    warn("Cannot create socket");
    goto fail;
  }

  if (setsockopt(f, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    warn("Cannot set socket option");
    goto fail;
  }

  if (connect(f, &addr, len) < 0) {
    warn("Cannot connect to socket");
    goto fail;
  }

  close(*fd);
  *fd = f;
  return f;

fail:
  close(f);
  return -1;

}

static int create_icmp_socket(
    const char *addr)
{
  int rc = -1;
  int fd = -1;
  int yes = 1;
  struct addrinfo hints, *ai = NULL;

  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_DGRAM;
  hints.ai_protocol = 0;

  if ((rc = getaddrinfo(addr, NULL, &hints, &ai))) {
    warnx("Cannot obtain IP address: %s", gai_strerror(rc));
    goto fail;
  }

  fd = socket(ai->ai_family, ai->ai_socktype|SOCK_CLOEXEC, IPPROTO_ICMP);
  if (fd < 0) { 
    warn("Cannot create socket");
    goto fail;
  }

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    warn("Cannot set socket option");
    goto fail;
  }

  if (connect(fd, ai->ai_addr, ai->ai_addrlen) < 0) {
    warn("Cannot connect to socket");
    goto fail;
  }

  freeaddrinfo(ai);
  return fd;

fail:
  if (ai)
    freeaddrinfo(ai);
  close(fd);

  return -1;
}

int icmp_socket_fd(
    struct icmp_socket *ic)
{
  assert(ic);
  return ic->fd;
}



int icmp_socket_send(
    struct icmp_socket *ic,
    double timestamp)
{
  int rc;
  int len = sizeof(struct icmphdr) + 16;
  void *packet = alloca(len);
  struct rlist *rl = NULL, *t = NULL;

  create_echo_packet(++ic->seqno, packet, len);
  rc = send(ic->fd, packet, len, MSG_NOSIGNAL);
  if (rc != len)
    return -1;

  rl = malloc(sizeof(*rl));
  assert(rl);

  rl->sequence = ic->seqno;
  rl->sent_time = timestamp;
  rl->recv_time = -1.0;
  rl->next = NULL;
  if (!ic->results) {
    ic->results = rl;
  }
  else {
    t = ic->results;
    while (t->next != NULL)
      t = t->next;
    t->next = rl;
  }
  ic->results_len++;

  return rc;
}



int icmp_socket_recv(
    struct icmp_socket *ic,
    double *timestamp)
{
  int rc;
  int len = sizeof(struct icmphdr) + 16;
  struct icmphdr *hdr = NULL;
  struct rlist *rl, *la = NULL;
  uint16_t seq;

  void *packet = alloca(len);
  rc = recv(ic->fd, packet, len, 0);
  if (rc != len)
    return -1;

  hdr = packet;
  seq = ntohs(hdr->un.echo.sequence);
  for (rl=ic->results; rl != NULL; rl=rl->next) {
    if (rl->sequence == seq) {
      if (!la)
        ic->results = ic->results->next;
      else
        la->next = rl->next;

      *timestamp = rl->sent_time;
      free(rl);
      ic->results_len--;
      return seq;
    }
    else {
      la = rl;
    }
  }

  return 0;
}


int icmp_socket_timeout(
    struct icmp_socket *ic,
    double now)
{
  assert(ic);
  struct rlist *rl;
  int rc = 0;
  double t; 

  if (!ic->results)
    return 0;

  t = (ic->results->sent_time + ic->timeout) - now;
  if (t < 0.0) {
    rl = ic->results;
    ic->results = ic->results->next;
    rc = rl->sequence;
    free(rl);
    ic->results_len--;
  }

  return rc;
}

struct icmp_socket * icmp_socket_create(
    const char *addr,
    double interval,
    double timeout) 
{
  struct icmp_socket *ic = NULL;

  ic = malloc(sizeof(*ic));
  if (!ic)
    return NULL;

  memset(ic, 0, sizeof(*ic));

  ic->fd = create_icmp_socket(addr);
  if (ic->fd < 0)
    goto fail;

  ic->addr = strdup(addr);
  if (!ic->addr)
    goto fail;

  ic->seqno = 0;
  if (timeout < 0.001) 
    goto fail;
  ic->timeout = timeout;
  if (interval < 0.01)
    goto fail;
  ic->interval = interval;

  ic->results = NULL;
  ic->results_len = 0;

  return ic;

fail:
  if (ic) {
    if (ic->fd > -1)
      close(ic->fd);
    if (ic->addr)
      free(ic->addr);
  } 
}


int icmp_socket_recreate(
    struct icmp_socket *ic)
{
  if (!ic)
    return -1;

  if (recreate_icmp_socket(&ic->fd) < 0)
    return -1;

  return 0;  
}


void icmp_socket_destroy(
    struct icmp_socket *ic)
{
  struct rlist *rl;
  if (!ic)
    return;

  if (ic->addr)
    free(ic->addr);
  if (ic->fd > -1)
    close(ic->fd);
  while (ic->results) {
    rl = ic->results;
    ic->results->next = rl->next;
    free(rl);
  }
  return;
}
