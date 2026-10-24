#include "mmap_file.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

MmapFile::MmapFile(int fd, uint64_t offset, uint64_t length) {
    if (length == 0) {
        return;
    }

    const uint64_t page_size = static_cast<uint64_t>(::sysconf(_SC_PAGESIZE));
    const uint64_t aligned_offset = offset & ~(page_size - 1);
    const uint64_t slop = offset - aligned_offset;

    map_length_ = static_cast<std::size_t>(length + slop);
    base_ = ::mmap(nullptr, map_length_, PROT_READ, MAP_PRIVATE, fd,
                   static_cast<off_t>(aligned_offset));

    if (base_ == MAP_FAILED) {
        const int saved_errno = errno;
        base_ = nullptr;
        map_length_ = 0;
        throw std::runtime_error(std::string("mmap failed: ") +
                                 std::strerror(saved_errno));
    }

    data_ = static_cast<const uint8_t *>(base_) + slop;
    length_ = length;

}

void MmapFile::unmap() {
    if (base_ != nullptr) {
        ::munmap(base_, map_length_);
    }
    base_ = nullptr;
    map_length_ = 0;
    data_ = nullptr;
    length_ = 0;
}

MmapFile::~MmapFile() { unmap(); }

MmapFile::MmapFile(MmapFile &&other) noexcept
    : base_(other.base_), map_length_(other.map_length_), data_(other.data_),
      length_(other.length_) {
    other.base_ = nullptr;
    other.map_length_ = 0;
    other.data_ = nullptr;
    other.length_ = 0;
}

MmapFile &MmapFile::operator=(MmapFile &&other) noexcept {
    if (this != &other) {
        unmap();
        base_ = other.base_;
        map_length_ = other.map_length_;
        data_ = other.data_;
        length_ = other.length_;
        other.base_ = nullptr;
        other.map_length_ = 0;
        other.data_ = nullptr;
        other.length_ = 0;
    }
    return *this;
}
