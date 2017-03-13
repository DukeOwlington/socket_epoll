#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define UDP 1
#define TCP 2
#define BUFLEN 512
#define PORT 8888
#define MAX_TCP_USERS 4

/* initialize sockaddr_in struct */
struct sockaddr_in InitializeAddr(void) {
  struct sockaddr_in serv_addr;

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(PORT);
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

  return serv_addr;
}

/* create socket, 1 - UDP type; 2 - TCP type; */
int CreateSockets(int type) {
  int sock_desc;

  if (type == UDP) {
    /* create a UDP socket */
    if ((sock_desc = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror("socket");
      return -1;
    }
  } else if (type == TCP) {
    /* create a TCP socket */
    if ((sock_desc = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
      perror("socket");
      return -1;
    }
  }

  return sock_desc;
}


int BindnListen(int sock_desc, int type, struct sockaddr_in serv_addr) {

  /* bind socket to port */
  if (bind(sock_desc, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) == -1) {
    perror("bind");
    return -1;
  }

  /* try to specify maximum of 3 pending connections
   * if we specified TCP socket */
  if (type == TCP) {
    if ((listen(sock_desc, 3) < 0)) {
      perror("listen");
      exit(EXIT_FAILURE);
    }
  }

  return sock_desc;
}

/* UDP message handle scenario */
int HandleUDPConnection(int udp_sock) {
  int recv_len;
  unsigned int slen;
  char buf[BUFLEN];
  struct sockaddr_in client;

  /* try to receive some data, this is a blocking call */
  if ((recv_len = recvfrom(udp_sock, buf, BUFLEN, 0, (struct sockaddr *) &client,
                           &slen)) == -1) {
    perror("recvfrom");
    return -1;
  }
  printf("UDP message: %s\n", buf);

  /* now reply the client with the same data */
  if (sendto(udp_sock, buf, recv_len, 0, (struct sockaddr*) &client, slen) == -1) {
    perror("sendto");
    return -1;
  }

  return 0;
}

int MaxDesc(int nfds, int desc) {
  if (desc > nfds)
    nfds = desc + 1;
  return nfds;
}

void HandleTCPConnection(int usr_sock) {
  char buf[BUFLEN];
  /* receive a message from client
     * check if someone disconnected*/
  if (recv(usr_sock , buf , 512, 0) == 0) {
    close(usr_sock);
    usr_sock = 0;
    return;
  }
  /* send the message back to client */
  write(usr_sock, buf , strlen(buf));
  printf("TCP message: %s\n", buf);
}

int main(void) {
  int udp_sock; /* UDP descriptor */
  int tcp_sock; /* TCP descriptor */
  int tcp_usr_sock[MAX_TCP_USERS]; /* user connections on TCP socket */
  int i, j;
  int efd;
  int fd; /* temp varible for storing user socket descriptor */
  int addrlen; /* address length for tcp */
  struct sockaddr_in serv_addr; /* server address */
  struct sockaddr_in client; /* client address */
  struct epoll_event event, events[MAX_TCP_USERS + 2]; /* epoll events */

  for (i = 0; i < MAX_TCP_USERS; i++)
    tcp_usr_sock[i] = 0;
  /* get socket descriptors */
  udp_sock = CreateSockets(UDP);
  tcp_sock = CreateSockets(TCP);
  if (udp_sock < 0 || tcp_sock < 0)
    return EXIT_FAILURE;
  /* imitialize address */
  serv_addr = InitializeAddr();
  /* bind sockets to ports and listen (if TCP) */
  if (BindnListen(udp_sock, UDP, serv_addr) < 0 || BindnListen(tcp_sock, TCP, serv_addr) < 0)
    return EXIT_FAILURE;

  /* bit number of max fd */
  addrlen = sizeof(client);
  /* create epoll descriptor */
  efd = epoll_create(MAX_TCP_USERS + 2);
  /* add sockets to epoll monitor */
  event.data.fd = udp_sock;
  event.events = EPOLLIN;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, udp_sock, &event) < 0) {
    perror("epoll_ctl");
    return EXIT_FAILURE;
  }
  event.data.fd = tcp_sock;
  if (epoll_ctl(efd, EPOLL_CTL_ADD, tcp_sock, &event) < 0) {
    perror("epoll_ctl");
    return EXIT_FAILURE;
  }
  while (true) {
    fflush(stdout);
    for (i = 0; i < MAX_TCP_USERS; i++) {
      /* monitor set */
      if (epoll_wait(efd, events, MAX_TCP_USERS + 2, -1) < 0) {
        perror("poll");
        return EXIT_FAILURE;
      }
      /* UDP connection scenario */
      if (events[i].data.fd == udp_sock && (events[i].events & EPOLLIN)) {
        if (HandleUDPConnection(udp_sock) < 0)
          return EXIT_FAILURE;
      }

      /* TCP connection scenario
       * if it's a new connection then accept it
       * else handle client messages */
      if (events[i].data.fd == tcp_sock && (events[i].events & EPOLLIN)) {

        /* search free place for storing usr socket number */
        j = 0;
        while (tcp_usr_sock[j] != 0 && j < MAX_TCP_USERS)
          j++;

        if ((fd = accept(tcp_sock, (struct sockaddr *)&client, (socklen_t*)&addrlen)) < 0) {
          perror("accept");
          return EXIT_FAILURE;
        }

        /* if there's no space for new connection */
        if (i == MAX_TCP_USERS)
          close(fd);
        else {
          tcp_usr_sock[j] = fd;
          event.data.fd = tcp_usr_sock[j];
          if (epoll_ctl(efd, EPOLL_CTL_ADD, tcp_usr_sock[j], &event) < 0) {
            perror("epoll_ctl");
            return EXIT_FAILURE;
          }
        }

      }
      if (events[i].data.fd == tcp_usr_sock[i] && (events[i].events & EPOLLIN))
        HandleTCPConnection(tcp_usr_sock[i]);
    }
  }

  return 0;
}
