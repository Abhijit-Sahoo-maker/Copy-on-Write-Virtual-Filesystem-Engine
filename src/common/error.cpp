#include "cowfs/common/error.hpp"

namespace cowfs {

std::string_view error_to_string(FsError err) {
    switch (err) {
        case FsError::Success: return "Success";
        case FsError::NotFound: return "No such file, directory, or snapshot";
        case FsError::AlreadyExists: return "Entry already exists";
        case FsError::NoSpaceLeft: return "No space left on device";
        case FsError::IoError: return "Hardware / storage I/O error";
        case FsError::CorruptedData: return "Data corruption detected";
        case FsError::ChecksumMismatch: return "Block CRC32 checksum mismatch";
        case FsError::InvalidArg: return "Invalid argument provided";
        case FsError::PermissionDenied: return "Permission denied";
        case FsError::NotADirectory: return "Not a directory";
        case FsError::IsADirectory: return "Is a directory";
        case FsError::DirectoryNotEmpty: return "Directory not empty";
        case FsError::BadFileDescriptor: return "Bad file descriptor";
        case FsError::DeviceNotMounted: return "Filesystem is not mounted";
        case FsError::DeviceAlreadyMounted: return "Filesystem is already mounted";
        case FsError::SnapshotNotFound: return "Snapshot not found";
        case FsError::SnapshotAlreadyExists: return "Snapshot name already exists";
        case FsError::FileTooLarge: return "File exceeds maximum supported size";
        case FsError::TransactionAborted: return "Transaction aborted";
        case FsError::RefcountOverflow: return "Block reference count overflow";
        case FsError::BufferPoolExhausted: return "Buffer pool / page cache exhausted";
        default: return "Unknown filesystem error";
    }
}

} // namespace cowfs
