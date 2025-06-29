#include <chrono>
#include <iostream>
#include <optional>
#include <atomic>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <omp.h>
#include <thread>
#include <cassert>
#include <immintrin.h>
#include "tool.h"
namespace dmludp {
    // struct BitPos {
    //     int64_t last_one = -1;
    //     int64_t last_zero = -1;
    // };

    // struct NextZeroInfo {
    //     int64_t first = -1;
    //     bool has_next = false;
    //     int64_t second = -1;
    // };

    // // ===============================
    // // 查找最后 1 和 0（标量高效版）
    // // ===============================
    // inline BitPos find_last_1_and_0_impl(std::span<const uint64_t> bits, size_t num_bits,
    //                                     size_t offset_start, size_t offset_end) {
    //     BitPos result;
    //     size_t start_word = offset_start / 64;
    //     size_t end_word = offset_end / 64;
    //     size_t start_offset = offset_start % 64;
    //     size_t end_offset = offset_end % 64;

    //     for (int64_t i = static_cast<int64_t>(end_word); i >= static_cast<int64_t>(start_word); --i) {
    //         uint64_t word = bits[i];
    //         uint64_t mask = ~0ULL;
    //         if (i == static_cast<int64_t>(start_word))
    //             mask &= (~0ULL << start_offset);
    //         if (i == static_cast<int64_t>(end_word))
    //             mask &= (1ULL << (end_offset + 1)) - 1;

    //         size_t bits_from = i * 64;
    //         if (bits_from + 64 > num_bits) {
    //             size_t valid_bits = num_bits - bits_from;
    //             mask &= (valid_bits >= 64) ? ~0ULL : ((1ULL << valid_bits) - 1);
    //         }

    //         uint64_t masked = word & mask;
    //         uint64_t masked_inv = (~word) & mask;

    //         if (masked)
    //             result.last_one = std::max(result.last_one,
    //                                     static_cast<int64_t>(i * 64 + (63 - __builtin_clzll(masked))));
    //         if (masked_inv)
    //             result.last_zero = std::max(result.last_zero,
    //                                         static_cast<int64_t>(i * 64 + (63 - __builtin_clzll(masked_inv))));
    //     }
    //     return result;
    // }

    // // ===============================
    // // AVX-512 查找下一个 bit（1 或 0）
    // // ===============================
    // inline int64_t find_next_bit_avx512(std::span<const uint64_t> bits, size_t num_bits,
    //                                     size_t offset_start, size_t offset_end, bool find_one) {
    //     const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(bits.data());
    //     size_t byte_start = offset_start / 8;
    //     size_t byte_end = offset_end / 8;

    //     for (size_t i = byte_start; i + 63 <= byte_end; i += 64) {
    //         __m512i v = _mm512_loadu_si512(reinterpret_cast<const void*>(byte_ptr + i));
    //         if (!find_one)
    //             v = _mm512_xor_si512(v, _mm512_set1_epi8(-1));

    //         __mmask64 mask = _mm512_cmpneq_epi8_mask(v, _mm512_setzero_si512());
    //         if (mask != 0) {
    //             int bit_pos = __builtin_ctzll(mask);
    //             return static_cast<int64_t>((i * 8) + bit_pos);
    //         }
    //     }

    //     // fallback scalar
    //     size_t bit_tail = std::max(byte_start, byte_end - 63) * 8;
    //     for (size_t b = bit_tail; b <= offset_end; ++b) {
    //         if (b < offset_start) continue;
    //         size_t word_idx = b / 64;
    //         size_t bit_idx = b % 64;
    //         bool bit = (bits[word_idx] >> bit_idx) & 1ULL;
    //         if (bit == find_one)
    //             return static_cast<int64_t>(b);
    //     }
    //     return -1;
    // }

    // // ===============================
    // // BitmapSpan 类
    // // ===============================
    // class BitmapSpan {
    // public:
    //     BitmapSpan()
    //     : bits_(), start_bit_(0), end_bit_(0), num_bits_(0),
    //       next_one_index_(0), next_zero_index_(0) {}

    //     BitmapSpan(std::span<const uint64_t> data, size_t start_bit, size_t end_bit)
    //         : bits_(data), start_bit_(start_bit), end_bit_(end_bit),
    //         num_bits_(end_bit >= start_bit ? end_bit - start_bit + 1 : 0),
    //         next_one_index_(start_bit), next_zero_index_(start_bit) {
    //         assert(start_bit <= end_bit);
    //     }

    //     BitPos find_last_1_and_0() const {
    //         BitPos pos = find_last_1_and_0_impl(bits_, num_bits_, 0, end_bit_ - start_bit_);
    //         if (pos.last_one != -1) pos.last_one += start_bit_;
    //         if (pos.last_zero != -1) pos.last_zero += start_bit_;
    //         return pos;
    //     }

    //     void reset_span(std::span<const uint64_t> new_bits, size_t new_start_bit, size_t new_end_bit) {
    //         assert(new_start_bit <= new_end_bit);
    //         bits_ = new_bits;
    //         start_bit_ = new_start_bit;
    //         end_bit_ = new_end_bit;
    //         num_bits_ = end_bit_ - start_bit_ + 1;
    //         next_one_index_ = start_bit_;
    //         next_zero_index_ = start_bit_;
    //     }

    //     int64_t next_one_avx512() {
    //         if (next_one_index_ > end_bit_) return -1;
    //         int64_t pos = find_next_bit_avx512(bits_, num_bits_,
    //                                         next_one_index_ - start_bit_,
    //                                         end_bit_ - start_bit_, true);
    //         if (pos != -1) {
    //             pos += start_bit_;
    //             next_one_index_ = pos + 1;
    //             return pos;
    //         }
    //         next_one_index_ = end_bit_ + 1;
    //         return -1;
    //     }

    //     int64_t next_zero_avx512() {
    //         if (next_zero_index_ > end_bit_) return -1;
    //         int64_t pos = find_next_bit_avx512(bits_, num_bits_,
    //                                         next_zero_index_ - start_bit_,
    //                                         end_bit_ - start_bit_, false);
    //         if (pos != -1) {
    //             pos += start_bit_;
    //             next_zero_index_ = pos + 1;
    //             return pos;
    //         }
    //         next_zero_index_ = end_bit_ + 1;
    //         return -1;
    //     }

    //     void reset_next_indices() {
    //         next_one_index_ = start_bit_;
    //         next_zero_index_ = start_bit_;
    //     }

    // private:
    //     std::span<const uint64_t> bits_;
    //     size_t start_bit_;
    //     size_t end_bit_;
    //     size_t num_bits_;
    //     size_t next_one_index_;
    //     size_t next_zero_index_;
    // };
    BitPos find_last_1_and_0_impl(Span<const uint64_t> bits, size_t num_bits,
                              size_t offset_start, size_t offset_end) {
        BitPos result;
        size_t start_word = offset_start / 64;
        size_t end_word = offset_end / 64;
        size_t start_offset = offset_start % 64;
        size_t end_offset = offset_end % 64;

        for (int64_t i = static_cast<int64_t>(end_word); i >= static_cast<int64_t>(start_word); --i) {
            uint64_t word = bits[i];
            uint64_t mask = ~0ULL;
            if (i == static_cast<int64_t>(start_word))
                mask &= (~0ULL << start_offset);
            if (i == static_cast<int64_t>(end_word))
                mask &= (1ULL << (end_offset + 1)) - 1;

            size_t bits_from = i * 64;
            if (bits_from + 64 > num_bits) {
                size_t valid_bits = num_bits - bits_from;
                mask &= (valid_bits >= 64) ? ~0ULL : ((1ULL << valid_bits) - 1);
            }

            uint64_t masked = word & mask;
            uint64_t masked_inv = (~word) & mask;

            if (masked)
                result.last_one = std::max(result.last_one,
                                        static_cast<int64_t>(i * 64 + (63 - __builtin_clzll(masked))));
            if (masked_inv)
                result.last_zero = std::max(result.last_zero,
                                            static_cast<int64_t>(i * 64 + (63 - __builtin_clzll(masked_inv))));
        }
        return result;
    }

    int64_t find_next_bit_avx512(Span<const uint64_t> bits, size_t num_bits,
                                size_t offset_start, size_t offset_end, bool find_one) {
        const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(bits.data());
        size_t byte_start = offset_start / 8;
        size_t byte_end = offset_end / 8;

        for (size_t i = byte_start; i + 63 <= byte_end; i += 64) {
            __m512i v = _mm512_loadu_si512(reinterpret_cast<const void*>(byte_ptr + i));
            if (!find_one)
                v = _mm512_xor_si512(v, _mm512_set1_epi8(-1));

            __mmask64 mask = _mm512_cmpneq_epi8_mask(v, _mm512_setzero_si512());
            if (mask != 0) {
                int bit_pos = __builtin_ctzll(mask);
                return static_cast<int64_t>((i * 8) + bit_pos);
            }
        }

        // fallback scalar
        size_t bit_tail = std::max(byte_start, byte_end - 63) * 8;
        for (size_t b = bit_tail; b <= offset_end; ++b) {
            if (b < offset_start) continue;
            size_t word_idx = b / 64;
            size_t bit_idx = b % 64;
            bool bit = (bits[word_idx] >> bit_idx) & 1ULL;
            if (bit == find_one)
                return static_cast<int64_t>(b);
        }
        return -1;
    }

    // BitmapSpan方法实现
    BitmapSpan::BitmapSpan()
        : bits_(), start_bit_(0), end_bit_(0), num_bits_(0),
        next_one_index_(0), next_zero_index_(0) {}

    BitmapSpan::BitmapSpan(Span<const uint64_t> data, size_t start_bit, size_t end_bit)
        : bits_(data), start_bit_(start_bit), end_bit_(end_bit),
        num_bits_(end_bit >= start_bit ? end_bit - start_bit + 1 : 0),
        next_one_index_(start_bit), next_zero_index_(start_bit) {
        assert(start_bit <= end_bit);
    }

    BitPos BitmapSpan::find_last_1_and_0() const {
        BitPos pos = find_last_1_and_0_impl(bits_, num_bits_, 0, end_bit_ - start_bit_);
        if (pos.last_one != -1) pos.last_one += start_bit_;
        if (pos.last_zero != -1) pos.last_zero += start_bit_;
        return pos;
    }

    void BitmapSpan::reset_span(Span<const uint64_t> new_bits, size_t new_start_bit, size_t new_end_bit) {
        assert(new_start_bit <= new_end_bit);
        bits_ = new_bits;
        start_bit_ = new_start_bit;
        end_bit_ = new_end_bit;
        num_bits_ = end_bit_ - start_bit_ + 1;
        next_one_index_ = start_bit_;
        next_zero_index_ = start_bit_;
    }

    int64_t BitmapSpan::next_one_avx512() {
        if (next_one_index_ > end_bit_) return -1;
        int64_t pos = find_next_bit_avx512(bits_, num_bits_,
                                        next_one_index_ - start_bit_,
                                        end_bit_ - start_bit_, true);
        if (pos != -1) {
            pos += start_bit_;
            next_one_index_ = pos + 1;
            return pos;
        }
        next_one_index_ = end_bit_ + 1;
        return -1;
    }

    int64_t BitmapSpan::next_zero_avx512() {
        if (next_zero_index_ > end_bit_) return -1;
        int64_t pos = find_next_bit_avx512(bits_, num_bits_,
                                        next_zero_index_ - start_bit_,
                                        end_bit_ - start_bit_, false);
        if (pos != -1) {
            pos += start_bit_;
            next_zero_index_ = pos + 1;
            return pos;
        }
        next_zero_index_ = end_bit_ + 1;
        return -1;
    }

    void BitmapSpan::reset_next_indices() {
        next_one_index_ = start_bit_;
        next_zero_index_ = start_bit_;
    }   

    bool BitmapSpan::empty() {
        return bits_.empty() || num_bits_ == 0;;
    }
}