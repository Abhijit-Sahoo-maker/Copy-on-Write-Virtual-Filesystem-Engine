#pragma once

#include "cowfs/common/types.hpp"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace cowfs {

// Cache-aligned wrapper to prevent false sharing between CPU cores
template <typename T>
struct alignas(CACHE_LINE_SIZE) CacheAligned {
    T value;

    template <typename... Args>
    explicit CacheAligned(Args&&... args) : value(std::forward<Args>(args)...) {}

    T* operator->() noexcept { return &value; }
    const T* operator->() const noexcept { return &value; }
    T& operator*() noexcept { return value; }
    const T& operator*() const noexcept { return value; }
};

// RAII Wrapper for page-aligned (4096 byte) block buffers
class AlignedBuffer {
public:
    explicit AlignedBuffer(size_t size = BLOCK_SIZE, size_t alignment = BLOCK_SIZE)
        : size_(size), alignment_(alignment), data_(nullptr) {
        allocate();
    }

    ~AlignedBuffer() {
        deallocate();
    }

    // Non-copyable, movable
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    AlignedBuffer(AlignedBuffer&& other) noexcept
        : size_(other.size_), alignment_(other.alignment_), data_(other.data_) {
        other.data_ = nullptr;
        other.size_ = 0;
    }

    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept {
        if (this != &other) {
            deallocate();
            size_ = other.size_;
            alignment_ = other.alignment_;
            data_ = other.data_;
            other.data_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    uint8_t* data() noexcept { return data_; }
    const uint8_t* data() const noexcept { return data_; }
    size_t size() const noexcept { return size_; }

    void zero() noexcept {
        if (data_) std::memset(data_, 0, size_);
    }

    uint8_t& operator[](size_t index) noexcept { return data_[index]; }
    const uint8_t& operator[](size_t index) const noexcept { return data_[index]; }

private:
    void allocate() {
        if (size_ == 0) return;
#if defined(_MSC_VER)
        data_ = static_cast<uint8_t*>(_aligned_malloc(size_, alignment_));
#else
        void* ptr = nullptr;
        if (posix_memalign(&ptr, alignment_, size_) != 0) {
            throw std::bad_alloc();
        }
        data_ = static_cast<uint8_t*>(ptr);
#endif
        if (!data_) {
            throw std::bad_alloc();
        }
        zero();
    }

    void deallocate() noexcept {
        if (data_) {
#if defined(_MSC_VER)
            _aligned_free(data_);
#else
            std::free(data_);
#endif
            data_ = nullptr;
        }
    }

    size_t size_;
    size_t alignment_;
    uint8_t* data_;
};

} // namespace cowfs
