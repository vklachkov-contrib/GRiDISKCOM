#include "mbr.h"

bool isMbrDisk(const uint8_t* data, size_t size) {
    return size > 0x200 && data[0x1FE] == 0x55 && data[0x1FF] == 0xAA;
}

std::vector<MbrPartition> parseMbr(const uint8_t* data, size_t size) {
    std::vector<MbrPartition> partitions;
    partitions.reserve(4);

    if (size < 512) {
        return partitions;
    }

    const uint8_t* table = data + 0x1BE;
    for (size_t i = 0; i < 4; i++) {
        const uint8_t* entry = table + i * 16;

        bool isGRiD = entry[4] == 0x47;
        bool isActive = (entry[0] & 0x80) != 0;

        uint64_t part_offset = (entry[8] | entry[9] << 8 | entry[10] << 16 | entry[11] << 24) * 512;
        uint64_t part_size = (entry[12] | entry[13] << 8 | entry[14] << 16 | entry[15] << 24) * 512;

        // A zeroed entry marks the end of the used partition table.
        if (part_offset == 0 && part_size == 0 && !isGRiD && !isActive) {
            break;
        }

        partitions.push_back(MbrPartition {
            i,
            isGRiD,
            isActive,
            part_offset,
            part_size
        });
    }

    return partitions;
}
