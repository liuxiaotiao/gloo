#include <dmludp.h>
#include <loop.h>

namespace gloo {
namespace transport {
namespace dmludp{

struct retry_message{
    uint8_t data[1350];

    ssize_t len;

    double retry_time;
}

class dmludp_io final: public handler{
    public:
        void dmludp_io(int sock, bool server, dmludp_config* config, device* dev,
         *Pair_HandleEvent, Address)
        : sock(sock),
        is_server(server),
        config(config),
        timer(50000),
        device_(dev),
        Other_HandleEvent(Pair_HandleEvent),
        local_addr(local),
        peer_addr(peer),
        {
            conn = dmludp_create(config);
            if(is_server == true){
                conn = dmludp_conn_accept(local, sizeof(local), peer, sizeof(peer), config);
            }else{
                conn = dmludp_conn_connect(local,sizeof(local), peer, sizeof(peer), config);
            }
        }
        int sock;

        bool is_server;

        struct sockaddr_storage peer_addr;
        socklen_t peer_addr_len;

        struct sockaddr local_addr;
        socklen_t local_addr_len;

        dmludp_config config,

        dmludp_conn *conn;

        std::chrono::milliseconds timer;

        device device_;

        std::map<ssize_t, retry_message> messages;

        time_t timer_fd;

        void *Pair_HandleEvent(int);

        void registerDescriptor(int fd){
            loop_->registerDescriptor(fd, EPOLLIN | EPOLLOUT, this);
        }
        void unregisterDescriptor(int fd, Handler* h){
            loop_->unregisterDescriptor(fd, this);
        }

        void handleEvents(int events){
            char buf[1024];
            if(is_server){
               memset(buffer, 0, sizeof(buffer));
                memset(out, 0, sizeof(out));

                socklen_t peer_addr_len = sizeof(peer_addr);
                memset(&peer_addr, 0, peer_addr_len);
                ssize_t read = recvfrom(conn->sock, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&peer_addr, &addr_len);

                if (read < 0){
                    if((errno == EWOULDBLOCK)||(errno == EAGAIN)){
                        cout<<"recv would block"<<endl;
                        continue;
                    }
                }
                if(read > 0)
                {
                    if(conn = NULL){
                        // Handshake
                        conn->conn = dmludp_accept(conn->local_addr, conn->local_addr_len,(struct sockaddr *)peer, config);
                        
                        memcpy(&conn->peer_addr, peer_addr, peer_addr_len);
                        conn->peer_addr_len = peer_addr_len;

                        ssize_t written = dmludp_conn_send(conn->conn, out, sizeof(out), &send_info);
                        ssize_t sent = sendto(conn->sock, out, written, 0, (struct sockaddr *) &send_info.to,
                                send_info.to_len);
                        uint8_t type;
                        ssize_t pkt_num;
                        uint64_t rtt; 

                        dmludp_get_rtt(conn->conn, &rtt);

                        ssize_t rv = dmludp_header_info(buffer, sent, &type, &pkt_num);
                        struct retry_message *timer_message = calloc(1, sizeof(*retry_message));
                        timer_message->len = written;
                        
                        /// write duration
                        struct timeval result;
                        gettimeofday(&result, NULL);

                        double now = result.tv_sec*1000000 + result.tv_usec;
                        double retry_time = now + rtt;

                        /////
                        memcpy(timer_message->data, out, sent);

                        conn->messages.insert(pkt_num, timer_message);
                    }else{
                        uint8_t type;
                        ssize_t pkt_num;
                        quiche_recv_info recv_info = {
                            (struct sockaddr *)&peer_addr,
                            peer_addr_len,

                            conns->local_addr,
                            conns->local_addr_len,
                        };
                        
                        ////////
                        ssize_t rv = dmludp_header_info(buffer, sent, &type, &pkt_num);
                        if(type == 2){
                            dmludp_data_send(conn->conn, file);
                        }

                        conn->messages.erase(pkt_num);
                        if(type == 7){
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_sock, NULL);
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, timer_fd, NULL);
                            break;

                        }
                    }
                }else{
                    // Merge send_all, stopped, send_data
                    if((conn->messages).empty()){
                        if(!dmludp_conn_send_all(conn->conn)){
                            ssize_t sent = dmludp_send_data_fin(conn->conn, out);       
                            sendto(server_sock, out, sent, 0, (struct sockaddr*)&conn->peer_addr, conn->peer_addr_len);
                        }
                    }
                    if(!dmludp_conn_is_stop(conn->conn)){
                        ssize_t sent = dmludp_send_data(conn->conn, out);       

                        // if(sent == DMLUDP_ERR_DONE){
                        //     epoll_ctl(epoll_fd, EPOLL_CTL_DEL, server_sock, NULL);
                        // }
                        sendto(server_sock, out, sent, 0, (struct sockaddr*)&conn->peer_addr, conn->peer_addr_len);
                    }

                }
            }
            else{
                char buf[1350];
                socklen_t addr_len = sizeof(server_addr);
                ssize_t len = recvfrom(client_sock, buf, sizeof(buf), 0, (struct sockaddr *)&server_addr, &addr_len);
                if (len == -1) {
                    perror("recvfrom");
                    continue;
                }

                dmludp_recv_info recv_info ={
                    (struct sockaddr *) &peer_addr,
                    peer_addr_len,

                    (struct sockaddr *) conn->local_addr,
                    conn->local_addr_len,
                };
                ssize_t recivec = dmludp_conn_recv(conn->conn, buf, len, &recv_info);
                uint8_t type;
                ssize_t pkt_num;
                ssize_t rv = dmludp_header_info(buf, recivec, &type, &pkt_num);

                if(type == 4){
                    ssize_t sent = conn.send_data(conn->conn, buf, sizeof(buf), send_info);
                    sendto(client_sock, response, strlen(response), 0, (struct sockaddr *)&server_addr, addr_len);
                }

            }
            }

            if(dmludp_conn->is_empty()){
                device_->registerDescriptor(fd_, EPOLLIN, Other_HandleEvent);
            }

        }
    }


class dmludp_timer_fd:final: public handler{
    public:
        void dmludp_timer_fd(int timer_fd, dmludp_io* dio):
        dio(dio)
        timer_fd(timer_fd){
            conn = dio->conn;
        }
        dmludp_io *dio;
        dmludp_conn *conn;
        int timer_fd;

        void handleEvents(int events){
            auto it = (conn->messages).begin();
            while(it != (conn->messages).end()){
                struct timeval tv;
                gettimeofday(&tv, NULL);

                double now = tv.tv_sec*1000000 + tv.tv_usec;

                auto pkt_num = it->first;
                auto temp = it->second;

                double record = (temp->retry_time);
                if(record - now > 0){
                    auto newone = std::make_unique<message>();
                    newone->pkt_num = temp->pkt_num;
                    newone->data = temp->data;
                    newone->retry_time = record;

                    // Get the rtt from dmludp
                    struct timeval result;
                    gettimeofday(&result, NULL);
                    uint64_t rtt;
                    dmludp_get_rtt(conn->conn, &rtt);
                    newone->retry_time = record + rtt;
                    (conn->messages).insert(newone);
                    sendto(conn->sock, temp.data, temp.len, 0, (struct sockaddr*)&conn->peer_addr, conn->peer_addr_len)
                }
                it++;
            }
        }


}
}
}

