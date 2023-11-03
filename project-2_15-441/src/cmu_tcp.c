/**
 * Copyright (C) 2022 Carnegie Mellon University
 *
 * This file is part of the TCP in the Wild course project developed for the
 * Computer Networks course (15-441/641) taught at Carnegie Mellon University.
 *
 * No part of the project may be copied and/or distributed without the express
 * permission of the 15-441/641 course staff.
 *
 *
 * This file implements the high-level API for CMU-TCP sockets.
 */

#include "cmu_tcp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "backend.h"
#include "log.h"

tcp_state_transition_t tcp_transition_table[] = {
    {STATE_CLOSED, EVENT_ACTIVE_OPEN, STATE_SYN_SENT, handshake_syn},
    {STATE_CLOSED, EVENT_PASSIVE_OPEN, STATE_LISTEN, NULL},
    {STATE_LISTEN, EVENT_RECV_SYN, STATE_SYN_RCVD, handshake_syn_ack},
    {STATE_SYN_SENT, EVENT_RECV_SYN_ACK, STATE_ESTABLISHED, handshake_ack},
    {STATE_SYN_SENT, EVENT_TIMEOUT, STATE_SYN_SENT, handshake_syn},
    {STATE_SYN_RCVD, EVENT_RECV_ACK, STATE_ESTABLISHED, NULL},
    {STATE_SYN_RCVD, EVENT_TIMEOUT, STATE_SYN_RCVD, handshake_syn_ack}
};

const char *tcp_state_string[] = {
    "CLOSED",      "LISTEN",     "SYN_RCVD",   "SYN_SENT",
    "ESTABLISHED", "FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT",
    "CLOSING",     "LAST_ACK",   "TIME_WAIT",
};

const uint64_t tcp_state_timeout[] = {
    UINT64_MAX, UINT64_MAX, DEFAULT_TIMEOUT, UINT64_MAX, UINT64_MAX, UINT64_MAX,
    UINT64_MAX, UINT64_MAX, UINT64_MAX,      UINT64_MAX, UINT64_MAX};

uint64_t get_time_ms() {
  struct timeval tv;
  uint64_t milliseconds;

  // Get the current time
  gettimeofday(&tv, NULL);

  // Convert timeval to milliseconds
  milliseconds = (long long)(tv.tv_sec) * 1000 + (uint64_t)(tv.tv_usec) / 1000;

  return milliseconds;
}

void *state_monitor(void *in) {
  cmu_socket_t *sock = (cmu_socket_t *)in;
  tcp_state_t prev_state = STATE_CLOSED;
  uint64_t start_time = get_time_ms();

  while (1) {
    usleep(1 * 1000);
    if (sock->state == prev_state) {
      if (tcp_state_timeout[sock->state] != UINT64_MAX &&
          get_time_ms() - start_time > tcp_state_timeout[sock->state]) {
        handle_event(EVENT_TIMEOUT, sock);
        start_time = get_time_ms();
      }
    } else {
      prev_state = sock->state;
      start_time = get_time_ms();
    }
    while (pthread_mutex_lock(&(sock->death_lock)) != 0) {
    }
    if (sock->dying) {
      pthread_mutex_unlock(&(sock->death_lock));
      break;
    }
    pthread_mutex_unlock(&(sock->death_lock));
  }
  log_write("State monitor thread exiting");
  pthread_exit(NULL);
}

int cmu_socket(cmu_socket_t *sock, const cmu_socket_type_t socket_type,
               const int port, const char *server_ip) {
  int sockfd, optval;
  socklen_t len;
  struct sockaddr_in conn, my_addr;
  log_init(socket_type);

  len = sizeof(my_addr);

  sockfd = socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd < 0) {
    perror("ERROR opening socket");
    return EXIT_ERROR;
  }
  sock->socket = sockfd;
  sock->received_buf = NULL;
  sock->received_len = 0;
  pthread_mutex_init(&(sock->recv_lock), NULL);

  sock->sending_buf = NULL;
  sock->sending_len = 0;
  pthread_mutex_init(&(sock->send_lock), NULL);

  sock->type = socket_type;
  sock->dying = 0;
  pthread_mutex_init(&(sock->death_lock), NULL);

  pthread_mutex_init(&(sock->state_lock), NULL);

  struct timeval tv;
  gettimeofday(&tv, NULL);
  unsigned int seed =
      (unsigned int)(tv.tv_sec * (uint64_t)1000000 + tv.tv_usec);
  sock->window.last_ack_received = rand_r(&seed);
  sock->window.next_seq_expected = 0;

  if (pthread_cond_init(&sock->wait_cond, NULL) != 0) {
    perror("ERROR condition variable not set\n");
    return EXIT_ERROR;
  }

  switch (socket_type) {
    case TCP_INITIATOR:
      if (server_ip == NULL) {
        perror("ERROR server_ip NULL");
        return EXIT_ERROR;
      }
      memset(&conn, 0, sizeof(conn));
      conn.sin_family = AF_INET;
      conn.sin_addr.s_addr = inet_addr(server_ip);
      conn.sin_port = htons(port);
      sock->conn = conn;

      my_addr.sin_family = AF_INET;
      my_addr.sin_addr.s_addr = htonl(INADDR_ANY);
      my_addr.sin_port = 0;
      if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("ERROR on binding");
        return EXIT_ERROR;
      }
      change_status(sock, STATE_CLOSED);
      handle_event(EVENT_ACTIVE_OPEN, sock);
      break;
    case TCP_LISTENER:
      memset(&conn, 0, sizeof(conn));
      conn.sin_family = AF_INET;
      conn.sin_addr.s_addr = htonl(INADDR_ANY);
      conn.sin_port = htons((uint16_t)port);

      optval = 1;
      setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
                 sizeof(int));
      if (bind(sockfd, (struct sockaddr *)&conn, sizeof(conn)) < 0) {
        perror("ERROR on binding");
        return EXIT_ERROR;
      }
      sock->conn = conn;
      change_status(sock, STATE_CLOSED);
      handle_event(EVENT_PASSIVE_OPEN, sock);
      break;
    default:
      perror("Unknown Flag");
      return EXIT_ERROR;
  }
  getsockname(sockfd, (struct sockaddr *)&my_addr, &len);
  sock->my_port = ntohs(my_addr.sin_port);

  pthread_create(&(sock->monitor_id), NULL, state_monitor, (void *)sock);
  pthread_create(&(sock->backend_id), NULL, begin_backend, (void *)sock);

  return EXIT_SUCCESS;
}

int cmu_close(cmu_socket_t *sock) {
  while (pthread_mutex_lock(&(sock->death_lock)) != 0) {
  }
  sock->dying = 1;
  pthread_mutex_unlock(&(sock->death_lock));

  pthread_join(sock->backend_id, NULL);
  pthread_join(sock->monitor_id, NULL);

  if (sock != NULL) {
    if (sock->received_buf != NULL) {
      free(sock->received_buf);
    }
    if (sock->sending_buf != NULL) {
      free(sock->sending_buf);
    }
  } else {
    perror("ERROR null socket\n");
    return EXIT_ERROR;
  }
  return close(sock->socket);
}

int cmu_read(cmu_socket_t *sock, void *buf, int length, cmu_read_mode_t flags) {
  uint8_t *new_buf;
  int read_len = 0;

  if (length < 0) {
    perror("ERROR negative length");
    return EXIT_ERROR;
  }

  while (pthread_mutex_lock(&(sock->recv_lock)) != 0) {
  }
  log_extra("recv lock is acquired in cmu_read");

  switch (flags) {
    case NO_FLAG:
      while (sock->received_len == 0) {
        log_extra("recv lock is released in cmu_read NO_FLAG");
        pthread_cond_wait(&(sock->wait_cond), &(sock->recv_lock));
        log_extra("recv lock is re-acquired in cmu_read NO_FLAG");
      }

    // Fall through.
    case NO_WAIT:
      if (sock->received_len > 0) {
        if (sock->received_len > length)
          read_len = length;
        else
          read_len = sock->received_len;

        memcpy(buf, sock->received_buf, read_len);
        if (read_len < sock->received_len) {
          new_buf = malloc(sock->received_len - read_len);
          memcpy(new_buf, sock->received_buf + read_len,
                 sock->received_len - read_len);
          free(sock->received_buf);
          sock->received_len -= read_len;
          sock->received_buf = new_buf;
        } else {
          free(sock->received_buf);
          sock->received_buf = NULL;
          sock->received_len = 0;
        }
      }
      break;
    default:
      perror("ERROR Unknown flag.\n");
      read_len = EXIT_ERROR;
  }

  log_extra("recv lock is released in cmu_read");
  pthread_mutex_unlock(&(sock->recv_lock));
  return read_len;
}

int cmu_write(cmu_socket_t *sock, const void *buf, int length) {
  while (pthread_mutex_lock(&(sock->send_lock)) != 0) {
  }
  if (sock->sending_buf == NULL)
    sock->sending_buf = malloc(length);
  else
    sock->sending_buf = realloc(sock->sending_buf, length + sock->sending_len);
  memcpy(sock->sending_buf + sock->sending_len, buf, length);
  sock->sending_len += length;

  pthread_mutex_unlock(&(sock->send_lock));
  return EXIT_SUCCESS;
}

void handshake(cmu_socket_t *sock, uint8_t flags, char *message) {
  while (pthread_mutex_lock(&(sock->send_lock)) != 0) {
  }
  uint16_t src = sock->my_port;
  uint16_t dst = ntohs(sock->conn.sin_port);
  uint32_t seq = sock->window.last_ack_received;
  uint32_t ack = sock->window.next_seq_expected;
  uint16_t hlen = sizeof(cmu_tcp_header_t);
  uint16_t plen = hlen;
  uint16_t adv_window = 1;
  uint16_t ext_len = 0;
  uint8_t *ext_data = NULL;
  uint8_t *packet = create_packet(src, dst, seq, ack, hlen, plen, flags,
                                  adv_window, ext_len, ext_data, NULL, 0);

  sendto(sock->socket, packet, plen, 0, (struct sockaddr *)&(sock->conn),
         sizeof(sock->conn));
  log_packet_send(packet, message);
  pthread_mutex_unlock(&(sock->send_lock));
}

void handshake_syn(cmu_socket_t *sock) {
  handshake(sock, SYN_FLAG_MASK, "First Handshake");
}

void handshake_syn_ack(cmu_socket_t *sock) {
  handshake(sock, SYN_FLAG_MASK | ACK_FLAG_MASK, "Second Handshake");
}

void handshake_ack(cmu_socket_t *sock) {
  handshake(sock, ACK_FLAG_MASK, "Third Handshake");
}

void change_status(cmu_socket_t *sock, int status) {
  while (pthread_mutex_lock(&(sock->state_lock)) != 0) {
  }
  sock->state = status;
  pthread_mutex_unlock(&(sock->state_lock));
}

bool handle_event(tcp_event_t event, cmu_socket_t *sock) {
  for (uint64_t i = 0;
       i < sizeof(tcp_transition_table) / sizeof(tcp_transition_table[0]);
       i++) {
    if (tcp_transition_table[i].current_state == sock->state &&
        tcp_transition_table[i].event == event) {
      log_write_format("Transitioning from %s to %s",
                       tcp_state_string[sock->state],
                       tcp_state_string[tcp_transition_table[i].next_state]);
      if (tcp_transition_table[i].action) {
        tcp_transition_table[i].action(sock);
      }
      change_status(sock, tcp_transition_table[i].next_state);
      return true;
    }
  }
  return false;
}
