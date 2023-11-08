//
// Created by 10416 on 2023/3/7.
//
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <functional>
#include <unistd.h>
#include <unistd.h>
#include <fcntl.h>
#include "quiche.h"
#define DEST_PORT 8000
#define DSET_IP_ADDRESS  "10.10.1.3"


struct conn_io {

    int sock;

    struct sockaddr_storage local_addr;
    socklen_t local_addr_len;

    quiche_conn *conn;
};

int RecvServerData(int sock_fd, struct sockaddr_in addrServer){
    char recv_buf[20];
    int recv_num;
    int len = sizeof(addrServer);
    while(1){
        memset(recv_buf, 20, 0);
        recv_num = recvfrom(sock_fd, recv_buf, sizeof(recv_buf), 0, (struct sockaddr *)&addrServer, (socklen_t *)&len);

        if(recv_num < 0)
        {
            perror("recvfrom error:");
            break;
        }

        recv_buf[recv_num] = '\0';
        printf("client receive %d bytes: %s\n", recv_num, recv_buf);
    }
    return 0;
}

int main()
{
    /* socket文件描述符 */
    int sock_fd;

    /* 建立udp socket */
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock_fd < 0)
    {
        perror("socket");
        exit(1);
    }

    if (fcntl(sock_fd, F_SETFL, O_NONBLOCK) != 0) {
        perror("failed to make socket non-blocking");
        return -1;
    }

    /* 设置address */
    struct sockaddr_in addr_serv;
    int len;
    memset(&addr_serv, 0, sizeof(addr_serv));
    addr_serv.sin_family = AF_INET;
    addr_serv.sin_addr.s_addr = inet_addr(DSET_IP_ADDRESS);
    addr_serv.sin_port = htons(DEST_PORT);
    len = sizeof(addr_serv);


    struct conn_io *conn_io = malloc(sizeof(*conn_io));

    if (conn_io == NULL) {
        fprintf(stderr, "failed to allocate connection IO\n");
        return -1;
    }

    quiche_config *config = quiche_config_new();
    if (config == NULL) {
        fprintf(stderr, "failed to create config\n");
        return -1;
    }

    conn_io->local_addr_len = sizeof(conn_io->local_addr);
    if (getsockname(sock_fd, (struct sockaddr *)&conn_io->local_addr,
                    &conn_io->local_addr_len) != 0)
    {
        perror("failed to get local address of socket");
        return -1;
    };

    quiche_conn *conn = quiche_connect((struct sockaddr *) &conn_io->local_addr,
                                       conn_io->local_addr_len,
                        (struct sockaddr *)&addr_serv,(socklen_t)len,config);


    int send_num;

    char send_buf[20] = "hey, who are you?";
    std::thread th1(std::bind(&RecvServerData, std::ref(sock_fd),std::ref(addr_serv)));
    th1.detach();

    int counter = 0;
    while(1){
        printf("client send: %s\n", send_buf);

        send_num = sendto(sock_fd, send_buf, strlen(send_buf), 0, (struct sockaddr *)&addr_serv, len);

        if(send_num < 0)
        {
            perror("sendto error:");
            exit(1);
        }
        counter++;
        if(counter == 100){
            break;
        }
    }


    

    close(sock_fd);

    return 0;
}
