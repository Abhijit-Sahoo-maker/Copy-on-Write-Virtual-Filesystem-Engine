#pragma once

#include <string>
#include <string_view>

namespace cowfs {

enum class FsError {
    Success = 0,
    NotFound,
    AlreadyExists,
    NoSpaceLeft,
    IoError,
    CorruptedData,
    ChecksumMismatch,
    InvalidArg,
    PermissionDenied,
    NotADirectory,
    IsADirectory,
    DirectoryNotEmpty,
    BadFileDescriptor,
    DeviceNotMounted,
    DeviceAlreadyMounted,
    SnapshotNotFound,
    SnapshotAlreadyExists,
    FileTooLarge,
    TransactionAborted,
    RefcountOverflow,
    BufferPoolExhausted
};

std::string_view error_to_string(FsError err);

} // namespace cowfs
