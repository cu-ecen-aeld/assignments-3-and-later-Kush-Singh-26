#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#define PORT "9000"
#define DATAFILE "/var/tmp/aesdsocketdata"
#define BUFSIZE 1024

static volatile sig_atomic_t caught_signal = 0;
static int server_fd = -1;

void signal_handler(int signum) {
    caught_signal = 1;
}

int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);

    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(NULL, PORT, &hints, &res) != 0) {
        syslog(LOG_ERR, "getaddrinfo failed: %s", strerror(errno));
        return -1;
    }

    server_fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        freeaddrinfo(res);
        return -1;
    }

    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        freeaddrinfo(res);
        return -1;
    }

    if (bind(server_fd, res->ai_addr, res->ai_addrlen) == -1) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(server_fd);
        freeaddrinfo(res);
        return -1;
    }
    freeaddrinfo(res);

    if (listen(server_fd, 1) == -1) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (daemon_mode) {
        pid_t pid = fork();
        if (pid == -1) {
            syslog(LOG_ERR, "fork failed: %s", strerror(errno));
            close(server_fd);
            return -1;
        }
        if (pid > 0) {
            close(server_fd);
            return 0;
        }
        setsid();
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    while (!caught_signal) {
        struct sockaddr_storage client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd == -1) {
            if (caught_signal) break;
            syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            continue;
        }

        char ip_str[INET6_ADDRSTRLEN];
        struct sockaddr_in *s = (struct sockaddr_in *)&client_addr;
        inet_ntop(AF_INET, &s->sin_addr, ip_str, sizeof(ip_str));
        syslog(LOG_INFO, "Accepted connection from %s", ip_str);

        FILE *fp = fopen(DATAFILE, "a+");
        if (!fp) {
            syslog(LOG_ERR, "fopen failed: %s", strerror(errno));
            close(client_fd);
            continue;
        }

        char *buf = malloc(BUFSIZE);
        if (!buf) {
            syslog(LOG_ERR, "malloc failed");
            fclose(fp);
            close(client_fd);
            continue;
        }

        char *accum = NULL;
        size_t accum_len = 0;
        ssize_t recv_len;

        while ((recv_len = recv(client_fd, buf, BUFSIZE, 0)) > 0) {
            char *new_accum = realloc(accum, accum_len + recv_len);
            if (!new_accum) {
                syslog(LOG_ERR, "realloc failed");
                free(accum);
                free(buf);
                fclose(fp);
                close(client_fd);
                goto next_conn;
            }
            accum = new_accum;
            memcpy(accum + accum_len, buf, recv_len);
            accum_len += recv_len;

            char *newline;
            while ((newline = memchr(accum, '\n', accum_len)) != NULL) {
                size_t line_len = (newline - accum) + 1;
                fwrite(accum, 1, line_len, fp);
                fflush(fp);

                memmove(accum, newline + 1, accum_len - line_len);
                accum_len -= line_len;

                fseek(fp, 0, SEEK_END);
                long file_size = ftell(fp);
                fseek(fp, 0, SEEK_SET);

                char *send_buf = malloc(file_size);
                if (send_buf) {
                    fread(send_buf, 1, file_size, fp);
                    send(client_fd, send_buf, file_size, 0);
                    free(send_buf);
                }
            }
        }

        free(accum);
        free(buf);
        fclose(fp);
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", ip_str);
next_conn:;
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    close(server_fd);
    remove(DATAFILE);
    closelog();
    return 0;
}
