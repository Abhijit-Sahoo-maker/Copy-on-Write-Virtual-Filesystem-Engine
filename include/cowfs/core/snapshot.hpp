#pragma once

#include "cowfs/common/types.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>

namespace cowfs {

constexpr size_t MAX_SNAPSHOT_NAME = 64;

#pragma pack(push, 1)
struct SnapshotRecord {
    char name[MAX_SNAPSHOT_NAME];
    uint64_t timestamp;
    uint64_t generation;
    inode_id_t root_inode_id;
    uint32_t flags;
};
#pragma pack(pop)

struct SnapshotInfo {
    std::string name;
    uint64_t timestamp;
    uint64_t generation;
    inode_id_t root_inode_id;
};

} // namespace cowfs
