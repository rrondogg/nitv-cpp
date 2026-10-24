#pragma once

#include <cstdint>


// Another half assed thing because POSIX mmap() requires the file offset to be a multiple of the page size,
// but NITF segment data starts at whatever byte the preceding headers happen to end on.
// So gotta round the requested offset down to a page boundary, map a correspondingly longer region, and hand back
// a pointer nudged forward to the byte the caller actually asked for. Another thing that can be replaced with something better in the future probably
class MmapFile {
public:
    MmapFile() = default;
    MmapFile(int fd, uint64_t offset, uint64_t length);
    ~MmapFile();

    // Move-only. Copying would unmap the same region twice.
    MmapFile(const MmapFile &) = delete;
    MmapFile &operator=(const MmapFile &) = delete;
    MmapFile(MmapFile &&other) noexcept;
    MmapFile &operator=(MmapFile &&other) noexcept;

    const uint8_t *data() const { return data_; }

    uint64_t size() const { return length_; }

private:
    void unmap();

    void *base_ = nullptr;           // what mmap() returned (page aligned)
    std::size_t map_length_ = 0;     // what munmap() needs to be given
    const uint8_t *data_ = nullptr;  // base_ + slop
    uint64_t length_ = 0;            // what the caller asked for
};
