/**
 * Copyright (c) 2018-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "gloo/context.h"
#include "gloo/transport/unbound_buffer.h"
#include "gloo/tool.h"

namespace gloo {

namespace detail {

struct AllreduceOptionsImpl {
  // This type describes the function to use for element wise reduction.
  //
  // Its arguments are:
  //   1. non-const output pointer
  //   2. const input pointer 1 (may be equal to 1)
  //   3. const input pointer 2 (may be equal to 1)
  //   4. number of elements to reduce.
  //
  // Note that this function is not strictly typed and takes void pointers.
  // This is specifically done to avoid the need for a templated options class
  // and templated algorithm implementations. We found this adds very little
  // value for the increase in compilation time and code size.
  //
  using Func = std::function<void(void*, const void*, const void*, size_t)>;

  enum Algorithm {
    UNSPECIFIED = 0,
    RING = 1,
    BCUBE = 2,
  };

  explicit AllreduceOptionsImpl(const std::shared_ptr<Context>& context)
      : context(context),
        timeout(context->getTimeout()),
        algorithm(UNSPECIFIED) {}

  std::shared_ptr<Context> context;

  // End-to-end timeout for this operation.
  std::chrono::milliseconds timeout;

  // Algorithm selection.
  Algorithm algorithm;

  // Input and output buffers.
  // The output is used as input if input is not specified.
  std::vector<std::unique_ptr<transport::UnboundBuffer>> in;
  std::vector<std::unique_ptr<transport::UnboundBuffer>> out;

  // Number of elements.
  size_t elements = 0;

  // Number of bytes per element.
  size_t elementSize = 0;

  // Reduction function.
  Func reduce;

  // Tag for this operation.
  // Must be unique across operations executing in parallel.
  uint32_t tag = 0;

  // This is the maximum size of each I/O operation (send/recv) of which
  // two are in flight at all times. A smaller value leads to more
  // overhead and a larger value leads to poor cache behavior.
  static constexpr size_t kMaxSegmentSize = 2 * 1024 * 1024;

  // Internal use only. This is used to exercise code paths where we
  // have more than 2 segments per rank without making the tests slow
  // (because they would require millions of elements if the default
  // were not configurable).
  size_t maxSegmentSize = kMaxSegmentSize;
};

} // namespace detail

class AllreduceOptions {
 public:
  using Func = detail::AllreduceOptionsImpl::Func;
  using Algorithm = detail::AllreduceOptionsImpl::Algorithm;

  explicit AllreduceOptions(const std::shared_ptr<Context>& context)
      : impl_(context) {}

  void setAlgorithm(Algorithm algorithm) {
    impl_.algorithm = algorithm;
  }

  template <typename T>
  void setInput(std::unique_ptr<transport::UnboundBuffer> buf) {
    std::vector<std::unique_ptr<transport::UnboundBuffer>> bufs(1);
    bufs[0] = std::move(buf);
    setInputs<T>(std::move(bufs));
  }

  template <typename T>
  void setInputs(std::vector<std::unique_ptr<transport::UnboundBuffer>> bufs) {
    impl_.elements = bufs[0]->size / sizeof(T);
    impl_.elementSize = sizeof(T);
    impl_.in = std::move(bufs);
  }

  template <typename T>
  void setInput(T* ptr, size_t elements) {
    setInputs(&ptr, 1, elements);
  }

  template <typename T>
  void setInputs(std::vector<T*> ptrs, size_t elements) {
    setInputs(ptrs.data(), ptrs.size(), elements);
  }

  template <typename T>
  void setInputs(T** ptrs, size_t len, size_t elements) {
    impl_.elements = elements;
    impl_.elementSize = sizeof(T);
    impl_.in.reserve(len);
    for (size_t i = 0; i < len; i++) {
      impl_.in.push_back(
          impl_.context->createUnboundBuffer(ptrs[i], elements * sizeof(T)));
    }
  }

  template <typename T>
  void setOutput(std::unique_ptr<transport::UnboundBuffer> buf) {
    std::vector<std::unique_ptr<transport::UnboundBuffer>> bufs(1);
    bufs[0] = std::move(buf);
    setOutputs<T>(std::move(bufs));
  }

  template <typename T>
  void setOutputs(std::vector<std::unique_ptr<transport::UnboundBuffer>> bufs) {
    impl_.elements = bufs[0]->size / sizeof(T);
    impl_.elementSize = sizeof(T);
    impl_.out = std::move(bufs);
  }

  template <typename T>
  void setOutput(T* ptr, size_t elements) {
    setOutputs(&ptr, 1, elements);
  }

  template <typename T>
  void setOutputs(std::vector<T*> ptrs, size_t elements) {
    setOutputs(ptrs.data(), ptrs.size(), elements);
  }

  template <typename T>
  void setOutputs(T** ptrs, size_t len, size_t elements) {
    impl_.elements = elements;
    impl_.elementSize = sizeof(T);
    impl_.out.reserve(len);
    for (size_t i = 0; i < len; i++) {
      impl_.out.push_back(
          impl_.context->createUnboundBuffer(ptrs[i], elements * sizeof(T)));
    }
  }

  void setReduceFunction(Func fn) {
    impl_.reduce = fn;
  }

  void setTag(uint32_t tag) {
    impl_.tag = tag;
  }

  void setMaxSegmentSize(size_t maxSegmentSize) {
    impl_.maxSegmentSize = maxSegmentSize;
  }

  void setTimeout(std::chrono::milliseconds timeout) {
    impl_.timeout = timeout;
  }

 protected:
  detail::AllreduceOptionsImpl impl_;

  friend void allreduce(const AllreduceOptions&, const std::vector<uint64_t> &topkbitmap);
};

void allreduce(const AllreduceOptions& opts, const std::vector<uint64_t> &topkbitmap = {});

template<typename T>
class VectorGroupWithIndex {
public:
    VectorGroupWithIndex() : index_(0) {}
    explicit VectorGroupWithIndex(size_t n) : groups_(n), index_(0) {}

    void set_index(size_t idx) {
        assert(idx < groups_.size());
        index_ = idx;
    }
    size_t index() const { return index_; }
    void next() {
        if (!groups_.empty())
            index_ = (index_ + 1) % groups_.size();
    }
    dmludp::Span<T> current_vector() {
      assert(index_ < groups_.size());
      return dmludp::Span<T>(groups_[index_]);
    }
    dmludp::Span<const T> current_vector() const {
      assert(index_ < groups_.size());
      return dmludp::Span<const T>(groups_[index_]);
    }
    dmludp::Span<T> current_span(size_t begin, size_t end) {
      assert(index_ < groups_.size());
      assert(begin <= end && end <= groups_[index_].size());
      return dmludp::Span<T>(groups_[index_].data() + begin, end - begin);
    }
    dmludp::Span<const T> current_span(size_t begin, size_t end) const {
      assert(index_ < groups_.size());
      assert(begin <= end && end <= groups_[index_].size());
      return dmludp::Span<const T>(groups_[index_].data() + begin, end - begin);
    }

    void add_or_replace_by_length(const std::vector<T>& v) {
      if (groups_.empty()) {
        groups_.push_back(v);
        return;
      }

      for (auto& group : groups_) {
          if (group.size() == v.size()) {
              group = v; 
              return;
          }
      }
      groups_.push_back(v);
    }

    void add_or_replace_by_length(std::vector<T>&& v) {
      if (groups_.empty()){
        groups_.push_back(std::move(v));
        return;
      }

      for (auto& group : groups_) {
          if (group.size() == v.size()) {
              group = std::move(v); 
              return;
          }
      }
      groups_.push_back(std::move(v));
    }

    void add_or_replace_by_tag(std::vector<T>&& v, size_t segmentBytes, size_t tag) {
      if (tag == 4) {
        groups_.push_back(std::move(v));
        segmentBytesvec.push_back(segmentBytes);
        return;
      } else {
        if (tag == 7) {
          groups_.clear();
          groups_.push_back(std::move(v));
          segmentBytesvec.push_back(segmentBytes);
          return;
        } else if (tag > 7 && tag < 12) {
          groups_.push_back(std::move(v));
          segmentBytesvec.push_back(segmentBytes);
          return;
        } else {
          auto index = (tag - 7) % 5;
          groups_[index] = std::move(v); 
          segmentBytesvec[index] = segmentBytes;
        }
      }
    }

    void add_or_replace_by_tag(const std::vector<T>& v, size_t segmentBytes, size_t tag) {
      if (tag == 4) {
        groups_.push_back(std::move(v));
        segmentBytesvec.push_back(segmentBytes);
        return;
      } else {
        if (tag == 7) {
          groups_.clear();
          groups_.push_back(std::move(v));
          segmentBytesvec.push_back(segmentBytes);
          return;
        } else if (tag > 7 && tag < 12) {
          groups_.push_back(std::move(v));
          segmentBytesvec.push_back(segmentBytes);
          return;
        } else {
          auto index = (tag - 7) % 5;
          groups_[index] = std::move(v); 
          segmentBytesvec[index] = segmentBytes;
        }
      }
    }

    // void update_index() {
    //   ++index_;
    // }
    void update_index(size_t tag) {
      if (tag == 4){
        index_ = 0;
      } else {
        index_ = (tag - 7) % 5;
      }
    }

    void update_segmentBytes(const size_t segmentBytes){
      superblock_bytes = segmentBytes;
    }

    dmludp::Span<const uint64_t> partialSpan(size_t beginOffset, size_t bytes) const {
      int64_t index = -1;
      size_t bitmapID = 0;
      size_t block_per_SuperBlock = 0;
      for (auto i = 0; i < segmentBytesvec.size(); i++){
        if(beginOffset % segmentBytesvec[i] == 0) {
          bitmapID = beginOffset / segmentBytesvec[i];
          block_per_SuperBlock = (segmentBytesvec[i] + 1440 * sizeof(uint64_t) - 1) / (1440 * sizeof(uint64_t));
          index = i;
          break;
        }
      }
      if (index == -1){
        std::cerr << "partialSpan: error" << std::endl;
        _Exit(0);
      }
      size_t offset = block_per_SuperBlock * bitmapID;
      return dmludp::Span<const uint64_t>(groups_[index].data() + offset, block_per_SuperBlock);
    }
    
    void replace_vector(size_t idx, std::vector<T>&& vec) {
        assert(idx < groups_.size());
        groups_[idx] = std::move(vec);
    }
    void replace_vector(size_t idx, const std::vector<T>& vec) {
        assert(idx < groups_.size());
        groups_[idx] = vec;
    }
    std::vector<std::vector<T>>& data() { return groups_; }
    const std::vector<std::vector<T>>& data() const { return groups_; }
    size_t group_count() const { return groups_.size(); }

private:
    std::vector<std::vector<T>> groups_;
    std::vector<uint64_t> segmentBytesvec;
    uint64_t index_ = std::numeric_limits<uint64_t>::max();
    size_t superblock_bytes = 0;
};

extern VectorGroupWithIndex<uint64_t> global_manager;
dmludp::Span<const uint64_t> get_global_span(uint64_t offset, uint64_t len);

} // namespace gloo
