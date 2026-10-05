#include "test_framework.hpp"
#include "cowfs/vfs/vfs.hpp"
#include <cstring>

using namespace cowfs;

bool test_posix_vfs_file_io() {
    VFS vfs;
    TEST_ASSERT(vfs.mkfs(":memory:", 512) == 0, "mkfs failed");
    TEST_ASSERT(vfs.mount(":memory:") == 0, "mount failed");

    // Create and write
    int fd = vfs.open("/test.txt", OpenFlags::Create | OpenFlags::WriteOnly);
    TEST_ASSERT(fd >= 3, "open for write failed");

    const char* message = "Hello from POSIX VFS Emulation!";
    size_t len = std::strlen(message);
    ssize_t written = vfs.write(fd, message, len);
    TEST_ASSERT(written == static_cast<ssize_t>(len), "write count mismatch");
    TEST_ASSERT(vfs.close(fd) == 0, "close failed");

    // Reopen and read
    fd = vfs.open("/test.txt", OpenFlags::ReadOnly);
    TEST_ASSERT(fd >= 3, "open for read failed");

    char buffer[64] = {0};
    ssize_t read_bytes = vfs.read(fd, buffer, sizeof(buffer));
    TEST_ASSERT(read_bytes == static_cast<ssize_t>(len), "read count mismatch");
    TEST_ASSERT(std::string(buffer) == message, "content mismatch");

    // Seek test
    int64_t pos = vfs.lseek(fd, 6, SeekOrigin::Set);
    TEST_ASSERT(pos == 6, "lseek failed");
    char seek_buf[16] = {0};
    vfs.read(fd, seek_buf, 4);
    TEST_ASSERT(std::string(seek_buf) == "from", "seeked read mismatch");

    vfs.close(fd);
    return true;
}

bool test_directory_operations() {
    VFS vfs;
    vfs.mkfs(":memory:", 512);
    vfs.mount(":memory:");

    TEST_ASSERT(vfs.mkdir("/docs") == 0, "mkdir /docs failed");
    TEST_ASSERT(vfs.mkdir("/photos") == 0, "mkdir /photos failed");

    auto entries = vfs.ls("/");
    TEST_ASSERT(entries.size() == 2, "root directory entries count mismatch");

    int fd = vfs.open("/docs/file.txt", OpenFlags::Create | OpenFlags::WriteOnly);
    TEST_ASSERT(fd >= 3, "create file inside directory failed");
    vfs.close(fd);

    auto docs_entries = vfs.ls("/docs");
    TEST_ASSERT(docs_entries.size() == 1, "/docs entries count mismatch");

    return true;
}

bool test_deduplication() {
    VFS vfs;
    vfs.mkfs(":memory:", 512);
    vfs.mount(":memory:");

    // Write two identical 4KB blocks in two separate files
    std::vector<uint8_t> block_data(BLOCK_SIZE, 0x42);

    int fd1 = vfs.open("/file1.bin", OpenFlags::Create | OpenFlags::WriteOnly);
    vfs.write(fd1, block_data.data(), block_data.size());
    vfs.close(fd1);

    int fd2 = vfs.open("/file2.bin", OpenFlags::Create | OpenFlags::WriteOnly);
    vfs.write(fd2, block_data.data(), block_data.size());
    vfs.close(fd2);

    // Run deduplication
    size_t deduped = vfs.dedup();
    TEST_ASSERT(deduped >= 1, "Deduplication should detect and merge identical blocks");

    // Verify both files still read the exact same data
    std::vector<uint8_t> verify_buf(BLOCK_SIZE, 0);
    fd1 = vfs.open("/file1.bin", OpenFlags::ReadOnly);
    vfs.read(fd1, verify_buf.data(), verify_buf.size());
    vfs.close(fd1);
    TEST_ASSERT(verify_buf == block_data, "file1.bin verification failed after dedup");

    std::fill(verify_buf.begin(), verify_buf.end(), 0);
    fd2 = vfs.open("/file2.bin", OpenFlags::ReadOnly);
    vfs.read(fd2, verify_buf.data(), verify_buf.size());
    vfs.close(fd2);
    TEST_ASSERT(verify_buf == block_data, "file2.bin verification failed after dedup");

    return true;
}

int main() {
    std::cout << "--- Running POSIX VFS & Feature Tests ---\n";
    RUN_TEST(test_posix_vfs_file_io);
    RUN_TEST(test_directory_operations);
    RUN_TEST(test_deduplication);
    cowfs::test::print_summary();
    return (cowfs::test::failed_tests == 0) ? 0 : 1;
}
