/**
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <memory>

#include <gloo/transport/dmludp/error.h>
#include <gloo/transport/dmludp/loop.h>
#include <gloo/transport/dmludp/socket.h>
#include <iostream>
namespace gloo {
namespace transport {
namespace dmludp {

// ReadValueOperation asynchronously reads a value of type T from the
// socket specified at construction. Upon completion or error, the
// callback is called. Its lifetime is coupled with completion of the
// operation, so the called doesn't need to hold on to the instance.
// It does so by storing a shared_ptr to itself (effectively a leak)
// until the event loop calls back.
template <typename T>
class ReadValueOperation final
    : public Handler,
      public std::enable_shared_from_this<ReadValueOperation<T>> {
 public:
  using callback_t =
      std::function<void(std::shared_ptr<Socket>, const Error& error, T&& t)>;

  ReadValueOperation(
      std::shared_ptr<Loop> loop,
      std::shared_ptr<Socket> socket,
      callback_t fn)
      : loop_(std::move(loop)),
        socket_(std::move(socket)),
        fn_(std::move(fn)) {}

  void run() {
    // Cannot initialize leak until after the object has been
    // constructed, because the std::make_shared initialization
    // doesn't run after construction of the underlying object.
    leak_ = this->shared_from_this();
    // Register with loop only after we've leaked the shared_ptr,
    // because we unleak it when the event loop thread calls.
    loop_->registerDescriptor(socket_->fd(), EPOLLIN | EPOLLONESHOT, this);
  }

  void handleEvents(int events) override {
    // Move leaked shared_ptr to the stack so that this object
    // destroys itself once this function returns.
    auto self = std::move(this->leak_);

    // Read T.
    // This part is not important!!!!
    auto rv = socket_->read(&t_, sizeof(t_));
    if (rv == -1) {
       fn_(socket_, SystemError("read", errno), std::move(t_));
       return;
     }

    // Check for short read (assume we can read in a single call).
    if (rv < sizeof(t_)) {
      fn_(socket_, ShortReadError(rv, sizeof(t_)), std::move(t_));
      return;
    }

     //2/7/2024
    rv = socket_->write(&t_, sizeof(t_));
    if (rv == -1) {
      return;
    }

    // // Check for short write (assume we can write in a single call).
    if (rv < sizeof(t_)) {
      return;
    }
    //

    fn_(socket_, Error::kSuccess, std::move(t_));

  }

 private:
  std::shared_ptr<Loop> loop_;
  std::shared_ptr<Socket> socket_;
  callback_t fn_;
  std::shared_ptr<ReadValueOperation<T>> leak_;

  T t_;
};

template <typename T>
void read(
    std::shared_ptr<Loop> loop,
    std::shared_ptr<Socket> socket,
    typename ReadValueOperation<T>::callback_t fn) {
  auto x = std::make_shared<ReadValueOperation<T>>(
      std::move(loop), std::move(socket), std::move(fn));
  x->run();
}

// WriteValueOperation asynchronously writes a value of type T to the
// socket specified at construction. Upon completion or error, the
// callback is called. Its lifetime is coupled with completion of the
// operation, so the called doesn't need to hold on to the instance.
// It does so by storing a shared_ptr to itself (effectively a leak)
// until the event loop calls back.
template <typename T>
class WriteValueOperation final
    : public Handler,
      public std::enable_shared_from_this<WriteValueOperation<T>> {
 public:
  using callback_t =
      std::function<void(std::shared_ptr<Socket>, const Error& error)>;

  WriteValueOperation(
      std::shared_ptr<Loop> loop,
      std::shared_ptr<Socket> socket,
      T t,
      callback_t fn)
      : loop_(std::move(loop)),
        socket_(std::move(socket)),
        fn_(std::move(fn)),
        t_(std::move(t)) {}

  void run() {
    // Cannot initialize leak until after the object has been
    // constructed, because the std::make_shared initialization
    // doesn't run after construction of the underlying object.
    leak_ = this->shared_from_this();
    // Register with loop only after we've leaked the shared_ptr,
    // because we unleak it when the event loop thread calls.
    loop_->registerDescriptor(socket_->fd(), EPOLLOUT | EPOLLONESHOT, this);
  }

  void handleEvents(int events) override {
    // Move leaked shared_ptr to the stack so that this object
    // destroys itself once this function returns.
    auto leak = std::move(this->leak_);

    // write T.
    // auto dmludp_rv = socket_->dmludp_write(&t_, sizeof(t_));
    // auto rv = socket_->write(dmludp_rv, sizeof(dmludp_rv));
    // for(;;){
    //    if(timer.out < 0){
    //    auto recv = socket_->recv(dmludp_rv);
    //    auto dmludp_recv = socket_->dmludp_recv();
    //    }
    //    else{
    //     break;
    //    }
    // }
    // fn_(socket_, Error::KSuccess);
    
    // Unrelated operation!!!
    // Write T.
    auto rv = socket_->write(&t_, sizeof(t_));
    if (rv == -1) {
      fn_(socket_, SystemError("write", errno));
      return;
    }

    // // Check for short write (assume we can write in a single call).
    if (rv < sizeof(t_)) {
      fn_(socket_, ShortWriteError(rv, sizeof(t_)));
      return;
    }

    // 2/7/2024
    for (;;){
      rv = socket_->read(&t_, sizeof(t_));
      if (rv == sizeof(t_))
        break;
    } 
    //////

    fn_(socket_, Error::kSuccess);
  }

 private:
  std::shared_ptr<Loop> loop_;
  std::shared_ptr<Socket> socket_;
  callback_t fn_;
  std::shared_ptr<WriteValueOperation<T>> leak_;

  T t_;
};

template <typename T>
void write(
    std::shared_ptr<Loop> loop,
    std::shared_ptr<Socket> socket,
    T t,
    typename WriteValueOperation<T>::callback_t fn) {
  auto x = std::make_shared<WriteValueOperation<T>>(
      std::move(loop), std::move(socket), std::move(t), std::move(fn));
  x->run();
}

} // namespace dmludp
} // namespace transport
} // namespace gloo
