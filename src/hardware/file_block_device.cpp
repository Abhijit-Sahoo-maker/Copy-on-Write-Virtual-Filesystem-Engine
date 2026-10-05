#include "cowfs/hardware/file_block_device.hpp"
#include <fcntl.h>
#include <sys/stat.h>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace cowfs {

FileBlockDevice::FileBlockDevice(std::string filepath, int fd, uint64_t total_blocks, size_t block_size)
    : filepath_(std::move(filepath)), fd_(fd), total_blocks_(total_blocks), block_size_(block_size) {}

FileBlockDevice::~FileBlockDevice() {
    if (fd_ >= 0) {
        sync();
#if defined(_WIN32)
        _close(fd_);
#else
        close(fd_);
#endif
        fd_ = -1;
    }
}

Result<std::unique_ptr<FileBlockDevice>> FileBlockDevice::create_or_open(
    const std::string& filepath,
    uint64_t total_blocks,
    size_t block_size
) {
    if (total_blocks == 0 || block_size == 0) {
        return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::InvalidArg);
    }

    uint64_t total_bytes = total_blocks * block_size;
    int fd = -1;

#if defined(_WIN32)
    fd = _open(filepath.c_str(), _O_RDWR | _O_CREAT | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0) return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::IoError);
    if (_chsize_s(fd, static_cast<__int64>(total_bytes)) != 0) {
        _close(fd);
        return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::IoError);
    }
#else
    fd = open(filepath.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0) return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::IoError);
    if (ftruncate(fd, static_cast<off_t>(total_bytes)) != 0) {
        close(fd);
        return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::IoError);
    }
#endif

    return Result<std::unique_ptr<FileBlockDevice>>::ok(
        std::unique_ptr<FileBlockDevice>(new FileBlockDevice(filepath, fd, total_blocks, block_size))
    );
}

Result<std::unique_ptr<FileBlockDevice>> FileBlockDevice::open_existing(
    const std::string& filepath,
    size_t block_size
) {
    int fd = -1;
#if defined(_WIN32)
    fd = _open(filepath.c_str(), _O_RDWR | _O_BINARY);
    if (fd < 0) return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::NotFound);

    __int64 file_size = _lseeki64(fd, 0, SEEK_END);
    if (file_size < 0) {
        _close(fd);
        return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::IoError);
    }
#else
    fd = open(filepath.c_str(), O_RDWR);
    if (fd < 0) return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::NotFound);

    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size < 0) {
        close(fd);
        return Result<std::unique_ptr<FileBlockDevice>>::err(FsError::IoError);
    }
#endif

    uint64_t total_blocks = static_cast<uint64_t>(file_size) / block_size;
    return Result<std::unique_ptr<FileBlockDevice>>::ok(
        std::unique_ptr<FileBlockDevice>(new FileBlockDevice(filepath, fd, total_blocks, block_size))
    );
}

Result<void> FileBlockDevice::read_block(block_id_t block_id, uint8_t* out_buffer) {
    if (!out_buffer || block_id >= total_blocks_ || fd_ < 0) {
        return Result<void>::err(FsError::InvalidArg);
    }

    uint64_t offset = static_cast<uint64_t>(block_id) * block_size_;

#if defined(_WIN32)
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (_lseeki64(fd_, static_cast<__int64>(offset), SEEK_SET) < 0) {
        return Result<void>::err(FsError::IoError);
    }
    int bytes_read = _read(fd_, out_buffer, static_cast<unsigned int>(block_size_));
    if (bytes_read != static_cast<int>(block_size_)) {
        return Result<void>::err(FsError::IoError);
    }
#else
    // POSIX pread() is thread-safe and does not modify file offset
    ssize_t bytes_read = pread(fd_, out_buffer, block_size_, static_cast<off_t>(offset));
    if (bytes_read != static_cast<ssize_t>(block_size_)) {
        return Result<void>::err(FsError::IoError);
    }
#endif

    stats_.reads_count++;
    stats_.bytes_read += block_size_;
    return Result<void>::ok();
}

Result<void> FileBlockDevice::write_block(block_id_t block_id, const uint8_t* in_buffer) {
    if (!in_buffer || block_id >= total_blocks_ || fd_ < 0) {
        return Result<void>::err(FsError::InvalidArg);
    }

    uint64_t offset = static_cast<uint64_t>(block_id) * block_size_;

#if defined(_WIN32)
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (_lseeki64(fd_, static_cast<__int64>(offset), SEEK_SET) < 0) {
        return Result<void>::err(FsError::IoError);
    }
    int bytes_written = _write(fd_, in_buffer, static_cast<unsigned int>(block_size_));
    if (bytes_written != static_cast<int>(block_size_)) {
        return Result<void>::err(FsError::IoError);
    }
#else
    // POSIX pwrite() is thread-safe and atomic with respect to offset
    ssize_t bytes_written = pwrite(fd_, in_buffer, block_size_, static_cast<off_t>(offset));
    if (bytes_written != static_cast<ssize_t>(block_size_)) {
        return Result<void>::err(FsError::IoError);
    }
#endif

    stats_.writes_count++;
    stats_.bytes_written += block_size_;
    return Result<void>::ok();
}

Result<void> FileBlockDevice::sync() {
    if (fd_ < 0) return Result<void>::err(FsError::DeviceNotMounted);

#if defined(_WIN32)
    if (_commit(fd_) != 0) {
        return Result<void>::err(FsError::IoError);
    }
#else
    // POSIX fdatasync: flushes dirty blocks to disk hardware
    if (fdatasync(fd_) != 0) {
        return Result<void>::err(FsError::IoError);
    }
#endif

    stats_.syncs_count++;
    return Result<void>::ok();
}

} // namespace cowfs
