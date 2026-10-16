#pragma once

#include <cstdint>
#include <string>
#include <vector>

void write_png(const std::string &path, uint32_t width, uint32_t height,
               int channels, const std::vector<uint8_t> &pixels);
