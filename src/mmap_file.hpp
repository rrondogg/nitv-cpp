#pragma once

#include <cstdint>


class MmapFile {
public:
    MmapFile(int fd, uint64_t offset, uint64_t length);
    ~MmapFile();

    MmapFile &operator=(const MmapFile &) = delete;
    MmapFile(MmapFile &&other) noexcept;
    MmapFile &operator=(MmapFile &&other) noexcept;


private:
    void unmap();

    void *base_ = nullptr;           // what mmap() returned (page aligned)
    std::size_t map_length_ = 0;     // what munmap() needs to be given
};
