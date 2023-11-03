//
// Created by Kaiyuan Liu on 2023/11/1.
//

#ifndef PROJECT_2_15_441_INC_LOG_H_
#define PROJECT_2_15_441_INC_LOG_H_
#include <stdio.h>
#include <stdlib.h>

#define LOG
#include "cmu_packet.h"
#include "cmu_tcp.h"
#define LOG_BUFFER_SIZE 1024
void log_init(cmu_socket_type_t type);
void log_write(char *msg);
void log_write_format(const char *format, ...);
void log_extra(char *msg);
void log_packet_recv(uint8_t *packet, char *msg);
void log_packet_send(uint8_t *packet, char *msg);
void log_close();

#endif  // PROJECT_2_15_441_INC_LOG_H_
