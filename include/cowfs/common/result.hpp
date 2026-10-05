#pragma once

#include "cowfs/common/error.hpp"
#include <variant>
#include <stdexcept>
#include <utility>

namespace cowfs {

template <typename T>
class Result {
public:
    Result(const T& val) : data_(val) {}
    Result(T&& val) : data_(std::move(val)) {}
    Result(FsError err) : data_(err) {}

    static Result<T> ok(T val) {
        return Result<T>(std::move(val));
    }

    static Result<T> err(FsError e) {
        return Result<T>(e);
    }

    bool is_ok() const noexcept {
        return std::holds_alternative<T>(data_);
    }

    bool is_err() const noexcept {
        return std::holds_alternative<FsError>(data_);
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    T& value() & {
        if (is_err()) {
            throw std::runtime_error(std::string(error_to_string(std::get<FsError>(data_))));
        }
        return std::get<T>(data_);
    }

    const T& value() const & {
        if (is_err()) {
            throw std::runtime_error(std::string(error_to_string(std::get<FsError>(data_))));
        }
        return std::get<T>(data_);
    }

    T&& value() && {
        if (is_err()) {
            throw std::runtime_error(std::string(error_to_string(std::get<FsError>(data_))));
        }
        return std::move(std::get<T>(data_));
    }

    T value_or(T default_val) const {
        return is_ok() ? std::get<T>(data_) : default_val;
    }

    FsError error() const noexcept {
        return is_err() ? std::get<FsError>(data_) : FsError::Success;
    }

private:
    std::variant<T, FsError> data_;
};

// Specialization for void
template <>
class Result<void> {
public:
    Result() : error_(FsError::Success) {}
    Result(FsError err) : error_(err) {}

    static Result<void> ok() {
        return Result<void>();
    }

    static Result<void> err(FsError e) {
        return Result<void>(e);
    }

    bool is_ok() const noexcept {
        return error_ == FsError::Success;
    }

    bool is_err() const noexcept {
        return error_ != FsError::Success;
    }

    explicit operator bool() const noexcept {
        return is_ok();
    }

    FsError error() const noexcept {
        return error_;
    }

private:
    FsError error_;
};

} // namespace cowfs
