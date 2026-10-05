#include "cowfs/vfs/vfs.hpp"
#include "cowfs/hardware/file_block_device.hpp"
#include "cowfs/hardware/memory_block_device.hpp"

namespace cowfs {

VFS::VFS() = default;

VFS::~VFS() {
    if (is_mounted()) {
        unmount();
    }
}

int VFS::mkfs(const std::string& dev_path, uint64_t total_blocks) {
    std::unique_ptr<IBlockDevice> dev;
    if (dev_path == ":memory:") {
        dev = std::make_unique<MemoryBlockDevice>(total_blocks);
    } else {
        auto dev_res = FileBlockDevice::create_or_open(dev_path, total_blocks);
        if (dev_res.is_err()) return -1;
        dev = std::move(dev_res.value());
    }

    auto fmt_res = FileSystem::format(*dev, total_blocks);
    return fmt_res.is_ok() ? 0 : -1;
}

int VFS::mount(const std::string& dev_path) {
    if (is_mounted()) return -1;

    std::unique_ptr<IBlockDevice> dev;
    if (dev_path == ":memory:") {
        dev = std::make_unique<MemoryBlockDevice>(1024); // default 4MB for memory
        FileSystem::format(*dev, 1024);
    } else {
        auto dev_res = FileBlockDevice::open_existing(dev_path);
        if (dev_res.is_err()) return -1;
        dev = std::move(dev_res.value());
    }

    fs_ = std::make_unique<FileSystem>(std::move(dev));
    auto mnt_res = fs_->mount();
    if (mnt_res.is_err()) {
        fs_.reset();
        return -1;
    }

    return 0;
}

int VFS::unmount() {
    if (!is_mounted()) return -1;
    file_table_.close_all();
    auto res = fs_->unmount();
    fs_.reset();
    return res.is_ok() ? 0 : -1;
}

bool VFS::is_mounted() const {
    return fs_ && fs_->is_mounted();
}

int VFS::open(const std::string& path, int flags, int mode) {
    if (!is_mounted()) return -1;

    auto lookup_res = fs_->lookup_path(path);
    inode_id_t inode_id = INVALID_INODE;

    if (lookup_res.is_ok()) {
        inode_id = lookup_res.value();
        if (flags & OpenFlags::Truncate) {
            fs_->truncate_file(inode_id, 0);
        }
    } else if (flags & OpenFlags::Create) {
        auto create_res = fs_->create_file(path, FileType::Regular, static_cast<uint16_t>(mode));
        if (create_res.is_err()) return -1;
        inode_id = create_res.value();
    } else {
        return -1; // File not found and O_CREAT not specified
    }

    uint64_t initial_offset = 0;
    if (flags & OpenFlags::Append) {
        auto stat_res = fs_->stat_file(inode_id);
        if (stat_res.is_ok()) {
            initial_offset = stat_res.value().size;
        }
    }

    return file_table_.allocate_fd(inode_id, flags, path, initial_offset);
}

int VFS::close(int fd) {
    return file_table_.free_fd(fd) ? 0 : -1;
}

ssize_t VFS::read(int fd, void* buf, size_t count) {
    if (!is_mounted()) return -1;

    auto desc_opt = file_table_.get(fd);
    if (!desc_opt.has_value()) return -1;
    auto desc = desc_opt.value();

    if (!desc.is_readable()) return -1;

    auto read_res = fs_->read_file(desc.inode_id, desc.offset, static_cast<uint8_t*>(buf), count);
    if (read_res.is_err()) return -1;

    size_t bytes = read_res.value();
    file_table_.update_offset(fd, desc.offset + bytes);
    return static_cast<ssize_t>(bytes);
}

ssize_t VFS::write(int fd, const void* buf, size_t count) {
    if (!is_mounted()) return -1;

    auto desc_opt = file_table_.get(fd);
    if (!desc_opt.has_value()) return -1;
    auto desc = desc_opt.value();

    if (!desc.is_writable()) return -1;

    auto write_res = fs_->write_file(desc.inode_id, desc.offset, static_cast<const uint8_t*>(buf), count);
    if (write_res.is_err()) return -1;

    size_t bytes = write_res.value();
    file_table_.update_offset(fd, desc.offset + bytes);
    return static_cast<ssize_t>(bytes);
}

int64_t VFS::lseek(int fd, int64_t offset, SeekOrigin whence) {
    if (!is_mounted()) return -1;

    auto desc_opt = file_table_.get(fd);
    if (!desc_opt.has_value()) return -1;
    auto desc = desc_opt.value();

    int64_t new_offset = 0;
    switch (whence) {
        case SeekOrigin::Set:
            new_offset = offset;
            break;
        case SeekOrigin::Current:
            new_offset = static_cast<int64_t>(desc.offset) + offset;
            break;
        case SeekOrigin::End: {
            auto st = fs_->stat_file(desc.inode_id);
            if (st.is_err()) return -1;
            new_offset = static_cast<int64_t>(st.value().size) + offset;
            break;
        }
    }

    if (new_offset < 0) return -1;

    file_table_.update_offset(fd, static_cast<uint64_t>(new_offset));
    return new_offset;
}

int VFS::unlink(const std::string& path) {
    if (!is_mounted()) return -1;
    return fs_->remove_file(path).is_ok() ? 0 : -1;
}

int VFS::stat(const std::string& path, FileStat* st) {
    if (!is_mounted() || !st) return -1;
    auto id_res = fs_->lookup_path(path);
    if (id_res.is_err()) return -1;

    auto stat_res = fs_->stat_file(id_res.value());
    if (stat_res.is_err()) return -1;

    *st = stat_res.value();
    return 0;
}

int VFS::mkdir(const std::string& path, int mode) {
    if (!is_mounted()) return -1;
    return fs_->create_directory(path, static_cast<uint16_t>(mode)).is_ok() ? 0 : -1;
}

int VFS::rmdir(const std::string& path) {
    if (!is_mounted()) return -1;
    return fs_->remove_directory(path).is_ok() ? 0 : -1;
}

std::vector<DirEntry> VFS::ls(const std::string& path) {
    if (!is_mounted()) return {};
    auto res = fs_->list_directory(path);
    return res.value_or(std::vector<DirEntry>{});
}

int VFS::clone(const std::string& src_path, const std::string& dst_path) {
    if (!is_mounted()) return -1;
    return fs_->clone_file(src_path, dst_path).is_ok() ? 0 : -1;
}

int VFS::snapshot_create(const std::string& name) {
    if (!is_mounted()) return -1;
    return fs_->create_snapshot(name).is_ok() ? 0 : -1;
}

int VFS::snapshot_restore(const std::string& name) {
    if (!is_mounted()) return -1;
    return fs_->restore_snapshot(name).is_ok() ? 0 : -1;
}

int VFS::snapshot_delete(const std::string& name) {
    if (!is_mounted()) return -1;
    return fs_->delete_snapshot(name).is_ok() ? 0 : -1;
}

std::vector<SnapshotInfo> VFS::snapshot_list() {
    if (!is_mounted()) return {};
    return fs_->list_snapshots().value_or(std::vector<SnapshotInfo>{});
}

size_t VFS::dedup() {
    if (!is_mounted()) return 0;
    return fs_->deduplicate_blocks().value_or(0);
}

int VFS::sync() {
    if (!is_mounted()) return -1;
    return fs_->sync().is_ok() ? 0 : -1;
}

FsStatus VFS::status() {
    if (!is_mounted()) return {};
    return fs_->get_status();
}

const CacheStats* VFS::cache_stats() const {
    if (!is_mounted()) return nullptr;
    return &fs_->get_cache_stats();
}

const BlockDeviceStats* VFS::io_stats() const {
    if (!is_mounted()) return nullptr;
    return &fs_->get_io_stats();
}

} // namespace cowfs
