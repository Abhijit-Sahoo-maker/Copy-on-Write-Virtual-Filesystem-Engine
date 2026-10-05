#pragma once

#include "cowfs/common/types.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace cowfs {

constexpr size_t MAX_FILENAME_LEN = 57;

#pragma pack(push, 1)
struct DirEntry {
    inode_id_t inode_id{INVALID_INODE}; // 4 bytes
    uint8_t file_type{0};               // 1 byte
    uint8_t name_len{0};                // 1 byte
    char name[MAX_FILENAME_LEN + 1];    // 58 bytes
    // Total = 4 + 1 + 1 + 58 = 64 bytes

    bool is_valid() const noexcept {
        return inode_id != INVALID_INODE && name_len > 0;
    }

    std::string_view get_name() const noexcept {
        return std::string_view(name, name_len);
    }

    void set_name(std::string_view str) {
        size_t len = std::min(str.size(), MAX_FILENAME_LEN);
        std::memcpy(name, str.data(), len);
        name[len] = '\0';
        name_len = static_cast<uint8_t>(len);
    }
};
#pragma pack(pop)

static_assert(sizeof(DirEntry) == 64, "DirEntry must be exactly 64 bytes");
constexpr size_t DIR_ENTRIES_PER_BLOCK = BLOCK_SIZE / sizeof(DirEntry); // 64 entries per block

} // namespace cowfs
