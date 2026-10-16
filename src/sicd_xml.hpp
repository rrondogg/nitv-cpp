#pragma once

#include <cstdint>
#include <string>
#include <vector>




// gotta be SICD
std::string first_element_name(const std::vector<uint8_t> &xml);
bool looks_like_sicd(const std::vector<uint8_t> &xml);

// Only need 4 numbers so instead of
// pulling in an XML library this does a plain nested substring search.

struct SicdGeometry {
    double row_ss = 0.0;     // Grid/Row/SS        -- row sample spacing, metres
    double col_ss = 0.0;     // Grid/Col/SS        -- column sample spacing
    double graze_ang = 0.0;  // SCPCOA/GrazeAng    -- degrees
    double twist_ang = 0.0;  // SCPCOA/TwistAng    -- degrees
};

SicdGeometry read_sicd_geometry(const std::vector<uint8_t> &xml);
