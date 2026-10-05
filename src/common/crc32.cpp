#include "cowfs/common/crc32.hpp"

namespace cowfs {

uint32_t CRC32::table[256];
bool CRC32::table_initialized = false;

void CRC32::init_table() {
    if (table_initialized) return;

    // CRC32-IEEE 802.3 standard polynomial: 0xEDB88320
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int j = 0; j < 8; ++j) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        table[i] = c;
    }
    table_initialized = true;
}

uint32_t CRC32::calculate(const void* data, size_t length) {
    init_table();
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;

    for (size_t i = 0; i < length; ++i) {
        crc = table[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

uint32_t CRC32::calculate(std::span<const uint8_t> data) {
    return calculate(data.data(), data.size());
}

} // namespace cowfs
