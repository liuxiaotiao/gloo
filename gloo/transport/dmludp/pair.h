/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <list>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <sys/socket.h>
#include <sys/uio.h>
#include <gloo/dmludp.h>
#include <sys/timerfd.h>

#include "gloo/common/error.h"
#include "gloo/common/memory.h"
#include "gloo/transport/pair.h"
#include "gloo/transport/dmludp/address.h"
#include "gloo/transport/dmludp/device.h"
#include "gloo/transport/dmludp/error.h"
#include "gloo/transport/dmludp/socket.h"

namespace gloo {
namespace transport {
namespace dmludp {

// Forward declaration
class Buffer;

// Forward declaration
class Context;

// Forward declaration
class UnboundBuffer;

// Sufficiently large timeout (of 100 hours) to prevent overflow
constexpr auto kLargeTimeDuration = std::chrono::hours(100);

struct retry_message{
    // uint8_t data[1500];
    int pkt_num;

    std::array<uint8_t, 1500> data;

    ssize_t len;

    std::chrono::steady_clock::time_point retry_time;
};

struct Op {
  enum Opcode {
    SEND_BUFFER = 0,
    SEND_UNBOUND_BUFFER = 1,
    NOTIFY_SEND_READY = 2,
    NOTIFY_RECV_READY = 3,
  };

  inline enum Opcode getOpcode() {
    return static_cast<Opcode>(preamble.opcode);
  }

  struct {
    size_t nbytes = 0;
    size_t opcode = 0;
    size_t slot = 0;
    size_t offset = 0;
    size_t length = 0;
    size_t roffset = 0;
  } preamble;

  // Used internally
  Buffer* buf = nullptr;
  WeakNonOwningPtr<UnboundBuffer> ubuf;
  size_t nread = 0;
  size_t nwritten = 0;

  // Byte offset to read from/write to and byte count.
  size_t offset = 0;
  size_t nbytes = 0;
};

class Pair : public ::gloo::transport::Pair, public Handler {
 protected:
  enum state {
    INITIALIZING = 1,
    CONNECTING = 3,
    CONNECTED = 4,
    CLOSED = 5,
  };

 public:
  explicit Pair(
      Context* context,
      Device* device,
      int rank,
      std::chrono::milliseconds timeout);

  virtual ~Pair();

  Pair(const Pair& that) = delete;

  Pair& operator=(const Pair& that) = delete;

  virtual const Address& address() const override;

  virtual void connect(const std::vector<char>& bytes) override;

  virtual void setSync(bool sync, bool busyPoll) override;

  virtual std::unique_ptr<::gloo::transport::Buffer> createSendBuffer(
      int slot,
      void* ptr,
      size_t size) override;

  virtual std::unique_ptr<::gloo::transport::Buffer> createRecvBuffer(
      int slot,
      void* ptr,
      size_t size) override;

  // Send from the specified buffer to remote side of pair.
  virtual void send(
      transport::UnboundBuffer* tbuf,
      uint64_t tag,
      size_t offset,
      size_t nbytes) override;

  // Receive into the specified buffer from the remote side of pair.
  virtual void recv(
      transport::UnboundBuffer* tbuf,
      uint64_t tag,
      size_t offset,
      size_t nbytes) override;

  // Attempt to receive into the specified buffer from the remote side
  // of pair. Returns true if there was a remote pending send and the
  // recv is in progress, false otherwise.
  bool tryRecv(
      transport::UnboundBuffer* tbuf,
      uint64_t tag,
      size_t offset,
      size_t nbytes);

  void handleEvents(int events) override;

  void close() override;

  std::map<std::chrono::steady_clock::time_point, retry_message> message;
  std::map<int, std::chrono::steady_clock::time_point> message_time;

  int timer_fd;

  void remove_retrymessage_by_pktnum(int pkt_num) {
    if (message_time.count(pkt_num) > 0 ) {
      auto time = message_time[pkt_num];
      message.erase(time);
      message_time.erase(pkt_num);
      if (!message.empty()) {
        update_timerfd(message.begin()->first);
      }else{
        struct itimerspec new_value{};
        // timerfd_settime(fd_, 0, &new_value, NULL);
        timerfd_settime(timer_fd, 0, &new_value, NULL);
      }
    }
  }

  void update_timerfd(std::chrono::steady_clock::time_point time_point) {
    auto now = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(time_point - now);

    struct itimerspec new_value{};
    new_value.it_value.tv_sec = duration.count() / 1000000000;
    new_value.it_value.tv_nsec = duration.count() % 1000000000;
    // timerfd_settime(fd_, 0, &new_value, nullptr);
    timerfd_settime(timer_fd, 0, &new_value, nullptr);

  }

  void add_message(std::chrono::steady_clock::time_point time, retry_message& new_task) {
    // if (message.empty()){
    //   // message[time] = std::move(new_task);
    //   message.emplace(time, std::move(new_task));
    //   message_time[new_task.pkt_num] = time;
    //   update_timerfd(message.begin()->first);
    // }else{
    //   // message[time] = std::move(new_task);
    //   message.emplace(time, std::move(new_task));
    //   message_time[new_task.pkt_num] = time;
    // }
    message.emplace(time, std::move(new_task));
    message_time[new_task.pkt_num] = time;
    if (message.size() == 1){
      update_timerfd(message.begin()->first);
    }else{
      if (message.begin()->first != time){
        update_timerfd(message.begin()->first);
      }
    }
  }


  class dmludptimer: public Handler{
    public:
    Pair& outerPtr;

    dmludptimer(Pair& outer) : outerPtr(outer) {}

    void handleEvents(int events){
      uint64_t expirations;
      auto timer_read = ::read(outerPtr.timer_fd, &expirations, sizeof(expirations));
      for (auto it = (outerPtr.message).begin(); it != (outerPtr.message).end(); it++){
        auto now = std::chrono::steady_clock::now();
        if (it->second.retry_time > now){
          outerPtr.update_timerfd(it->first);
          break;
        }else{
          struct retry_message retry;
          retry.pkt_num = it->second.pkt_num;
          double rtt = dmludp_get_rtt(outerPtr.dmludp_connection);
          std::chrono::steady_clock::duration duration = std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double, std::nano>(rtt));
          std::chrono::steady_clock::time_point futureTimePoint = now + duration;
          retry.retry_time = futureTimePoint;
          retry.data = it->second.data;
          retry.len = it->second.len;
          auto variable_time = ::send(outerPtr.fd_, it->second.data.data(), it->second.len, 0);
          outerPtr.add_message(futureTimePoint, retry);
        }
      }

      if (outerPtr.message.empty()){
        struct itimerspec new_value = {};
        timerfd_settime(outerPtr.timer_fd, 0, &new_value, NULL);
      }
    }
  };
  friend class dmludptimer;

  dmludptimer innertimer;

 protected:
  // Refer to parent context using raw pointer. This could be a
  // weak_ptr, seeing as the context class is a shared_ptr, but:
  // 1) That means calling std::weak_ptr::lock() everytime we need it,
  // 2) The context holds a unique_ptr to this pair, so the context
  //    pointer will be valid for the lifetime of this pair.
  Context* const context_;

  // Refer to device using raw pointer. The context owns a shared_ptr
  // to the device, and per the lifetime guarantees of the context,
  // there is no need to duplicate that shared_ptr in this class.
  Device* const device_;

  const int rank_;
  state state_;
  std::atomic<bool> sync_;
  const std::chrono::milliseconds timeout_;
  // When set, instructs pair to use busy-polling on receive.
  // Can only be used with sync receive mode.
  bool busyPoll_;
  int fd_;
  size_t sendBufferSize_;

  Address self_;
  Address peer_;

  std::mutex m_;
  std::condition_variable cv_;
  std::map<int, Buffer*> buffers_;

  // Tuple captures pointer to unbound buffer, byte offset, and byte count.
  using UnboundBufferOp =
      std::tuple<WeakNonOwningPtr<UnboundBuffer>, size_t, size_t>;

  std::unordered_map<uint64_t, std::deque<UnboundBufferOp>> localPendingSend_;
  std::unordered_map<uint64_t, std::deque<UnboundBufferOp>> localPendingRecv_;

  void sendUnboundBuffer(
      WeakNonOwningPtr<UnboundBuffer> buf,
      uint64_t slot,
      size_t offset,
      size_t nbytes);
  void sendNotifyRecvReady(uint64_t slot, size_t nbytes);
  void sendNotifySendReady(uint64_t slot, size_t nbytes);

  void connectCallback(std::shared_ptr<Socket> socket, Error error);

  Buffer* getBuffer(int slot);
  void registerBuffer(Buffer* buf);
  void unregisterBuffer(Buffer* buf);

  void sendSyncMode(Op& op);
  void sendAsyncMode(Op& op);
  void send(Op& op);
  void recv();

  const Address& peer() const {
    return peer_;
  }

  bool isSync() const {
    return sync_;
  }

  std::chrono::milliseconds getTimeout() const {
    return timeout_;
  }

  std::exception_ptr signalExceptionExternal(const std::string& msg);

  friend class Buffer;

  friend class Context;

 protected:
  // Maintain state of a single operation for receiving operations
  // from the remote side of the pair.
  Op rx_;

  // Maintain state of multiple operations for transmitting operations
  // to the remote side. To support send/recv of unbound buffers,
  // transmission of notifications may be triggered from the event
  // loop, where it is not possible to block and wait on other I/O
  // operations to complete. Instead, if transmission cannot complete
  // in place, it must be queued and executed later.
  std::deque<Op> tx_;

  // Helper function for the `write` function below.
  ssize_t prepareWrite(
      Op& op,
      const NonOwningPtr<UnboundBuffer>& buf,
      struct iovec* iov,
      int& ioc);

  // Write specified operation to socket.
  //
  // The pair mutex is expected to be held when called.
  //
  virtual bool write(Op& op);

  void writeComplete(const Op &op, NonOwningPtr<UnboundBuffer> &buf,
                     const Op::Opcode &opcode) const;

  // Helper function for the `read` function below.
  ssize_t prepareRead(
      Op& op,
      NonOwningPtr<UnboundBuffer>& buf,
      struct iovec& iov);

  // Read operation from socket into member variable (see `rx_`).
  //
  // The pair mutex is expected to be held when called.
  //
  virtual bool read();

  void readComplete(NonOwningPtr<UnboundBuffer> &buf);

  // Helper function that is called from the `read` function.
  void handleRemotePendingSend(const Op& op);

  // Helper function that is called from the `read` function.
  void handleRemotePendingRecv(const Op& op);

  // Handles read and write events after the pair moves to connected state
  // and until it moves to closed state.
  //
  // The pair mutex is expected to be held when called.
  //
  virtual void handleReadWrite(int events);

  void handlewrite();

  // bool handleread();

  void dmludp2read(struct iovec &iov, size_t buffer_len = 0);

  // bool write2dmludp(Op& op);

  bool protocal2read();

  bool protocal2send();

  // Advances this pair's state. See the `Pair::state` enum for
  // possible states. State can only move forward, i.e. from
  // initializing, to connected, to closed.
  //
  // The pair mutex is expected to be held when called.
  //
  virtual void changeState(state nextState) noexcept;

  template<typename pred_t>
  void waitUntil(pred_t pred, std::unique_lock<std::mutex>& lock,
                 bool useTimeout) {
    auto timeoutSet = timeout_ != kNoTimeout;
    if (useTimeout && timeoutSet) {
      // Use a longer timeout when waiting for initial connect

      // relTime must be small enough not to overflow when
      // added to std::chrono::steady_clock::now()
      auto relTime = std::min(
        timeout_ * 5,
        std::chrono::duration_cast<std::chrono::milliseconds>(kLargeTimeDuration));
      auto done = cv_.wait_for(lock, relTime, pred);
      if (!done) {
        signalAndThrowException(GLOO_ERROR_MSG("Connect timeout ", peer_.str()));
      }
    } else {
      cv_.wait(lock, pred);
    }
  }

  // Helper function to block execution until the pair has advanced to
  // the `CONNECTED` state. Expected to be called from `Pair::connect`.
  virtual void waitUntilConnected(
      std::unique_lock<std::mutex>& lock, bool useTimeout);

  // Helper function to assert the current state is `CONNECTED`.
  virtual void verifyConnected();

  // Throws if an exception if set.
  void throwIfException();

  // Set exception and signal all pending buffers.
  // This moves the pair to the CLOSED state which means that the
  // handleEvents function is no longer called by the device loop.
  void signalException(const std::string& msg);
  void signalException(std::exception_ptr);

  // Like signalException, but throws exception as well.
  void signalAndThrowException(const std::string& msg);
  void signalAndThrowException(std::exception_ptr ex);

  // Cache exception such that it can be rethrown if any function on
  // this instance is called again when it is in an error state.
  std::exception_ptr ex_;

  // Connection* dmludp_connection;
  std::shared_ptr<Connection> dmludp_connection;
};

} // namespace dmludp
} // namespace transport
} // namespace gloo
