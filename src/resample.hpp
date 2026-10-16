#pragma once

#include <cmath>
#include <cstdint>
#include <vector>


inline uint8_t to_u8(float value) {
    if (!(value > 0.0f)) {  // false for negatives AND for NaN
        return 0;
    }
    if (value > 255.0f) {
        return 255;
    }
    return static_cast<uint8_t>(value);
}

inline float frac(float value) { return value - std::trunc(value); }

inline uint32_t clamp_u32(uint32_t value, uint32_t low, uint32_t high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

struct OutputSize {
    uint32_t rows = 0;
    uint32_t cols = 0;
};


inline OutputSize calc_size(float n_rows, float n_cols, uint32_t size) {
    const float aspect = n_cols / n_rows;
    const float budget = (size != 0)
                             ? static_cast<float>(size) * static_cast<float>(size)
                             : n_rows * n_cols;
    //   aspect   = n_cols / n_rows
    //   budget   = size^2         (or the full input size when `size` is 0)
    //   out_cols = sqrt(aspect * budget)
    //   out_rows = budget / out_cols
    OutputSize out;
    out.cols = static_cast<uint32_t>(std::sqrt(aspect * budget));
    if (out.cols == 0) {
        out.cols = 1; 
    }
    out.rows = static_cast<uint32_t>(budget / static_cast<float>(out.cols));
    if (out.rows == 0) {
        out.rows = 1;
    }

    return out;
}

template <typename Sampler>
std::vector<uint8_t> resample(const Sampler &sample, int channels,
                              uint32_t n_rows, uint32_t n_cols,
                              uint32_t out_rows, uint32_t out_cols) {
    const float y_ratio = static_cast<float>(n_rows) / static_cast<float>(out_rows);
    const float x_ratio = static_cast<float>(n_cols) / static_cast<float>(out_cols);

    std::vector<uint8_t> pixels(static_cast<std::size_t>(out_rows) * out_cols *
                                channels);

    for (uint32_t out_y = 0; out_y < out_rows; ++out_y) {
        for (uint32_t out_x = 0; out_x < out_cols; ++out_x) {
            // The footprint of this output pixel in input coordinates.
            const float bottomf = static_cast<float>(out_y) * y_ratio;
            const float topf = bottomf + y_ratio;
            const float leftf = static_cast<float>(out_x) * x_ratio;
            const float rightf = leftf + x_ratio;

            const uint32_t bottom = clamp_u32(
                static_cast<uint32_t>(std::ceil(bottomf)), 0, n_rows - 1);
            const uint32_t top = clamp_u32(
                static_cast<uint32_t>(std::ceil(topf)), bottom, n_rows);
            const uint32_t left = clamp_u32(
                static_cast<uint32_t>(std::ceil(leftf)), 0, n_cols - 1);
            const uint32_t right = clamp_u32(
                static_cast<uint32_t>(std::ceil(rightf)), left, n_cols);

            for (int channel = 0; channel < channels; ++channel) {
                float value = 0.0f;

                if (bottom != top && left != right) {
                    // Downsampling case: the footprint covers whole input pixels in
                    // both directions, so take their mean. This is the only
                    // branch a normal thumbnail ever reaches.
                    const float count =
                        static_cast<float>((top - bottom) * (right - left));
                    float sum = 0.0f;
                    for (uint32_t row = bottom; row < top; ++row) {
                        for (uint32_t col = left; col < right; ++col) {
                            sum += sample(row, col, channel);
                        }
                    }
                    value = sum / count;

                } else if (bottom != top) {
                    // At least one whole row, less than one column case: blend the
                    // two neighbouring columns by the horizontal fraction
                    const float fraction = (frac(leftf) + frac(rightf)) / 2.0f;
                    float sum_left = 0.0f;
                    float sum_right = 0.0f;
                    for (uint32_t row = bottom; row < top; ++row) {
                        sum_left += sample(row, left, channel);
                        sum_right += sample(row, left + 1, channel);
                    }
                    const float span = static_cast<float>(top - bottom);
                    value = (1.0f - fraction) / span * sum_left +
                            fraction / span * sum_right;

                } else if (left != right) {
                    // At least one whole column, less than one row case :blend the
                    // two neighbouring rows by the vertical fraction
                    const float fraction = (frac(topf) + frac(bottomf)) / 2.0f;
                    float sum_bottom = 0.0f;
                    float sum_top = 0.0f;
                    for (uint32_t col = left; col < right; ++col) {
                        sum_bottom += sample(bottom, col, channel);
                        sum_top += sample(bottom + 1, col, channel);
                    }
                    const float span = static_cast<float>(right - left);
                    value = (1.0f - fraction) / span * sum_bottom +
                            fraction / span * sum_top;

                } else {
                    // Upsampling case: the footprint is inside a single input
                    // pixel, so do a standard four-tap bilinear blend.
                    const float fh = (frac(leftf) + frac(rightf)) / 2.0f;
                    const float fv = (frac(topf) + frac(bottomf)) / 2.0f;

                    const float factor_tr = fv * fh;
                    const float factor_tl = fv * (1.0f - fh);
                    const float factor_br = (1.0f - fv) * fh;
                    const float factor_bl = (1.0f - fv) * (1.0f - fh);

                    const float bottom_left = sample(bottom, left, channel);
                    const float top_left = sample(bottom + 1, left, channel);
                    const float bottom_right = sample(bottom, left + 1, channel);
                    const float top_right = sample(bottom + 1, left + 1, channel);

                    // the summation order decides the last
                    // bit of the result. This order (br, tr, bl, tl) is the
                    // one the used in the rust code from vaughn so doing that
                    value = factor_br * bottom_right + factor_tr * top_right +
                            factor_bl * bottom_left + factor_tl * top_left;
                }

                const std::size_t index =
                    (static_cast<std::size_t>(out_y) * out_cols + out_x) *
                        channels +
                    channel;
                pixels[index] = to_u8(value);
            }
        }
    }

    return pixels;
}
