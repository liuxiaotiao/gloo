/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gloo/transport/dmludp/socket.h>

#include <fcntl.h>
///Considering rewriting to udp.h
#include <netinet/udp.h>
#include <string.h>
#include <unistd.h>

#include <gloo/common/logging.h>

#include <gloo/dmludp.h>

namespace gloo {
namespace transport {
namespace dmludp {


std::shared_ptr<Socket> Socket::createForFamily(sa_family_t ai_family) {
  auto rv = socket(ai_family, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  GLOO_ENFORCE_NE(rv, -1, "socket: ", strerror(errno));
  return std::make_shared<Socket>(rv);
}

void Socket::localSockAddrStorage(const sockaddr_storage& ss){
  local = ss;
}
 
Socket::Socket(int fd) : fd_(fd) {
  new_socket = false;
}

Socket::~Socket() {
  if (fd_ >= 0) {
    ::close(fd_);
  }
}

void Socket::reuseAddr(bool on) {
  int value = on ? 1 : 0;
  auto rv = ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));
  GLOO_ENFORCE_NE(rv, -1, "setsockopt: ", strerror(errno));
}


void Socket::block(bool on) {
  auto rv = fcntl(fd_, F_GETFL);
  GLOO_ENFORCE_NE(rv, -1, "fcntl: ", strerror(errno));
  if (!on) {
    // Set O_NONBLOCK
    rv |= O_NONBLOCK;
  } else {
    // Clear O_NONBLOCK
    rv &= ~O_NONBLOCK;
  }
  rv = fcntl(fd_, F_SETFL, rv);
  GLOO_ENFORCE_NE(rv, -1, "fcntl: ", strerror(errno));
}

void Socket::configureTimeout(int opt, std::chrono::milliseconds timeout) {
  struct timeval tv = {
      .tv_sec = timeout.count() / 1000,
      .tv_usec = (timeout.count() % 1000) * 1000,
  };
  auto rv = setsockopt(fd_, SOL_SOCKET, opt, &tv, sizeof(tv));
  GLOO_ENFORCE_NE(rv, -1, "setsockopt: ", strerror(errno));
}

void Socket::recvTimeout(std::chrono::milliseconds timeout) {
  configureTimeout(SO_RCVTIMEO, std::move(timeout));
}

void Socket::sendTimeout(std::chrono::milliseconds timeout) {
  configureTimeout(SO_SNDTIMEO, std::move(timeout));
}

void Socket::bind(const sockaddr_storage& ss) {
  if (ss.ss_family == AF_INET) {
    const struct sockaddr_in* sa = (const struct sockaddr_in*)&ss;
    bind((const struct sockaddr*)sa, sizeof(*sa));
    return;
  }
  if (ss.ss_family == AF_INET6) {
    const struct sockaddr_in6* sa = (const struct sockaddr_in6*)&ss;
    bind((const struct sockaddr*)sa, sizeof(*sa));
    return;
  }
  GLOO_ENFORCE(false, "Unknown address family: ", ss.ss_family);
}

void Socket::bind(const struct sockaddr* addr, socklen_t addrlen) {
  auto rv = ::bind(fd_, addr, addrlen);
  GLOO_ENFORCE_NE(rv, -1, "bind: ", strerror(errno));
}

// listen() and accept() will be used in the listener Handler.
void Socket::listen(int backlog) {
  static uint8_t buf[65535];
  struct sockaddr_storage peer_addr;
  socklen_t peer_addr_len = sizeof(peer_addr);
  new_socket = false;
  int rv = 0;
  rv = ::recvfrom(fd_, buf, sizeof(buf), 0, (struct sockaddr *) &peer_addr, &peer_addr_len);
  if(rv > -1){
    int type;
    int pkt_num;
    auto header = dmludp_header_info(buf, 26, type, pkt_num);
    if (header == 2){
      peer = std::move(peer_addr);
      new_socket = true;
    }
  }
  // GLOO_ENFORCE_NE(rv, -1, "listen: ", strerror(errno));
}

std::shared_ptr<Socket> Socket::accept() {
  struct sockaddr_storage addr;
  socklen_t addrlen = sizeof(addr);
  if(new_socket){
    auto rv = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    auto connection = dmludp_conn_accept(local, peer);
    auto accept_socket = std::make_shared<Socket>(rv);
    accept_socket->dmludp_connection = connection;

    // Bind random port in local and remote node.
    sockaddr_storage storage;
    memset(&storage, 0, sizeof(storage));
    // sockaddr_in* addr = (struct sockaddr_in*)&storage;
    // addr->sin_family = AF_INET;
    // addr->sin_addr.s_addr = htonl(INADDR_ANY);
    // addr->sin_port = htons(0); 
    if ((&local)->ss_family == AF_INET) {
      // IPv4
      const sockaddr_in* src_addr = reinterpret_cast<const sockaddr_in*>(&local);
      sockaddr_in* dest_addr = reinterpret_cast<sockaddr_in*>(&storage);
      dest_addr->sin_family = AF_INET;
      memcpy(&dest_addr->sin_addr, &src_addr->sin_addr, sizeof(in_addr));
      dest_addr->sin_port = htons(0); 
    } else if ((&local)->ss_family == AF_INET6) {
      // IPv6
      const sockaddr_in6* src_addr = reinterpret_cast<const sockaddr_in6*>(&local);
      sockaddr_in6* dest_addr = reinterpret_cast<sockaddr_in6*>(&storage);
      dest_addr->sin6_family = AF_INET6;
      memcpy(&dest_addr->sin6_addr, &src_addr->sin6_addr, sizeof(in6_addr));
      dest_addr->sin6_port = htons(0); 
    }

    accept_socket->bind(storage);

    accept_socket->connect(peer);

    uint8_t out[1500];
    uint8_t buffer[1500];
    ssize_t written = dmludp_conn_send(connection, out, sizeof(out));
    // auto start = std::chrono::high_resolution_clock::now();
    ssize_t sent = accept_socket->write(out, written);

    // struct sockaddr *tmp_local;
    // // struct sockaddr *local_addr tmp_peer;
    // struct sockaddr_storage tmp_peer_addr;
    // struct sockaddr_storage *local_addr = &local;
    for (;;){
      ssize_t received = accept_socket->read(buffer, 1500);
      if(received < 1){
        continue;
      }
      ssize_t dmludp_recv = dmludp_conn_recv(connection, buffer, received);
      // auto end = std::chrono::high_resolution_clock::now();
      // auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
      // dmludp_set_rtt(connection, duration.count());
      new_socket = false;
      break;
    }
    return accept_socket;
  }else{
    return std::shared_ptr<Socket>();
  }
  // for (;;) {
  //   rv = ::accept(fd_, (struct sockaddr*)&addr, &addrlen);
  //   if (rv == -1) {
  //     if (errno == EINTR) {
  //       continue;
  //     }
  //     // Return empty shared_ptr to indicate failure.
  //     // The caller can assume errno has been set.
  //     return std::shared_ptr<Socket>();
  //   }
  //   new_socket = false;
  //   break;
  // }
  // return std::make_shared<Socket>(rv);
}

std::shared_ptr<Connection> Socket::dmludp_conn_connect(struct sockaddr_storage local, struct sockaddr_storage peer){
  return create_dmludp_connection(local, peer, false);
}

// Call dmludp_conn_accept after accpect called
std::shared_ptr<Connection> Socket::dmludp_conn_accept(struct sockaddr_storage local, struct sockaddr_storage peer){
  return create_dmludp_connection(local, peer, true);
}

// Connection* Socket::create_dmludp_connection(struct sockaddr_storage local, struct sockaddr_storage peer, bool is_server){
std::shared_ptr<Connection> Socket::create_dmludp_connection(struct sockaddr_storage local, struct sockaddr_storage peer, bool is_server){
  auto dmludp_config = dmludp_config_new();
  struct sockaddr_in addr;
  socklen_t len = sizeof(addr);
  if( is_server ){
    auto connection = dmludp_accept(local, peer, *dmludp_config);
    dmludp_config_free(dmludp_config);
    return connection;
  }else{
    auto connection = dmludp_connect(local, peer, *dmludp_config);
    dmludp_config_free(dmludp_config);
    return connection;
  }
}

void Socket::connect(const sockaddr_storage& ss) {
  if (ss.ss_family == AF_INET) {
    const struct sockaddr_in* sa = (const struct sockaddr_in*)&ss;
    return connect((const struct sockaddr*)sa, sizeof(*sa));
  }
  if (ss.ss_family == AF_INET6) {
    const struct sockaddr_in6* sa = (const struct sockaddr_in6*)&ss;
    return connect((const struct sockaddr*)sa, sizeof(*sa));
  }
  GLOO_ENFORCE(false, "Unknown address family: ", ss.ss_family);
}

void Socket::connect_dmludp(const sockaddr_storage& ss) {
  struct sockaddr *tmp_local;
  struct sockaddr_storage tmp_peer_addr;
  socklen_t peer_addr_len = sizeof(tmp_peer_addr);
  uint8_t out[1500];
  uint8_t buffer[1500];

  auto temp_connection = dmludp_conn_connect(local, ss);

  ssize_t written = dmludp_send_data_handshake(temp_connection, out, sizeof(out));
  auto start = std::chrono::high_resolution_clock::now();
  ssize_t sent = sendto(fd_, out, written, 0, (struct sockaddr *) &ss, peer_addr_len);

  struct sockaddr_in tmp_addr;
  memset(&tmp_addr, 0, sizeof(tmp_addr));
  tmp_addr.sin_family = AF_UNSPEC;

  for (;;){
    ssize_t received = recvfrom(fd_, buffer, sizeof(buffer), 0, (struct sockaddr *) &tmp_peer_addr, &peer_addr_len);
    if(received < 1){
      continue;
    }
    if(ss.ss_family != tmp_peer_addr.ss_family){
      continue;
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    if (tmp_peer_addr.ss_family == AF_INET) { // IPv4
        struct sockaddr_in *ipv4_addr1 = (struct sockaddr_in *)&ss;
        struct sockaddr_in *ipv4_addr2 = (struct sockaddr_in *)&tmp_peer_addr;
        if(ipv4_addr1->sin_addr.s_addr != ipv4_addr2->sin_addr.s_addr){
          continue;
        }
    } else if (tmp_peer_addr.ss_family == AF_INET6) { // IPv6
        struct sockaddr_in6 *ipv6_addr1 = (struct sockaddr_in6 *)&ss;
        struct sockaddr_in6 *ipv6_addr2 = (struct sockaddr_in6 *)&tmp_peer_addr;
        if(memcmp(&ipv6_addr1->sin6_addr, &ipv6_addr2->sin6_addr, sizeof(struct in6_addr)) != 0){
          continue;
        }
    }
    int type = 0;
    int pktnum = 0;
    auto header = dmludp_header_info(buffer, 26, type, pktnum);
    if(header != 2){
      continue;
    }
    peer = tmp_peer_addr;
    connect(tmp_peer_addr);
    auto connection = dmludp_conn_connect(local, peer);
    dmludp_connection = connection;
    dmludp_set_rtt(dmludp_connection, duration.count());
    ssize_t dmludp_recv = dmludp_conn_recv(dmludp_connection, buffer, received);
    written = dmludp_conn_send(dmludp_connection, out, sizeof(out));
    sent = write(out, written);
    new_socket = false;
    break;
  }
}

void Socket::connect(const struct sockaddr* addr, socklen_t addrlen) {
  for (;;) {
    auto rv = ::connect(fd_, addr, addrlen);
    if (rv == -1) {
      if (errno == EINTR) {
        continue;
      }
      if (errno != EINPROGRESS) {
        GLOO_ENFORCE_NE(rv, -1, "connect: ", strerror(errno));
      }
    }
    break;
  }
}

ssize_t Socket::read(void* buf, size_t count) {
  ssize_t rv = -1;
  for (;;) {
    rv = ::read(fd_, buf, count);
    if (rv == -1 && errno == EINTR) {
      continue;
    }
    break;
  }
  return rv;
}

ssize_t Socket::write(const void* buf, size_t count) {
  ssize_t rv = -1;
  for (;;) {
    rv = ::write(fd_, buf, count);
    if (rv == -1 && errno == EINTR) {
      continue;
    }
    break;
  }
  return rv;
}

Address Socket::sockName() const {
  return Address::fromSockName(fd_);
}

Address Socket::peerName() const {
  return Address::fromPeerName(fd_);
}

// Connection* Socket::getConnection(){
std::shared_ptr<Connection> Socket::getConnection(){
  return dmludp_connection;
}

} // namespace dmludp
} // namespace transport
} // namespace gloo
