/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <netinet/in.h>
#include <sys/socket.h>

#include <chrono>
#include <memory>

#include <gloo/transport/dmludp/address.h>
#include <gloo/dmludp.h>

namespace gloo {
namespace transport {
namespace dmludp {

class Socket final : public std::enable_shared_from_this<Socket> {
 public:
  static std::shared_ptr<Socket> createForFamily(sa_family_t ai_family);
  // static std::shared_ptr<Socket> createForFamily(struct sockaddr_storage ai_addr);

  explicit Socket(int fd);

  ~Socket();

  // Return underlying file descriptor.
  int fd() const {
    return fd_;
  }

  // Release underlying file descriptor.
  int release() {
    auto fd = fd_;
    fd_ = -1;
    return fd;
  }

  // Enable or disable SO_REUSEADDR socket option.
  void reuseAddr(bool on);

  // UDP doesn't have NO_DELAY option.
  // Enable or disable TCP_NODELAY socket option.
  // void noDelay(bool on);

  // Configure if the socket is blocking or not.
  void block(bool on);

  // Configure recv timeout.
  void recvTimeout(std::chrono::milliseconds timeout);

  // Configure send timeout.
  void sendTimeout(std::chrono::milliseconds timeout);

  // Bind socket to address.
  void bind(const sockaddr_storage& ss);

  // Bind socket to address.
  void bind(const struct sockaddr* addr, socklen_t addrlen);

  // Listen on socket.
  void listen(int backlog);

  // Accept new socket connecting to listening socket.
  std::shared_ptr<Socket> accept();

  void connect(const sockaddr_storage& ss);

  // Connect to address.
  void connect_dmludp(const sockaddr_storage& ss);

  // Connect to address.
  void connect(const struct sockaddr* addr, socklen_t addrlen);

  // Proxy to read(2) with EINTR retry.
  ssize_t read(void* buf, size_t count);

  // Proxy to write(2) with EINTR retry.
  ssize_t write(const void* buf, size_t count);

  // Return address for getsockname(2).
  Address sockName() const;

  // Return address for getpeername(2).
  Address peerName() const;

  void localSockAddrStorage(const sockaddr_storage& ss);

  Connection* getConnection();

  Connection* dmludp_conn_connect(struct sockaddr_storage local, struct sockaddr_storage peer);

  Connection* dmludp_conn_accept(struct sockaddr_storage local, struct sockaddr_storage peer);

  Connection* create_dmludp_connection(struct sockaddr_storage local, struct sockaddr_storage peer, 
                          bool is_server);

  // Maybe become unique_ptr
  Connection* dmludp_connection;

  struct sockaddr_storage peer;

  struct sockaddr_storage local;

  sa_family_t local_family;

  bool new_socket;
 private:
  int fd_;

  // Configure send or recv timeout.
  void configureTimeout(int opt, std::chrono::milliseconds timeout);
};

} // namespace dmludp
} // namespace transport
} // namespace gloo
