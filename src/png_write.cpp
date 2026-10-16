#include "png_write.hpp"

#include <zlib.h>

#include <cstdio>
#include <stdexcept>


static void push_be32(std::vector<uint8_t> &out, uint32_t value) {
    out.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(value & 0xFF));
}

static void push_chunk(std::vector<uint8_t> &out, const char type[4],
                       const std::vector<uint8_t> &data) {
    push_be32(out, static_cast<uint32_t>(data.size()));

    const std::size_t crc_begin = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());

    const uLong crc = ::crc32(0L, out.data() + crc_begin,
                              static_cast<uInt>(out.size() - crc_begin));
    push_be32(out, static_cast<uint32_t>(crc));
}

void write_png(const std::string &path, uint32_t width, uint32_t height,
               int channels, const std::vector<uint8_t> &pixels) {
    if (channels != 1 && channels != 3) {
        throw std::runtime_error("write_png supports 1 or 3 channels, got " +
                                 std::to_string(channels));
    }
    const std::size_t expected =
        static_cast<std::size_t>(width) * height * channels;
    if (pixels.size() != expected) {
        throw std::runtime_error("write_png got " +
                                 std::to_string(pixels.size()) +
                                 " bytes of pixels, expected " +
                                 std::to_string(expected));
    }


    std::vector<uint8_t> raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + width * channels));
    for (uint32_t row = 0; row < height; ++row) {
        raw.push_back(0);
        const std::size_t row_begin =
            static_cast<std::size_t>(row) * width * channels;
        raw.insert(raw.end(), pixels.begin() + row_begin,
                   pixels.begin() + row_begin + width * channels);
    }

    uLongf compressed_size = ::compressBound(static_cast<uLong>(raw.size()));
    std::vector<uint8_t> compressed(compressed_size);
    const int status = ::compress2(compressed.data(), &compressed_size,
                                   raw.data(), static_cast<uLong>(raw.size()),
                                   Z_DEFAULT_COMPRESSION);
    if (status != Z_OK) {
        throw std::runtime_error("zlib compression failed with code " +
                                 std::to_string(status));
    }
    compressed.resize(compressed_size);






    std::vector<uint8_t> file = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    std::vector<uint8_t> ihdr;
    push_be32(ihdr, width);
    push_be32(ihdr, height);
    ihdr.push_back(8);                                 // bit depth
    ihdr.push_back(channels == 1 ? 0 : 2);             // 0 = grey, 2 = RGB
    ihdr.push_back(0);                                 // compression: deflate
    ihdr.push_back(0);                                 // filter method 0
    ihdr.push_back(0);                                 // no interlacing
    push_chunk(file, "IHDR", ihdr);

    push_chunk(file, "IDAT", compressed);
    push_chunk(file, "IEND", {});

    std::FILE *handle = std::fopen(path.c_str(), "wb");
    if (handle == nullptr) {
        throw std::runtime_error("cannot open " + path + " for writing");
    }
    const std::size_t written =
        std::fwrite(file.data(), 1, file.size(), handle);
    std::fclose(handle);
    if (written != file.size()) {
        throw std::runtime_error("short write to " + path);
    }

}
