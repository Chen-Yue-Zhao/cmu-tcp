//
// Created by Kaiyuan Liu on 2023/11/1.
//
#include "log.h"

#ifdef LOG
#include <stdarg.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
FILE *log_file;
FILE *extra_log_file;

void log_time() {
  struct timeval tv;
  struct tm timeinfo;

  gettimeofday(&tv, NULL);  // Get the current time down to microseconds

  // Convert the seconds part to a struct tm using localtime_r
  localtime_r(&tv.tv_sec, &timeinfo);

  // Print just the time (hours, minutes, seconds) and microseconds to the log
  // file
  fprintf(log_file, "%02d:%02d:%02d.%06ld ", timeinfo.tm_hour, timeinfo.tm_min,
          timeinfo.tm_sec, tv.tv_usec);

  fflush(log_file);
}

void log_init(cmu_socket_type_t type) {
  switch (type) {
    case TCP_INITIATOR:
      log_file = fopen("client.log", "w");
      extra_log_file = fopen("extra_client.log", "w");
      break;
    case TCP_LISTENER:
      log_file = fopen("server.log", "w");
      extra_log_file = fopen("extra_server.log", "w");
      break;
    default:
      break;
  }
  fflush(log_file);
}

void log_write(char *msg) {
  fprintf(log_file, "%s\n", msg);
  fflush(log_file);
}

void log_write_format(const char *format, ...) {
  char buffer[LOG_BUFFER_SIZE];
  va_list args;

  va_start(args, format);
  vsnprintf(buffer, LOG_BUFFER_SIZE, format, args);
  va_end(args);

  log_write(buffer);
}

void log_extra(char *msg) {
  fprintf(extra_log_file, "%s\n", msg);
  fflush(extra_log_file);
}

void log_packet(uint8_t *packet, char *msg) {
  if (msg == NULL) {
    msg = "";
  }
  cmu_tcp_header_t *header = (cmu_tcp_header_t *)packet;
  switch (header->flags) {
    case SYN_FLAG_MASK:
      fputs("SYN ", log_file);
      break;
    case SYN_FLAG_MASK | ACK_FLAG_MASK:
      fputs("SYN ACK ", log_file);
      break;
    case ACK_FLAG_MASK:
      fputs("ACK ", log_file);
      break;
    case FIN_FLAG_MASK:
      fputs("FIN ", log_file);
      break;
    default:
      fputs("DATA ", log_file);
      break;
  }
  fprintf(log_file,
          "seq: %u, ack: %u, adv: %u "
          "size: %u %s\n",
          header->seq_num, header->ack_num, header->advertised_window,
          header->plen - header->hlen, msg);
  fflush(log_file);
}

void log_packet_recv(uint8_t *packet, char *msg) {
  log_time();
  fputs("RECV: ", log_file);
  log_packet(packet, msg);
}
void log_packet_send(uint8_t *packet, char *msg) {
  log_time();
  fputs("SEND: ", log_file);
  log_packet(packet, msg);
}

void log_close() { fclose(log_file); }
#else
void log_init(cmu_socket_type_t type) { (void)type; }
void log_write(char *msg) { (void)msg; }
void log_write_format(const char *format, ...) { (void)format; }
void log_extra(char *msg) { (void)msg; }
void log_packet_recv(uint8_t *packet, char *msg) {
  (void)packet;
  (void)msg;
}
void log_packet_send(uint8_t *packet, char *msg) {
  (void)packet;
  (void)msg;
}
void log_close() {}
#endif
