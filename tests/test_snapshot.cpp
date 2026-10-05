#include "test_framework.hpp"
#include "cowfs/core/filesystem.hpp"
#include "cowfs/hardware/memory_block_device.hpp"

using namespace cowfs;

bool test_snapshot_lifecycle() {
    constexpr uint64_t TOTAL_BLOCKS = 512;
    auto dev = std::make_unique<MemoryBlockDevice>(TOTAL_BLOCKS);
    TEST_ASSERT(FileSystem::format(*dev, TOTAL_BLOCKS).is_ok(), "format failed");

    FileSystem fs(std::move(dev));
    TEST_ASSERT(fs.mount().is_ok(), "mount failed");

    // 1. Create a file and write initial state
    auto id_res = fs.create_file("/state.txt", FileType::Regular);
    TEST_ASSERT(id_res.is_ok(), "create file failed");
    inode_id_t file_id = id_res.value();

    std::string text_v1 = "State Version 1: Initial Commit";
    fs.write_file(file_id, 0, reinterpret_cast<const uint8_t*>(text_v1.data()), text_v1.size());

    // 2. Take Snapshot 'snap_v1'
    TEST_ASSERT(fs.create_snapshot("snap_v1").is_ok(), "create_snapshot snap_v1 failed");

    auto snaps = fs.list_snapshots().value_or(std::vector<SnapshotInfo>{});
    TEST_ASSERT(snaps.size() == 1, "Snapshot list count mismatch");
    TEST_ASSERT(snaps[0].name == "snap_v1", "Snapshot name mismatch");

    // 3. Mutate file (overwrite)
    std::string text_v2 = "State Version 2: Modified After Snapshot!";
    fs.write_file(file_id, 0, reinterpret_cast<const uint8_t*>(text_v2.data()), text_v2.size());

    char buf[128] = {0};
    fs.read_file(file_id, 0, reinterpret_cast<uint8_t*>(buf), sizeof(buf));
    TEST_ASSERT(std::string(buf) == text_v2, "Read should reflect mutated text_v2");

    // 4. Restore Snapshot 'snap_v1' (Time Travel!)
    TEST_ASSERT(fs.restore_snapshot("snap_v1").is_ok(), "restore_snapshot failed");

    // Re-resolve and read file
    auto id_restored_res = fs.lookup_path("/state.txt");
    TEST_ASSERT(id_restored_res.is_ok(), "lookup restored file failed");

    std::memset(buf, 0, sizeof(buf));
    fs.read_file(id_restored_res.value(), 0, reinterpret_cast<uint8_t*>(buf), sizeof(buf));
    TEST_ASSERT(std::string(buf) == text_v1, "Restored file must match text_v1 exactly");

    // 5. Delete snapshot
    TEST_ASSERT(fs.delete_snapshot("snap_v1").is_ok(), "delete_snapshot failed");
    auto snaps_after = fs.list_snapshots().value_or(std::vector<SnapshotInfo>{});
    TEST_ASSERT(snaps_after.empty(), "Snapshot list should be empty after deletion");

    return true;
}

int main() {
    std::cout << "--- Running Snapshot Lifecycle Tests ---\n";
    RUN_TEST(test_snapshot_lifecycle);
    cowfs::test::print_summary();
    return (cowfs::test::failed_tests == 0) ? 0 : 1;
}
