/* A very simple HTTP flood tool
 *
 * Copyright (c) 2012, Angelo Marletta <angelo dot marletta at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <errno.h>
#include <signal.h>
#include <getopt.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <regex.h>

#define MAX_EVENTS 64
#define MAX_HEADERS 256
#define WRITE_BUFFER_SIZE 1024
#define READ_BUFFER_SIZE 16384
#define CONTENT_LENGTH "Content-Length"
#define CHUNKED_HEADER "Transfer-Encoding: chunked"
#define LAST_CHUNK "0\r\n\r\n"

struct http_client {
    int sockfd;
    int pending;
    int transfer;
};

enum transfer_mode {
    UNKNOWN,
    FIXED,
    CHUNKED
};

struct endpoint {
    char *protocol;
    char *host;
    int port;
    char *path;
};

int concurrency = 1;
int max_requests = -1;
int max_time = -1;
char *headers[MAX_HEADERS];
struct endpoint target;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void set_non_blocking(int sockfd)
{
    int flags;
    if ((flags = fcntl(sockfd, F_GETFL, 0)) == -1)
        flags = 0;
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) != 0)
        error("fcntl");
}

struct addrinfo *getaddr(const char *hostname, int port) {
    int ret = 0;
    struct addrinfo hints;
    struct addrinfo *result;
    char buffer[16];
    memset (&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;     /* Return IPv4 and IPv6 choices */
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */
    sprintf(buffer, "%d", port);
    ret = getaddrinfo(hostname, buffer, NULL, &result);
    if (ret < 0) {
        error("getaddrinfo");
    }
    return result;
}

void freeaddr(struct addrinfo *addr) {
    freeaddrinfo(addr);
}

void http_connect(struct http_client *client, struct addrinfo *addr)
{
    int sockfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sockfd == -1) {
        error("Cannot create the socket!");
    }
    set_non_blocking(sockfd);
    if (connect(sockfd, addr->ai_addr, addr->ai_addrlen) != 0 && errno == EINPROGRESS) {
        client->sockfd = sockfd;
        return;
    }
    else {
        printf("ERR %d\n", errno);
    }
    client->pending = 0;
    client->transfer = 0;
}

void http_close(struct http_client *client)
{
    shutdown(client->sockfd, SHUT_RDWR);
    close(client->sockfd);
}

void http_send_request(struct http_client *client, char *buffer, int length)
{
    int ret = send(client->sockfd, buffer, length, 0);
    if (ret < 0) {
        error("write");
    }
    client->pending++;
}

int read_status_line(char *buffer) {
    char *start = strchr(buffer, ' ');
    int status = strtol(start, NULL, 10);
    return status;
}

int read_http_header(char *buffer, char **key, char **value, int *key_length, int *value_length) {
    *key = buffer;
    *value = strchr(*key, ':') + 2;
    if (*value == NULL) return -1;
    *key_length = *value - *key - 2;
    *value_length = strchr(*value, '\r') - *value;
    return *key_length + *value_length + 4;
}

char *read_next_line(const char *buffer, int *bytes_read)
{
    char *new_line = strstr((char*)buffer, "\r\n");
    if (new_line == NULL) {
        *bytes_read = strlen(buffer);
        return NULL;
    }
    *bytes_read = new_line + 2 - buffer;
    return new_line + 2;
}

int http_read_response(struct http_client *client)
{
    static char read_buffer[READ_BUFFER_SIZE];
    int ret;
    ret = read(client->sockfd, read_buffer, sizeof(read_buffer));
    if (ret == sizeof(read_buffer)) {
        error("Read buffer filled up! This is not implemented yet :(");
    }
    if (ret > 0) {
        read_buffer[ret] = '\0';
        int response_count = 0;
        char *buffer_ptr = read_buffer;
        if (client->transfer == 1) {
            char *last = strstr(buffer_ptr, LAST_CHUNK);
            if (last != NULL) {
                buffer_ptr = last + sizeof(LAST_CHUNK);
                client->transfer = 0;
                response_count = 1;
            }
        }
        while (buffer_ptr < read_buffer + ret) {
            // parse response
            enum transfer_mode mode = UNKNOWN;
            int status = read_status_line(buffer_ptr);
            int content_length = -1;
            int read_bytes = 0;
            while ((buffer_ptr = read_next_line(buffer_ptr, &read_bytes)) != NULL) {
                if (read_bytes == 2) {
                    //headers read
                    break;
                }
                if (mode == UNKNOWN) {
                    if (strncasecmp(buffer_ptr, CONTENT_LENGTH, sizeof(CONTENT_LENGTH)-1) == 0) {
                        content_length = atoi(buffer_ptr + sizeof(CONTENT_LENGTH) + 1);
                        mode = FIXED;
                    }
                    else if (strncasecmp(buffer_ptr, CHUNKED_HEADER, sizeof(CHUNKED_HEADER)-1) == 0) {
                        mode = CHUNKED;
                    }
                }
            }
            if (mode == FIXED) {
                buffer_ptr += content_length;
                response_count++;
            }
            else if (mode == CHUNKED) {
                char *last = strstr(buffer_ptr, LAST_CHUNK);
                if (last == NULL) {
                    client->transfer = 1;
                    buffer_ptr += strlen(buffer_ptr);
                }
                else {
                    response_count++;
                    buffer_ptr = last + sizeof(LAST_CHUNK);
                }
            }
            else {
                printf("Cannot detect the transfer mode\n");
                exit(1);
            }
        }
        client->pending -= response_count;
        return response_count;
    }
    else if (ret < 0 && errno == EAGAIN) {
        printf("Try again!\n");
        return 0;
    }
    else {
        error("read");
        return -1;
    }
}

void run()
{
    int i, j;
    int ret;
    int efd;
    char write_buffer[WRITE_BUFFER_SIZE];
    struct http_client clients[concurrency];
    struct epoll_event ev[concurrency], *events;

    efd = epoll_create1(0);
    if (efd < 0) {
        error("epoll_create");
    }

    memset(clients, 0, sizeof(struct http_client) * concurrency);
    struct addrinfo *target_addr = getaddr(target.host, target.port);
    for (j=0; j<concurrency; j++) {
        http_connect(&clients[j], target_addr);
        ev[j].data.u32 = j;
        ev[j].events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR;
        ret = epoll_ctl(efd, EPOLL_CTL_ADD, clients[j].sockfd, &ev[j]);
        if (ret < 0) {
            error("epoll_ctl");
        }
    }
    events = (struct epoll_event*)calloc(MAX_EVENTS, sizeof(struct epoll_event));

    // create the HTTP request buffer
    int request_count = 0;
    int response_count = 0;
    int write_buffer_length = sprintf(write_buffer, "GET %s HTTP/1.1\r\nHost: %s\r\n", target.path, target.host);
    i = 0;
    while (headers[i]) {
        write_buffer_length += sprintf(write_buffer + write_buffer_length, "%s\r\n", headers[i]);
        i++;
    }
    write_buffer_length += sprintf(write_buffer + write_buffer_length, "\r\n");

    // The I/O loop
    while(response_count < max_requests || max_requests == -1) {
        int i, n;
        n = epoll_wait(efd, events, MAX_EVENTS, 1000);
        if (n < 0) {
            if (errno == EINTR) {
                printf("Interrupted! Resuming the loop...\n");
                continue;
            }
            printf("Error no %d", errno);
            error("epoll_wait");
        }
        for (i = 0; i < n; i++) {
            int client_id = events[i].data.u32;
            struct http_client *client = (struct http_client*)(clients + client_id);
            if (events[i].events & EPOLLRDHUP)
            {
                http_close(client);

                // reconnect the client
                http_connect(client, target_addr);
                ev[client_id].data.u32 = client_id;
                ev[client_id].events = EPOLLET | EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR;
                ret = epoll_ctl(efd, EPOLL_CTL_ADD, client->sockfd, &ev[client_id]);

                if (ret < 0) {
                    error("epoll_ctl");
                }
                if (client->pending) {
                    request_count -= client->pending;
                    client->pending = 0;
                }
                continue;
            }
            if (events[i].events & EPOLLERR)
            {
                error("EPOLLERR");
                goto exit_loop;
            }
            if (events[i].events & EPOLLHUP)
            {
                error("EPOLLHUP");
                goto exit_loop;
            }
            if (events[i].events & EPOLLIN) {
                // socket ready for reading
                int num_response = http_read_response(client);
                response_count += num_response;
            }
            if ((events[i].events & EPOLLOUT && (max_requests == -1 || request_count < max_requests))) {
                // socket ready for writing
                if (client->pending == 0) {
                    http_send_request(client, write_buffer, write_buffer_length);
                    request_count++;
                }
            }
        }
        //TODO estimate rate and print it from time to time
    }
exit_loop:
    freeaddr(target_addr);
    free(events);
    close(efd);
    printf("Requests sent: %d\n", request_count);
    printf("Responses received: %d\n", response_count);
    for (j=0; j<concurrency; j++) {
        http_close(&clients[j]);
    }
}

void print_usage(FILE *stream, int exit_code)
{
    fprintf(stream, "Usage: inundator [OPTIONS...] URL\n");
    fprintf(stream, "   OPTIONS\n");
    fprintf(stream, "      -n, --requests=N       Total number of requests\n");
    fprintf(stream, "      -c, --concurrency=N    Number of concurrent connections\n");
    fprintf(stream, "      -H, --header           Add a HTTP header\n");
    fprintf(stream, "      -h, --help             Display this help and exit\n");
    fprintf(stream, "\nReport bugs to https://github.com/opsengine/inundator\n");
    exit(exit_code);
}

int match(char *str, char *regex_str, regmatch_t *pmatch, int nmatch) {
    regex_t regex;
    if (regcomp(&regex, regex_str, REG_EXTENDED) != 0) {
        error("regcomp");
    }
    int ret = regexec(&regex, str, nmatch, pmatch, 0);
    regfree(&regex);
    return ret == 0;
}

int parse_url(char *url, struct endpoint *ep) {
    char *reg1 = "^(http)://([^/:]+):([0-9]+)(/.*)$"; //url with port
    char *reg2 = "^(http)://([^/:]+)()(/.*)$"; //url without port
    int match_count = 5;
    regmatch_t pmatch[match_count];
    if (!match(url, reg1, pmatch, match_count) && !match(url, reg2, pmatch, match_count)) {
        return 1;
    }
    int hostlen = pmatch[2].rm_eo - pmatch[2].rm_so;
    int pathlen = pmatch[4].rm_eo - pmatch[4].rm_so;
    ep->host = malloc(hostlen + 1);
    ep->path = malloc(pathlen + 1);
    memcpy(ep->host, url + pmatch[2].rm_so, hostlen);
    memcpy(ep->path, url + pmatch[4].rm_so, pathlen);
    ep->host[hostlen] = '\0';
    ep->path[pathlen] = '\0';
    if (pmatch[3].rm_so == pmatch[3].rm_eo)
        ep->port = 80;
    else
        ep->port = atoi(url + pmatch[3].rm_so);
    return 0;
}

int main(int argc, char **argv)
{
    const char* short_options = "+n:c:H:h";
    const struct option long_options[] = {
        { "requests",    required_argument, NULL, 'n' },
        { "concurrency", required_argument, NULL, 'c' },
        { "header",      required_argument, NULL, 'H' },
        { "help",        no_argument,       NULL, 'h' },
        { 0,             0,                 0,     0  }
    };
    int next_option;
    int option_index = 0;
    int count = 0;
    int header_count = 0;
    do {
        next_option = getopt_long(argc, argv, short_options, long_options, &option_index);
        count++;
        switch(next_option) {
            case 'n':
                max_requests = atoi(optarg);
                break;
            case 'c':
                concurrency = atoi(optarg);
                break;
            case 'H':
                headers[header_count++] = optarg;
                break;
            case 'h':
                print_usage(stdout, 1);
            case -1:
                break;
            default:
                abort();
        }
    } while(next_option != -1);
    if (argc < optind + 1) {
        //missing URL
        print_usage(stderr, 1);
    }
    headers[header_count] = NULL;
    char *url = argv[optind];
    if (parse_url(url, &target)) {
        printf("Unable to parse URL %s\n", url);
        exit(1);
    }
    run();
    return 0;
}
