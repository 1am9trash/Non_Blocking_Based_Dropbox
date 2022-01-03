#include <sys/select.h>
#include <sys/errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <queue>
using namespace std;

#define DEBUG

#define BUF_SIZE 1024
#define BACKLOG 20

struct PACKAGE {
    int mode, len;
    char buf[BUF_SIZE];

    PACKAGE() {
        this->mode = -1;
    }
};

void log_info(bool error, const char *msg) {
#ifdef DEBUG
    if (!error)
        fprintf(stdout, "%s", msg);
    else {
        fprintf(stderr, "%s", msg);
        exit(1);
    }
#endif
}

void set_no_blocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1)
        log_info(true, "[ERROR] failed to get fd flags.\n");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        log_info(true, "[ERROR] failed to set fd flags.\n");
}

int main(int argc, char *argv[]) {
    if (argc != 4)
        log_info(true, "[USAGE] <program> <IP> <port> <username>\n");

    int port;
    if (sscanf(argv[2], "%d", &port) != 1 || port < 0)
        log_info(true, "[ERROR] port must be a positive number.\n");

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
        log_info(true, "[ERROR] socket() error.\n");

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    inet_aton(argv[1], &server_address.sin_addr);

    if (connect(server_fd, (sockaddr *)&server_address, sizeof(server_address)) == -1)
        log_info(true, "[ERROR] connect() error.\n");

    int max_fd = server_fd;
    fd_set rd, rd_backup, wr, wr_backup;
    FD_ZERO(&rd_backup);
    FD_ZERO(&wr_backup);
    FD_SET(0, &rd_backup);
    FD_SET(server_fd, &rd_backup);
    FD_SET(server_fd, &wr_backup);

    PACKAGE up_package, down_package;
    up_package.mode = 0;
    up_package.len = -1;
    strcpy(up_package.buf, argv[3]);
    send(server_fd, &up_package, sizeof(up_package), 0);

    FILE *wr_fd = NULL;
    FILE *rd_fd = NULL;

    bool put_suc = true;

    char buf[BUF_SIZE];
    int second;
    char up_name[BUF_SIZE], down_name[BUF_SIZE];
    buf[0] = '\0';

    set_no_blocking(server_fd);

    while (true) {
        rd = rd_backup;
        wr = wr_backup;

        int status = select(max_fd + 1, &rd, &wr, NULL, NULL);
        if (status < 0)
            log_info(true, "[ERROR] select() error.\n");
        else if (status > 0) {
            if (strlen(buf) == 0 && FD_ISSET(0, &rd)) {
                scanf("%s", buf);
                if (!strcmp(buf, "/sleep"))
                    scanf("%d", &second);
                else if (!strcmp(buf, "/put"))
                    scanf("%s", up_name);
            }

            if (strlen(buf) != 0) {
                if (!strcmp(buf, "/exit")) {
                    if (wr_fd != NULL)
                        fclose(wr_fd);
                    if (rd_fd != NULL)
                        fclose(rd_fd);
                    close(server_fd);

                    break;
                } else if (!strcmp(buf, "/sleep")) {
                    printf("The client starts to sleep.\n");
                    for (int i = 0; i < second; i++) {
                        sleep(1);
                        printf("Sleep %d.\n", i + 1);
                    }
                    printf("Client wakes up.\n");

                    buf[0] = '\0';
                } else if(!strcmp(buf, "/put")) {
                    if (FD_ISSET(server_fd, &wr)) {
                        if (rd_fd == NULL) {
                            if (access(up_name, F_OK) == -1) {
                                log_info(false, "[INFO] file didn't exist.\n");
                                buf[0] = '\0';
                            } else {
                                up_package.mode = 1;
                                up_package.len = -1;
                                strcpy(up_package.buf, up_name);

                                int len = send(server_fd, &up_package, sizeof(up_package), 0);
                                if (len == -1) {
                                    if (errno == EAGAIN)
                                        log_info(false, "[INFO] send() not finished.\n");
                                    else
                                        log_info(true, "[ERROR] send() error.\n");
                                } else {
                                    rd_fd = fopen(up_package.buf, "rb");

                                    printf("[Upload] %s Start!\n", up_name);
                                    printf("Progress : [######################]\n");
                                }
                            }
                        } else {
                            if (put_suc) {
                                up_package.mode = 2;
                                up_package.len = fread(up_package.buf, 1, BUF_SIZE, rd_fd);
                            }

                            int len = send(server_fd, &up_package, sizeof(up_package), 0);
                            if (len == -1) {
                                put_suc = false;
                                if (errno == EAGAIN)
                                    log_info(false, "[INFO] send() not finished.\n");
                                else
                                    log_info(true, "[ERROR] send() error.\n");
                            } else {
                                put_suc = true;
                                if (up_package.len == 0) {
                                    fclose(rd_fd);
                                    rd_fd = NULL;
                                    buf[0] = '\0';

                                    printf("[Upload] %s Finish!\n", up_name);
                                }
                            }
                        }
                    }
                } else 
                    buf[0] = '\0';
            }

            if (FD_ISSET(server_fd, &rd)) {
                int len = recv(server_fd, &down_package, sizeof(down_package), 0);
                if (len == -1) {
                    if (errno == EAGAIN)
                        log_info(false, "[INFO] recv() not finished.\n");
                    else
                        log_info(true, "[ERROR] recv() error.\n");
                } else if (len == 0) {
                    if (wr_fd != NULL)
                        fclose(wr_fd);
                    if (rd_fd != NULL)
                        fclose(rd_fd);
                    close(server_fd);

                    break;
                } else {
                    if (down_package.mode == 1) {
                        wr_fd = fopen(down_package.buf, "wb");
                        strcpy(down_name, down_package.buf);

                        printf("[Download] %s Start!\n", down_name);
                        printf("Progress : [######################]\n");
                    }
                    else if (down_package.mode == 2) {
                        if (wr_fd == NULL)
                            log_info(true, "[ERROR] recv mode 2 error. no wr_fd.\n");
                        if (down_package.len == 0) {
                            fclose(wr_fd);
                            wr_fd = NULL;
                            
                            printf("[Download] %s Finish!\n", down_name);
                        }
                        else
                            fwrite(down_package.buf, 1, down_package.len, wr_fd);
                    }
                }
            }
        }
    }

    return 0;
}
