#pragma once

#include "cowfs/common/types.hpp"
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>

namespace cowfs {

struct FileDescriptor {
    int fd{-1};
    inode_id_t inode_id{INVALID_INODE};
    int flags{0};
    uint64_t offset{0};
    std::string path;

    bool is_readable() const noexcept {
        return (flags & OpenFlags::ReadOnly) || (flags & OpenFlags::ReadWrite);
    }

    bool is_writable() const noexcept {
        return (flags & OpenFlags::WriteOnly) || (flags & OpenFlags::ReadWrite);
    }
};

class FileTable {
public:
    FileTable() = default;

    int allocate_fd(inode_id_t inode_id, int flags, const std::string& path, uint64_t initial_offset = 0);
    std::optional<FileDescriptor> get(int fd);
    bool update_offset(int fd, uint64_t new_offset);
    bool free_fd(int fd);
    void close_all();

private:
    std::unordered_map<int, FileDescriptor> open_files_;
    int next_fd_{3}; // Start at 3, mimicking standard POSIX (0: stdin, 1: stdout, 2: stderr)
    std::mutex mutex_;
};

} // namespace cowfs
