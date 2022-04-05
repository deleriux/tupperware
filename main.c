#include "common.h"
#include "ev_icmp.h"
#include "ev_link.h"
#include "ini.h"

#include <ev.h>
#include <sys/auxv.h>
#include <signal.h>

struct {
  int entries;
  int argc;
  char **argv;
  struct entry {
    char *name;
    char *device;
    char *ping;
    double interval;
    double timeout;

    double average;
    int samples;
    int failures;
    double last_sent;
    struct entry *next;
    ev_icmp icmp;
  } *tuns;
} config;

void reload_cb(
    struct ev_loop *loop,
    ev_signal *w,
    int revents)
{
  sigset_t set;
  char *path = (char *)getauxval(AT_EXECFN);

  printf("Reloading daemon..\n");
  fflush(stdout);
  sigemptyset(&set);
  sigaddset(&set, SIGHUP);

  sigprocmask(SIG_UNBLOCK, &set, NULL);  
  execv(path, config.argv);
  ev_break(loop, EVBREAK_ALL);
}

static void update_stats(
    void *data,
    int seqno,
    double rtt)
{
  struct entry *e = data;
  int successes;


  successes = e->samples - e->failures;
  e->last_sent = ev_now(EV_DEFAULT);
  if (rtt > 0.0)
    e->average = ((e->average * (double)successes) + rtt) / ((double)successes + 1.0);
  else
    e->failures++;
  e->samples++;

  return;
}


static void print_stats(
    struct ev_loop *l,
    ev_signal *w,
    int revents)
{
  double now = ev_now(l);
  int successes;
  struct entry *e;

  if (config.tuns)
    printf("%16s/%-16s %6s %11s %8s %-8s\n", "device", "addr", "last", "rcv/sent", "percent", "rtt");

  for (e=config.tuns; e != NULL; e=e->next) {
    successes = e->samples - e->failures;
    printf("%16s/%-16s %5.1fs %5d/%-5d %6.1f%% %4.2fms\n",
    e->device, e->ping,
    (now - e->last_sent), successes, e->samples,
    ((double)successes/(double)e->samples) * 100,
    e->average * 1000);
  }
  fflush(stdout);
  return;
}


static void link_change(
    char *dev,
    int state)
{
  struct entry *e;
  for (e=config.tuns; e != NULL; e=e->next) {
    if (strcmp(e->device, dev) == 0) {
      if (state) {
        printf("%s up, Ping address %s, interval %.1fs, timeout %.1fs\n",
                e->device, e->ping, e->interval, e->timeout);
        ev_icmp_start(EV_DEFAULT, &e->icmp);
      }
      else {
        printf("%s down. Pinging suspended.\n", e->device);
        ev_icmp_stop(EV_DEFAULT, &e->icmp);
      }
      fflush(stdout);
    }
  }
}


static int cloexec_file(
    FILE *f)
{
  int fd = fileno(f);
  int flags = 0;

  flags = fcntl(fd, F_GETFD);
  flags |= FD_CLOEXEC;
  if (fcntl(fd, F_SETFD, flags) < 0)
    return -1;
  return 0;
}

static int config_parse(
    void *data,
    const char *section,
    const char *name,
    const char *value)
{
  struct entry *e;
  for (e=config.tuns; e != NULL; e = e->next) {
    if (strncmp(e->name, section, 64) == 0) {
      break;
    }
  }
  if (!e) {
    e = malloc(sizeof(*e));
    e->name = strdup(section);
    e->device = NULL; 
    e->ping = NULL;
    e->interval = 0.0;
    e->timeout = 0.0;
    e->next = config.tuns;
    e->samples = 0;
    e->average = 0;
    e->failures = 0;
    e->last_sent = 0;
    config.tuns = e;
    config.entries++;
  }

  if (strncmp(name, "dev", 3) == 0) {
    if (e->device) {
      warnx("Config parse failure. Duplicate entry: %s / %s", section, name);
      return 0;
    }
    e->device = strdup(value);
    assert(e->device);
  }
  else if (strncmp(name, "address", 7) == 0) {
    if (e->ping) {
      warnx("Config parse failure. Duplicate entry: %s / %s", section, name);
      return 0;
    }
    e->ping = strdup(value);
    assert(e->ping);
  }
  else if (strncmp(name, "timeout", 7) == 0) {
    if (e->timeout != 0.0) {
      warnx("Config parse failure. Duplicate entry: %s / %s", section, name);
      return 0;
    }
    e->timeout = atof(value);
    if (e->timeout < 1.0 || e->timeout > 180.0) {
      warnx("Config parse failure. Value %s in %s / %s should be between"
            " 1 and 180", value, section, name);
      return 0;
    }
  }
  else if (strncmp(name, "interval", 8) == 0) {
    if (e->interval != 0.0) {
      warnx("Config parse failure. Duplicate entry: %s / %s", section, name);
      return 0;
    }
    e->interval = atof(value);
    if (e->interval < 1.0 || e->interval > 86400.0) {
      warnx("Config parse failure. Value %s in %s / %s should be between"
            " 1 and 86400", value, section, name);
      return 0;
    }
  }
  else {
    warnx("Config parse failure. Unknown option: %s / %s", section, name);
    return 0;
  }

  return 1;
}

int main(
    int argc,
    char **argv) 
{
  struct ev_loop *loop = EV_DEFAULT;
  ev_signal sig, sig2;
  ev_link link;
  int fail = 0;
  char *fname = NULL;
  FILE *inifile = NULL;
  struct entry *e = NULL;

  config.entries = 0;
  config.tuns = NULL;
  config.argc = argc;
  config.argv = argv;

  if (argc > 1) 
    fname = argv[1];
  else
    fname = CONFIGFILE;
  inifile = fopen(fname, "r");
  if (!inifile)
    err(EXIT_FAILURE, "Cannot open config file: %s", fname);
  if (cloexec_file(inifile) < 0)
    err(EXIT_FAILURE, "Cannot reset flags on file");

  if (ini_parse_file(inifile, config_parse, &config) != 0)
    errx(EXIT_FAILURE, "Cannot parse config file %s", fname);
  fclose(inifile);

  if (!ev_link_init(&link, link_change))
    err(EXIT_FAILURE, "Cannot initialize link watcher");

  for (e=config.tuns; e != NULL; e=e->next) {
    assert(e->name);
    if (!e->device) {
      warnx("Config parse failure. Option \"dev\" must be set in section"
            " \"%s\"", e->name);
      fail = 1;
    }
    if (!e->ping) {
      warnx("Config parse failure. Option \"address\" must be set in section"
            " \"%s\"", e->name);
      fail = 1;
    }
    if (!e->timeout) {
      warnx("Config parse failure. Option \"timeout\" must be set in section"
            " \"%s\"", e->name);
      fail = 1;
    }
    if (!e->interval) {
      warnx("Config parse failure. Option \"interval\" must be set in section"
            " \"%s\"", e->name);
      fail = 1;
    }

   e->icmp.data = e;
    if (!ev_icmp_init(&e->icmp, update_stats, e->ping, e->interval, e->timeout))
      err(EXIT_FAILURE, "Cannot ping address");
    ev_link_add_device(&link, e->device);
  }

  if (fail)
    exit(EXIT_FAILURE);
  if (config.entries == 0)
    err(EXIT_FAILURE, "No devices set to watch. Exiting.");

  ev_signal_init(&sig2, print_stats, SIGUSR1);
  ev_signal_init(&sig, reload_cb, SIGHUP);
  ev_signal_start(loop, &sig);
  ev_signal_start(loop, &sig2);

  ev_link_start(loop, &link);

  ev_run(loop, 0);

  exit(0);
}
