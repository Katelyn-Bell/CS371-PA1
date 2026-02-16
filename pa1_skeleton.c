/*
# Copyright 2025 University of Kentucky
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
*/

/* 
Please specify the group members here

# Student #1: Katie Bell
# Student #2: Ian Rowe 
# Student #3: Kaleb Gordon 

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <pthread.h>

#define MAX_EVENTS 64
#define MESSAGE_SIZE 16
#define DEFAULT_CLIENT_THREADS 4

char *server_ip = "127.0.0.1";
int server_port = 12345;
int num_client_threads = DEFAULT_CLIENT_THREADS;
int num_requests = 1000000;

/*
 * This structure is used to store per-thread data in the client
 */
typedef struct {
    int epoll_fd;        /* File descriptor for the epoll instance, used for monitoring events on the socket. */
    int socket_fd;       /* File descriptor for the client socket connected to the server. */
    long long total_rtt; /* Accumulated Round-Trip Time (RTT) for all messages sent and received (in microseconds). */
    long total_messages; /* Total number of messages sent and received. */
    float request_rate;  /* Computed request rate (requests per second) based on RTT and total messages. */
} client_thread_data_t;

/*
 * This function runs in a separate client thread to handle communication with the server
 */
void *client_thread_func(void *arg) {
    client_thread_data_t *data = (client_thread_data_t *)arg;
    struct epoll_event event, events[MAX_EVENTS];
    char send_buf[MESSAGE_SIZE] = "ABCDEFGHIJKMLNOP"; /* Send 16-Bytes message every time */
    char recv_buf[MESSAGE_SIZE];
    struct timeval start, end;

    // Hint 1: register the "connected" client_thread's socket in the its epoll instance
    // Hint 2: use gettimeofday() and "struct timeval start, end" to record timestamp, which can be used to calculated RTT.

    /* TODO:
     * It sends messages to the server, waits for a response using epoll,
     * and measures the round-trip time (RTT) of this request-response.
     */
    if (data == NULL || data->socket_fd < 0 || data->epoll_fd < 0) {
        return NULL;
    }

    event.events = EPOLLIN;
    event.data.fd = data->socket_fd;
    if (epoll_ctl(data->epoll_fd, EPOLL_CTL_ADD, data->socket_fd, &event) < 0) {
        perror("epoll_ctl");
        close(data->socket_fd);
        close(data->epoll_fd);
        data->socket_fd = -1;
        data->epoll_fd = -1;
        return NULL;
    }

    for (int req = 0; req < num_requests; req++) {
        ssize_t sent = 0;
        ssize_t received = 0;

        gettimeofday(&start, NULL);

        while (sent < MESSAGE_SIZE) {
            ssize_t n = send(data->socket_fd, send_buf + sent, MESSAGE_SIZE - sent, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("send");
                goto out;
            }
            if (n == 0) goto out;
            sent += n;
        }

        while (received < MESSAGE_SIZE) {
            int nfds = epoll_wait(data->epoll_fd, events, MAX_EVENTS, -1);
            if (nfds < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait");
                goto out;
            }

            for (int i = 0; i < nfds; i++) {
                if (events[i].data.fd != data->socket_fd) continue;
                if (events[i].events & (EPOLLERR | EPOLLHUP)) goto out;

                if (events[i].events & EPOLLIN) {
                    ssize_t r = recv(data->socket_fd, recv_buf + received, MESSAGE_SIZE - received, 0);
                    if (r == 0) goto out;
                    if (r < 0) {
                        if (errno == EINTR) continue;
                        perror("recv");
                        goto out;
                    }
                    received += r;
                }
            }
        }

        gettimeofday(&end, NULL);
        long long rtt_us = (end.tv_sec - start.tv_sec) * 1000000LL + (end.tv_usec - start.tv_usec);
        data->total_rtt += rtt_us;
        data->total_messages++;

        if (memcmp(send_buf, recv_buf, MESSAGE_SIZE) != 0) {
            fprintf(stderr, "client_thread: echo mismatch\n");
            goto out;
        }
    }

    /* TODO:
     * The function exits after sending and receiving a predefined number of messages (num_requests). 
     * It calculates the request rate based on total messages and RTT
     */
out:
    if (data->total_rtt > 0) {
        data->request_rate = (float)data->total_messages / ((float)data->total_rtt / 1000000.0f);
    } else {
        data->request_rate = 0;
    }

    close(data->socket_fd);
    close(data->epoll_fd);
    data->socket_fd = -1;
    data->epoll_fd = -1;

    return NULL;
}

/*
 * This function orchestrates multiple client threads to send requests to a server,
 * collect performance data of each threads, and compute aggregated metrics of all threads.
 */
void run_client() {

    long long total_rtt = 0;       // Added this
    long total_messages = 0;      // Added this
    float total_request_rate = 0; // Added this
    
    pthread_t threads[num_client_threads];
    client_thread_data_t thread_data[num_client_threads];
    int thread_created[num_client_threads];
    struct sockaddr_in server_addr;

    /* TODO:
     * Create sockets and epoll instances for client threads
     * and connect these sockets of client threads to the server
     */
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        return;
    }
    
    // Hint: use thread_data to save the created socket and epoll instance for each thread
    // You will pass the thread_data to pthread_create() as below
    int created_count = 0;
    for (int i = 0; i < num_client_threads; i++) {
        thread_created[i] = 0;
        thread_data[i].socket_fd = -1;
        thread_data[i].epoll_fd = -1;
        thread_data[i].total_rtt = 0;
        thread_data[i].total_messages = 0;
        thread_data[i].request_rate = 0;

        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            perror("socket");
            continue;
        }

        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("connect");
            close(sockfd);
            continue;
        }

        int epfd = epoll_create1(0);
        if (epfd < 0) {
            perror("epoll_create1");
            close(sockfd);
            continue;
        }

        thread_data[i].socket_fd = sockfd;
        thread_data[i].epoll_fd = epfd;

        int rc = pthread_create(&threads[i], NULL, client_thread_func, &thread_data[i]);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            close(sockfd);
            close(epfd);
            thread_data[i].socket_fd = -1;
            thread_data[i].epoll_fd = -1;
            continue;
        }

        thread_created[i] = 1;
        created_count++;
    }

    /* TODO:
     * Wait for client threads to complete and aggregate metrics of all client threads
     */
    if (created_count == 0) {
        fprintf(stderr, "No client threads created\n");
        return;
    }

    for (int i = 0; i < num_client_threads; i++) {
        if (!thread_created[i]) continue;
        pthread_join(threads[i], NULL);

        long long avg_rtt = thread_data[i].total_messages ? (thread_data[i].total_rtt / thread_data[i].total_messages) : 0;
        printf("Thread %d Average RTT: %lld us\n", i, avg_rtt);
        printf("Thread %d Request Rate: %f messages/s\n", i, thread_data[i].request_rate);

        total_rtt += thread_data[i].total_rtt;
        total_messages += thread_data[i].total_messages;
        total_request_rate += thread_data[i].request_rate;
    }

    if (total_messages > 0) {
        printf("Average RTT: %lld us\n", total_rtt / total_messages);
    } else {
        printf("Average RTT: 0 us\n");
    }
    printf("Total Request Rate: %f messages/s\n", total_request_rate);
}

void run_server() {

    /* TODO:
     * Server creates listening socket and epoll instance.
     * Server registers the listening socket to epoll
     */
    int server_fd, epoll_fd;
    struct sockaddr_in address;
    struct epoll_event event;
    struct epoll_event events[MAX_EVENTS];

    // make server socket 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed ");
        exit(EXIT_FAILURE);
    }

    // server can restart immediatly 
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // assign network address to blank socket 
    address.sin_family = AF_INET;
    if (inet_pton(AF_INET, server_ip, &address.sin_addr) <= 0) {
        perror("Inet_pton failed");
        exit(EXIT_FAILURE);
    }
    address.sin_port = htons(server_port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Start Listening + up to 10 connections 
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // make epoll instance 
    if ((epoll_fd = epoll_create1(0)) < 0) {
        perror("Epoll failed");
        exit(EXIT_FAILURE);
    }

    // add server socket to epoll
    event.events = EPOLLIN; //data ready to read 
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) < 0) {
        perror("Epoll_ctr failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on %s:%d\n", server_ip, server_port);

    /* Server's run-to-completion event loop */
    while (1) {
        /* TODO:
         * Server uses epoll to handle connection establishment with clients
         * or receive the message from clients and echo the message back
         */
        // Wait for events (timeout = -1 means wait forever)
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        
        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == server_fd) {
                // CASE 1: new server trying to connect
                int client_fd;
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);
                
                if ((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len)) < 0) {
                    perror("Accept failed");
                    continue;
                }
                
                // register new client with epoll
                event.events = EPOLLIN;
                event.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                    perror("epoll adding client failed");
                    close(client_fd);
                }
            } else {
                // CASE 2: existing client sent data 
                int client_fd = events[i].data.fd;
                char buffer[MESSAGE_SIZE];
                
                // read data
                int bytes_read = read(client_fd, buffer, MESSAGE_SIZE);
                
                if (bytes_read <= 0) {
                    // if read = 0 then is gone
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
                    close(client_fd);
                } else {
                    // send same bytes 
                    write(client_fd, buffer, bytes_read);
                }
            }
        }
    }
    close(server_fd);
    close(epoll_fd);
}

int main(int argc, char *argv[]) {
    if (argc > 1 && strcmp(argv[1], "server") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);

        run_server();
    } else if (argc > 1 && strcmp(argv[1], "client") == 0) {
        if (argc > 2) server_ip = argv[2];
        if (argc > 3) server_port = atoi(argv[3]);
        if (argc > 4) num_client_threads = atoi(argv[4]);
        if (argc > 5) num_requests = atoi(argv[5]);

        run_client();
    } else {
        printf("Usage: %s <server|client> [server_ip server_port num_client_threads num_requests]\n", argv[0]);
    }

    return 0;
}
