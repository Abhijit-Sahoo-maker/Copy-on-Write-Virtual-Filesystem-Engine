#include "cowfs/vfs/file_descriptor.hpp"

namespace cowfs {

int FileTable::allocate_fd(inode_id_t inode_id, int flags, const std::string& path, uint64_t initial_offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    int fd = next_fd_++;
    FileDescriptor desc;
    desc.fd = fd;
    desc.inode_id = inode_id;
    desc.flags = flags;
    desc.offset = initial_offset;
    desc.path = path;
    open_files_[fd] = desc;
    return fd;
}

std::optional<FileDescriptor> FileTable::get(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = open_files_.find(fd);
    if (it != open_files_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool FileTable::update_offset(int fd, uint64_t new_offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = open_files_.find(fd);
    if (it != open_files_.end()) {
        it->second.offset = new_offset;
        return true;
    }
    return false;
}

bool FileTable::free_fd(int fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_files_.erase(fd) > 0;
}

void FileTable::close_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_files_.clear();
}

} // namespace cowfs
