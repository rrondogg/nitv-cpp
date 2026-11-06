#pragma once

#include <cstdint>
#include <cstring>
#include <vector>


template <typename Pixel>
struct BlockedView { // probably not gonna need  most of this later but in the meantime it's worth documenting each field so it doesn't get out of whack
    const uint8_t *base = nullptr; 

    uint32_t n_rows = 0;      // significant rows    (NROWS)
    uint32_t n_cols = 0;      // significant columns (NCOLS)

    uint32_t block_rows = 1;  // number of block rows    (NBPC)
    uint32_t block_cols = 1;  // number of block columns (NBPR)
    uint32_t block_h = 0;     // pixels down each block  (NPPBV)
    uint32_t block_w = 0;     // pixels across each block (NPPBH)

    Pixel at(uint32_t row, uint32_t col) const {
        const uint32_t block_row = row / block_h;
        const uint32_t row_in_block = row % block_h;
        const uint32_t block_col = col / block_w;
        const uint32_t col_in_block = col % block_w;

        // Blocks are stored whole, in block-row-major order.
        const std::size_t block_index =
            static_cast<std::size_t>(block_row) * block_cols + block_col;
        const std::size_t pixels_per_block =
            static_cast<std::size_t>(block_h) * block_w;
        const std::size_t index = block_index * pixels_per_block +
                                  static_cast<std::size_t>(row_in_block) *
                                      block_w +
                                  col_in_block;

        Pixel pixel;
        std::memcpy(&pixel, base + index * sizeof(Pixel), sizeof(Pixel));
        return pixel;
    }
};


template <typename Pixel>
struct SegmentStack {
    std::vector<BlockedView<Pixel>> views;
    uint32_t n_rows = 0;
    uint32_t n_cols = 0;

    Pixel at(uint32_t row, uint32_t col) const {
        // Clamp to the edge rather than reading past it. The resampler's
        // upsampling branches ask for row+1 and col+1, which runs off the end
        // on the last row/column.
        if (row >= n_rows) {
            row = n_rows - 1;
        }
        if (col >= n_cols) {
            col = n_cols - 1;
        }

        uint32_t remaining = row;
        for (const BlockedView<Pixel> &view : views) {
            if (remaining < view.n_rows) {
                return view.at(remaining, col);
            }
            remaining -= view.n_rows;
        }
        // Unreachable bcause row was clamped to n_rows - 1, which is the sum of
        // the views' row counts. Return the first pixel so the compiler is
        // satisfied.
        return views.front().at(0, 0);
    }
};

template <typename Pixel>
BlockedView<Pixel> make_view(const uint8_t *base, uint32_t n_rows,
                             uint32_t n_cols, uint32_t nbpr, uint32_t nbpc,
                             uint32_t nppbh, uint32_t nppbv) {
    BlockedView<Pixel> view;
    view.base = base;
    view.n_rows = n_rows;
    view.n_cols = n_cols;

    view.block_cols = nbpr;
    view.block_w = nppbh;
    if (nbpr <= 1) {
        view.block_cols = 1;
        view.block_w = n_cols;
    }

    view.block_rows = nbpc;
    view.block_h = nppbv;
    if (nbpc <= 1) {
        view.block_rows = 1;
        view.block_h = n_rows;
    }

    return view;
}
