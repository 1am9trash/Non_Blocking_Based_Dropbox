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

struct FILE_STATE {
    /* 
        mode
        0: idle
        1: read
        2: write
    */
    char name[30];
    int mode;

    FILE_STATE() {}
    FILE_STATE(char *name, int mode) {
        strcpy(this->name, name);
        this->mode = mode;
    }
};

struct FOLDER {
    char name[30];
    vector<FILE_STATE> files;

    FOLDER() {}
    FOLDER(char *name) {
        strcpy(this->name, name);
    }
};

struct PACKAGE {
    /*
       mode
       0: username
       1: filename
       2: data
    */
    int mode, len;
    char buf[BUF_SIZE];

    PACKAGE() {
        this->mode = -1;
    }
};

struct USER {
    char name[30], rd_name[30], wr_name[30];
    int fd;
    FILE *rd_fd, *wr_fd;
    queue<char *> rd_list;
    PACKAGE cur_case, up_package;

    USER() {
        this->fd = -1;
        this->rd_fd = NULL;
        this->wr_fd = NULL;
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

void change_file_state(vector<FOLDER>& folders, char *folder_name, char *file_name, int mode) {
    int folder_id = -1;
    for (int i = 0; i < folders.size(); i++) {
        if (!strcmp(folder_name, folders[i].name)) {
            folder_id = i;
            break;
        }
    }
    if (folder_id == -1)
        log_info(true, "[ERROR] change_file_state() error. no such folder.\n");

    int file_id = -1;
    for (int i = 0; i < folders[folder_id].files.size(); i++) {
        if (!strcmp(file_name, folders[folder_id].files[i].name)) {
            file_id = i;
            break;
        }
    }
    if (file_id == -1)
        log_info(true, "[ERROR] change_file_state() error. no such file.\n");

    folders[folder_id].files[file_id].mode = mode;
}

void user_exit(USER& user, vector<FOLDER>& folders) {
    close(user.fd);
    user.fd = -1;
    
    if (user.rd_fd != NULL) {
        fclose(user.rd_fd);
        user.rd_fd = NULL;
        change_file_state(folders, user.name, user.rd_name, 0);
    }

    if (user.wr_fd != NULL) {
        fclose(user.wr_fd);
        user.wr_fd = NULL;
        change_file_state(folders, user.name, user.wr_name, 0);
    }
}

bool is_user_exit(USER& user) {
    return user.fd == -1;
}

int main(int argc, char *argv[]) {
    if (argc != 2)
        log_info(true, "[USAGE] <program> <port>\n");

    int port;
    if (sscanf(argv[1], "%d", &port) != 1 || port < 0)
        log_info(true, "[ERROR] port must be a positive number.\n");

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1)
        log_info(true, "[ERROR] socket() error.\n");

    sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    inet_aton("127.0.0.1", &server_address.sin_addr);

    if (bind(server_fd, (sockaddr *)&server_address, sizeof(server_address)) == -1)
        log_info(true, "[ERROR] bind() error.\n");

    if (listen(server_fd, BACKLOG) == -1)
        log_info(true, "[ERROR] listen() error.\n");

    set_no_blocking(server_fd);

    vector<USER> clients;
    vector<FOLDER> folders;

    int max_fd = server_fd;
    fd_set rd, rd_backup, wr, wr_backup;
    FD_ZERO(&rd_backup);
    FD_ZERO(&wr_backup);
    FD_SET(server_fd, &rd_backup);
    FD_SET(server_fd, &wr_backup);

    char path[100];
    while (true) {
        rd = rd_backup;
        wr = wr_backup;

        int status = select(max_fd + 1, &rd, &wr, NULL, NULL);
        if (status < 0)
            log_info(true, "[ERROR] select() error.\n");
        else if (status > 0) {
            if (FD_ISSET(server_fd, &rd)) {
                int client_fd = accept(server_fd, NULL, NULL);
                if (client_fd == -1) {
                    if (errno == EAGAIN)
                        log_info(false, "[INFO] accept() not finished.\n");
                    else
                        log_info(true, "[ERROR] accept() error.\n");
                } else {
                    log_info(false, "[INFO] accept() new client.\n");

                    USER client;
                    client.fd = client_fd;
                    clients.push_back(client);

                    FD_SET(client_fd, &rd_backup);
                    FD_SET(client_fd, &wr_backup);
                    max_fd = max(max_fd, client_fd);
                }
            }

            for (int i = 0; i < clients.size(); i++) {
                if (clients[i].cur_case.mode != -1) {
                    if (clients[i].cur_case.mode == 0) {
                        strcpy(clients[i].name, clients[i].cur_case.buf);
                        mkdir(clients[i].name, 0777);

                        int folder_id = -1;
                        for (int j = 0; j < folders.size(); j++) {
                            if (!strcmp(clients[i].name, folders[j].name)) {
                                folder_id = j;
                                break;
                            }
                        }
                        if (folder_id == -1)
                            folders.push_back(FOLDER(clients[i].name));
                        else {
                            for (int j = 0; j < folders[folder_id].files.size(); j++) {
                                char *file_name = new char(strlen(folders[folder_id].files[j].name) + 1);
                                strcpy(file_name, folders[folder_id].files[j].name);
                                clients[i].rd_list.push(file_name);
                            }
                        }

                        clients[i].cur_case.mode = -1;
                    } else if (clients[i].cur_case.mode == 1) {
                        if (clients[i].wr_fd != NULL)
                            continue;

                        int folder_id = -1;
                        for (int j = 0; j < folders.size(); j++) {
                            if (!strcmp(clients[i].name, folders[j].name)) {
                                folder_id = j;
                                break;
                            }
                        }
                        if (folder_id == -1)
                            log_info(true, "[ERROR] recv mode 1 error. no such folder.\n");

                        int file_id = -1;
                        for (int j = 0; j < folders[folder_id].files.size(); j++) {
                            if (!strcmp(clients[i].cur_case.buf, folders[folder_id].files[j].name)) {
                                file_id = j;
                                break;
                            }
                        }
                        if (file_id != -1 && folders[folder_id].files[file_id].mode != 1)
                            continue;

                        strcpy(clients[i].wr_name, clients[i].cur_case.buf);
                        if (file_id == -1)
                            folders[folder_id].files.push_back(FILE_STATE(clients[i].wr_name, 2));
                        strcpy(path, clients[i].name);
                        strcat(path, "/");
                        strcat(path, clients[i].wr_name);
                        clients[i].wr_fd = fopen(path, "wb");

                        clients[i].cur_case.mode = -1;
                    } else if (clients[i].cur_case.mode == 2) {
                        if (clients[i].wr_fd == NULL)
                            log_info(true, "[ERROR] recv mode 2 error. no wr_fd.\n");

                        if (clients[i].cur_case.len == 0) {
                            fclose(clients[i].wr_fd);
                            clients[i].wr_fd = NULL;
                            change_file_state(folders, clients[i].name, clients[i].wr_name, 0);
                            
                            for (int j = 0; j < clients.size(); j++) {
                                if (i == j || strcmp(clients[i].name, clients[j].name))
                                    continue;
                                char *file_name = new char(strlen(clients[i].wr_name) + 1);
                                strcpy(file_name, clients[i].wr_name);
                                clients[j].rd_list.push(file_name);
                            }
                        } else
                            fwrite(clients[i].cur_case.buf, 1, clients[i].cur_case.len, clients[i].wr_fd);

                        clients[i].cur_case.mode = -1;
                    }
                } else if (FD_ISSET(clients[i].fd, &rd)) {
                    int len = recv(clients[i].fd, &clients[i].cur_case, sizeof(clients[i].cur_case), 0);
                    if (len == -1) {
                        if (errno == EAGAIN)
                            log_info(false, "[INFO] recv() not finished.\n");
                        else
                            log_info(true, "[ERROR] recv() error.\n");
                    } else if (len == 0) {
                        FD_CLR(clients[i].fd, &rd_backup);
                        FD_CLR(clients[i].fd, &wr_backup);

                        user_exit(clients[i], folders);
                        log_info(false, "[INFO] client exit.\n");
                        continue;
                    }
                }

                if (FD_ISSET(clients[i].fd, &wr)) {
                    if (clients[i].rd_fd != NULL) {
                        if (clients[i].up_package.mode == -1) {
                            clients[i].up_package.mode = 2;
                            clients[i].up_package.len = fread(clients[i].up_package.buf, 1, BUF_SIZE, clients[i].rd_fd);
                        }

                        int len = send(clients[i].fd, &clients[i].up_package, sizeof(clients[i].up_package), 0);
                        if (len == -1) {
                            if (errno == EAGAIN)
                                log_info(false, "[INFO] send() not finished.\n");
                            else
                                log_info(true, "[ERROR] send() error.\n");
                        } else if (clients[i].up_package.len == 0)  {
                            clients[i].rd_fd = NULL;

                            change_file_state(folders, clients[i].name, clients[i].rd_name, 0);
                        } else 
                            clients[i].up_package.mode = -1;
                    } else if (!clients[i].rd_list.empty()) {
                        clients[i].up_package.mode = 1;
                        clients[i].up_package.len = -1;
                        strcpy(clients[i].up_package.buf, clients[i].rd_list.front());

                        int len = send(clients[i].fd, &clients[i].up_package, sizeof(clients[i].up_package), 0);
                        if (len == -1) {
                            if (errno == EAGAIN)
                                log_info(false, "[INFO] send() not finished.\n");
                            else
                                log_info(true, "[ERROR] send() error.\n");
                        } else {
                            strcpy(clients[i].rd_name, clients[i].rd_list.front());
                            delete clients[i].rd_list.front();
                            clients[i].rd_list.pop();

                            strcpy(path, clients[i].name);
                            strcat(path, "/");
                            strcat(path, clients[i].rd_name);
                            clients[i].rd_fd = fopen(path, "rb");

                            change_file_state(folders, clients[i].name, clients[i].rd_name, 2);
                            clients[i].up_package.mode = -1;
                        }
                    }
                }
            }

            clients.erase(remove_if(clients.begin(), clients.end(), is_user_exit), clients.end());
        }
    }

    return 0;
}
