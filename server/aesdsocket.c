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
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <stdbool.h>

#define PORT "9000"
#define DATAFILE "/var/tmp/aesdsocketdata"
#define BUFSIZE 1024

typedef struct thread_node {
    pthread_t thread;
    struct thread_node *next;
} thread_node_t;

static volatile sig_atomic_t caught_signal = 0;
static int server_fd = -1;
static pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;
static thread_node_t *thread_list_head = NULL;
static bool exit_flag = false;
static timer_t timerid;

void signal_handler(int signum) {
    caught_signal = 1;
    exit_flag = true;
    timer_delete(timerid);
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
}

void thread_list_add(pthread_t thread) {
    thread_node_t *new_node = malloc(sizeof(thread_node_t));
    if (!new_node) {
        syslog(LOG_ERR, "malloc failed for thread node");
        return;
    }
    new_node->thread = thread;
    new_node->next = NULL;

    pthread_mutex_lock(&list_mutex);
    if (!thread_list_head) {
        thread_list_head = new_node;
    } else {
        thread_node_t *current = thread_list_head;
        while (current->next) {
            current = current->next;
        }
        current->next = new_node;
    }
    pthread_mutex_unlock(&list_mutex);
}

void thread_list_join_all(void) {
    pthread_mutex_lock(&list_mutex);
    thread_node_t *current = thread_list_head;
    while (current) {
        thread_node_t *to_join = current;
        current = current->next;
        pthread_mutex_unlock(&list_mutex);
        pthread_join(to_join->thread, NULL);
        pthread_mutex_lock(&list_mutex);
    }
    pthread_mutex_unlock(&list_mutex);
}

void thread_list_destroy(void) {
    pthread_mutex_lock(&list_mutex);
    thread_node_t *current = thread_list_head;
    while (current) {
        thread_node_t *tmp = current;
        current = current->next;
        free(tmp);
    }
    thread_list_head = NULL;
    pthread_mutex_unlock(&list_mutex);
}

void timestamp_write(void) {
    time_t now;
    struct tm *tm_info;
    char timestamp_buf[256];

    time(&now);
    tm_info = localtime(&now);
    strftime(timestamp_buf, sizeof(timestamp_buf), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);

    pthread_mutex_lock(&file_mutex);
    FILE *fp = fopen(DATAFILE, "a");
    if (fp) {
        fputs(timestamp_buf, fp);
        fclose(fp);
    }
    pthread_mutex_unlock(&file_mutex);
}

void timer_thread(union sigval sv) {
    timestamp_write();
}

void init_timer_sec(int seconds) {
    struct sigevent sev;
    struct itimerspec its;

    memset(&sev, 0, sizeof(sev));
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = timer_thread;
    sev.sigev_notify_attributes = NULL;

    if (timer_create(CLOCK_REALTIME, &sev, &timerid) == -1) {
        syslog(LOG_ERR, "timer_create failed: %s", strerror(errno));
        return;
    }

    its.it_value.tv_sec = seconds;
    its.it_value.tv_nsec = 0;
    its.it_interval.tv_sec = seconds;
    its.it_interval.tv_nsec = 0;

    if (timer_settime(timerid, 0, &its, NULL) == -1) {
        syslog(LOG_ERR, "timer_settime failed: %s", strerror(errno));
        timer_delete(timerid);
    }
}

void *thread_func(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);

    syslog(LOG_INFO, "Thread spawned for client_fd=%d", client_fd);

    char *buf = malloc(BUFSIZE);
    if (!buf) {
        syslog(LOG_ERR, "malloc failed");
        close(client_fd);
        return NULL;
    }

    char *accum = NULL;
    size_t accum_len = 0;
    ssize_t recv_len;

    while (!exit_flag && (recv_len = recv(client_fd, buf, BUFSIZE, 0)) > 0) {
        char *new_accum = realloc(accum, accum_len + recv_len);
        if (!new_accum) {
            syslog(LOG_ERR, "realloc failed");
            free(accum);
            free(buf);
            close(client_fd);
            return NULL;
        }
        accum = new_accum;
        memcpy(accum + accum_len, buf, recv_len);
        accum_len += recv_len;

        char *newline;
        while ((newline = memchr(accum, '\n', accum_len)) != NULL) {
            size_t line_len = (newline - accum) + 1;

            pthread_mutex_lock(&file_mutex);
            FILE *fp = fopen(DATAFILE, "a+");
            if (!fp) {
                syslog(LOG_ERR, "fopen failed: %s", strerror(errno));
                pthread_mutex_unlock(&file_mutex);
                free(accum);
                free(buf);
                close(client_fd);
                return NULL;
            }
            fwrite(accum, 1, line_len, fp);
            fflush(fp);

            fseek(fp, 0, SEEK_END);
            long file_size = ftell(fp);
            fseek(fp, 0, SEEK_SET);

            char *send_buf = malloc(file_size);
            if (send_buf) {
                fread(send_buf, 1, file_size, fp);
                pthread_mutex_unlock(&file_mutex);
                send(client_fd, send_buf, file_size, 0);
                free(send_buf);
            } else {
                pthread_mutex_unlock(&file_mutex);
            }
            fclose(fp);

            memmove(accum, newline + 1, accum_len - line_len);
            accum_len -= line_len;
        }
    }

    free(accum);
    free(buf);
    close(client_fd);
    syslog(LOG_INFO, "Closed connection client_fd=%d", client_fd);
    return NULL;
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

    if (listen(server_fd, 10) == -1) {
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

    init_timer_sec(10);

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

        int *client_fd_ptr = malloc(sizeof(int));
        if (!client_fd_ptr) {
            syslog(LOG_ERR, "malloc failed for client_fd");
            close(client_fd);
            continue;
        }
        *client_fd_ptr = client_fd;

        pthread_t thread;
        if (pthread_create(&thread, NULL, thread_func, client_fd_ptr) != 0) {
            syslog(LOG_ERR, "pthread_create failed: %s", strerror(errno));
            free(client_fd_ptr);
            close(client_fd);
            continue;
        }

        thread_list_add(thread);
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    thread_list_join_all();
    thread_list_destroy();
    if (server_fd != -1) {
        close(server_fd);
    }
    remove(DATAFILE);
    closelog();
    return 0;
}
