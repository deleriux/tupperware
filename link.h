#ifndef _LINK_H_
#define _LINK_H_
int link_socket(void);
int link_send(int fd);
int link_recv(int fd);

int link_online(char *name);
#endif
