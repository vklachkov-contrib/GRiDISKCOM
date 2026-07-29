#ifndef MBR_H
#define MBR_H

#include <cstdint>
#include <vector>

struct MbrPartition {
    size_t   index;
    bool     isGRiD;
    bool     isActive;
    uint64_t offset;
    uint64_t size;
};

bool isMbrDisk(const uint8_t* data, size_t size);
std::vector<MbrPartition> parseMbr(const uint8_t* data, size_t size);

#endif // MBR_H
