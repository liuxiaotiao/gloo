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

    std::array<uint8_t, 1500> data;

    ssize_t len;

    time_t retry_time;

    bool is_deleted;

    retry_message(time_t timeout, ssize_t len, const std::array<uint8_t, 1500>& data) 
    : retry_time(timeout), data(data), len(len), is_deleted(false) {}

    bool operator<(const Task& other) const {
      return retry_time > other.retry_time;
    }

    Task(Task&& other) noexcept 
        : id(other.id), timeout(other.timeout), data(std::move(other.data)), is_deleted(other.is_deleted) {
        other.is_deleted = true;  
    }
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

  std::map<ssize_t, std::shared_ptr<retry_message>> messages;

  std::priority_queue<retry_message> messages;

  int timer_fd;

  // Update timerfd.
  void update_timerfd(time_t next_timeout) {
    struct itimerspec new_time;
    memset(&new_time, 0, sizeof(new_time));
    new_time.it_value.tv_sec = next_timeout;
    timerfd_settime(timer_fd, TFD_TIMER_ABSTIME, &new_time, NULL);
  }

  // Add new entry to the priority queue.
  void add_task(const Task& new_task) {
    bool should_update_timer = task_queue.empty() || new_task < task_queue.top();
    task_queue.push(new_task);

    if (should_update_timer) {
        update_timerfd(fd, new_task.timeout);
    }
  }

  class dmludptimer: public Handler{
    Pair* outerPtr;

    void setOuter(Pair* outer) {
        outerPtr = outer;
        outerPtr->device_->registerDescriptor(outerPtr->timer_fd, EPOLLIN, this);
    }

    void handle_timeout(int fd) {
      uint64_t num_expired;
      ::read(fd, &num_expired, sizeof(num_expired));

      time_t current_time = time(NULL);

      while (!outerPtr->task_queue.empty() && outerPtr->task_queue.top().timeout <= current_time) {
          Task task = outerPtr->task_queue.top();
          outerPtr->task_queue.pop();
      }

      if (!outerPtr->task_queue.empty()) {
          outerPtr->update_timerfd(outerPtr->task_queue.top().timeout);
      }
    }

    void handleEvents(int events){
      uint64_t expirations;
      ::read(outerPtr->timer_fd, &expirations, sizeof(expirations));

      auto it = (outerPtr->messages).begin();
      while(it != (outerPtr->messages).end()){
        struct timeval tv;
        gettimeofday(&tv, NULL);

        double now = tv.tv_sec*1000000 + tv.tv_usec;

        auto pkt_num = it->first;
        auto temp = it->second;

        double record = (temp->retry_time);
        if(record - now > 0){
            auto newone = std::make_shared<retry_message>();
            std::copy(std::begin(temp->data), std::end(temp->data), std::begin(newone->data));
            newone->retry_time = record;

            // Get the rtt from dmludp
            struct timeval result;
            gettimeofday(&result, NULL);
            double rtt = dmludp_get_rtt((outerPtr->dmludp_connection).get());
            newone->retry_time = record + rtt;
            (outerPtr->messages).insert(std::make_pair(pkt_num, newone));
            ::send(outerPtr->fd_, temp->data, temp->len, 0);
        }
        it++;
      }
    }
  }
  
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

  void handleread();

  bool write2dmludp(Op& op);

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

  std::shared_ptr<dmludp_conn> dmludp_connection;
};

} // namespace dmludp
} // namespace transport
} // namespace gloo
