#pragma once

#include <cstdint>
#include <cstddef>
#include <span>

namespace cowfs {

class CRC32 {
public:
    static uint32_t calculate(const void* data, size_t length);
    static uint32_t calculate(std::span<const uint8_t> data);

private:
    static void init_table();
    static uint32_t table[256];
    static bool table_initialized;
};

} // namespace cowfs
