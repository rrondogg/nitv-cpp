#include "blocked.hpp"
#include "mmap_file.hpp"
#include "nitf.hpp"
#include "png_write.hpp"
#include "resample.hpp"
#include "sicd_xml.hpp"

#include <argparse.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// These are all byte arrays with alignment 1. For reasons unharbored to me, segment data
// starts at an arbitrary byte offset in the file, so a float * or uint32_t pointed into the mapping could be misaligned, which is undefined
// behaviour. Copy bytes out and reassemble the numbers by hand

struct Rgb8 {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
static_assert(sizeof(Rgb8) == 3, "Rgb8 must be exactly 3 bytes");

struct Complex32BE {
    uint8_t re[4];
    uint8_t im[4];
};
static_assert(sizeof(Complex32BE) == 8, "Complex32BE must be exactly 8 bytes");

static float be_float(const uint8_t bytes[4]) {
    const uint32_t bits = (static_cast<uint32_t>(bytes[0]) << 24) |
                          (static_cast<uint32_t>(bytes[1]) << 16) |
                          (static_cast<uint32_t>(bytes[2]) << 8) |
                          static_cast<uint32_t>(bytes[3]);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static float amplitude(const Complex32BE &z) {
    const float real = be_float(z.re);
    const float imag = be_float(z.im);
    return std::sqrt(real * real + imag * imag);
}


struct Options {
    std::filesystem::path output_dir = ".";
    uint32_t size = 256;
    bool open_when_done = false;
};

// Refuse the cases we silently get wrong rather than producing noise
static void check_supported(const ImageHeader &header, int expected_nbpp) {
    if (header.ic != "NC") {
        throw std::runtime_error(
            "image compression IC=\"" + header.ic +
            "\" is not supported (only \"NC\", uncompressed with no block "
            "mask). Compressed and masked data would be read as raw pixels "
            "and come out as noise.");
    }
    if (header.nbpp != expected_nbpp) {
        throw std::runtime_error(
            "NBPP=" + std::to_string(header.nbpp) + " is not supported here; " +
            "this path expects " + std::to_string(expected_nbpp) +
            " bits per pixel");
    }
    if (header.nrows == 0 || header.ncols == 0) {
        throw std::runtime_error("image segment reports 0 rows or 0 columns");
    }
}




template <typename Pixel>
static SegmentStack<Pixel> map_segments(FileReader &reader, const Nitf &nitf,
                                        std::vector<MmapFile> &maps) {
    SegmentStack<Pixel> stack;
    // reserve up front so the vector never reallocates while we are taking
    // pointers out of it because we return stack
    maps.reserve(nitf.image_segments.size());

    for (const ImageSegment &segment : nitf.image_segments) {
        const ImageHeader &header = segment.header;

        maps.emplace_back(reader.fd(), segment.data_offset, segment.data_size);
        stack.views.push_back(make_view<Pixel>(
            maps.back().data(), header.nrows, header.ncols, header.nbpr,
            header.nbpc, header.nppbh, header.nppbv));

        // assuming each segment holds all the columns and a slice
        // of the rows. The image is the segments stacked top to bottom. Keeping
        // this kind of the same like vaughns rust implementation until i wanna mess around
        // and change this more
        stack.n_rows += header.nrows;
    }

    stack.n_cols = nitf.image_segments.front().header.ncols;
    for (const ImageSegment &segment : nitf.image_segments) {
        if (segment.header.ncols != stack.n_cols) {
            std::cerr << "nitv: warning: image segments disagree on column "
                         "count (" << segment.header.ncols << " vs "
                      << stack.n_cols << "); this reader assumes segments are "
                         "split along rows only\n";
        }
    }

    return stack;
}

static std::string output_path(const Options &options,
                               const std::string &input_path) {
    const std::string stem = std::filesystem::path(input_path).stem().string();
    return (options.output_dir / (stem + ".png")).string();
}



static std::string make_mono(FileReader &reader, const Nitf &nitf,
                             const Options &options,
                             const std::string &input_path) {
    // single band


    check_supported(nitf.image_segments.front().header, 8);

    std::vector<MmapFile> maps;
    const SegmentStack<uint8_t> stack =
        map_segments<uint8_t>(reader, nitf, maps);

        //8 bits per pixel, straight to greyscale
    const OutputSize out = calc_size(static_cast<float>(stack.n_rows),
                                     static_cast<float>(stack.n_cols),
                                     options.size);

    const auto sample = [&stack](uint32_t row, uint32_t col, int channel) {
        (void)channel;  // only one
        return static_cast<float>(stack.at(row, col));
    };

    const std::vector<uint8_t> pixels = resample(
        sample, 1, stack.n_rows, stack.n_cols, out.rows, out.cols);

    const std::string path = output_path(options, input_path);
    write_png(path, out.cols, out.rows, 1, pixels);
    return path;
}


// three bands interleaved by pixel, 8 bits each
static std::string make_rgb(FileReader &reader, const Nitf &nitf,
                            const Options &options,
                            const std::string &input_path) {
    const ImageHeader &first = nitf.image_segments.front().header;
    check_supported(first, 8);

    // IMODE P means the three bands for one pixel sit next to each other
    // (R,G,B,R,G,B,...), which is what the 3-byte Rgb8 layout assumes.
    if (first.imode != ImageMode::P) {
        throw std::runtime_error(
            std::string("RGB images are only supported with IMODE=P "
                        "(band interleaved by pixel); this file has IMODE=") +
            image_mode_name(first.imode));
    }
    if (first.bands.size() < 3) {
        throw std::runtime_error("RGB image has only " +
                                 std::to_string(first.bands.size()) +
                                 " band(s)");
    }

    std::vector<MmapFile> maps;
    const SegmentStack<Rgb8> stack = map_segments<Rgb8>(reader, nitf, maps);

    const OutputSize out = calc_size(static_cast<float>(stack.n_rows),
                                     static_cast<float>(stack.n_cols),
                                     options.size);

    const auto sample = [&stack](uint32_t row, uint32_t col, int channel) {
        const Rgb8 pixel = stack.at(row, col);
        switch (channel) {
        case 0:  return static_cast<float>(pixel.r);
        case 1:  return static_cast<float>(pixel.g);
        default: return static_cast<float>(pixel.b);
        }
    };

    const std::vector<uint8_t> pixels = resample(
        sample, 3, stack.n_rows, stack.n_cols, out.rows, out.cols);

    const std::string path = output_path(options, input_path);
    write_png(path, out.cols, out.rows, 3, pixels);
    return path;
}


// static std::string make_rgb_lut(FileReader &reader, const Nitf &nitf,
//                                 const Options &options,
//                                 const std::string &input_path) {
//     const ImageHeader &first = nitf.image_segments.front().header;
//     check_supported(first, 8);

//     if (first.bands.empty()) {
//         throw std::runtime_error("RGB/LUT image has no bands");
//     }

//     const std::vector<std::vector<uint8_t>> &lut = first.bands.front().lutd;
//     if (lut.size() < 3) {
//         throw std::runtime_error(
//             "RGB/LUT image has " + std::to_string(lut.size()) +
//             " look-up table(s); three (red, green, blue) are needed");
//     }
//     const std::size_t lut_entries = lut.front().size();

//     std::vector<MmapFile> maps;
//     const SegmentStack<uint8_t> stack =
//         map_segments<uint8_t>(reader, nitf, maps);

//     const OutputSize out = calc_size(static_cast<float>(stack.n_rows),
//                                      static_cast<float>(stack.n_cols),
//                                      options.size);

//     const auto sample = [&stack, &lut, lut_entries](uint32_t row, uint32_t col,
//                                                     int channel) {
//         const std::size_t index = stack.at(row, col);
//         if (index >= lut_entries) {
//             return 0.0f;  // index past the end of the table
//         }
//         return static_cast<float>(lut[static_cast<std::size_t>(channel)][index]);
//     };

//     const std::vector<uint8_t> pixels = resample(
//         sample, 3, stack.n_rows, stack.n_cols, out.rows, out.cols);

//     const std::string path = output_path(options, input_path);
//     write_png(path, out.cols, out.rows, 3, pixels);
//     return path;
// }



// Piecewise extended density format remap, matching
// sarpy and vaughns implementaiton. Keeping this way for now
struct Pedf {
    float eps = 1e-5f;
    float slope = 0.0f;
    float constant = 0.0f;

    uint8_t remap(float amp) const {
        const float density =
            slope * std::log10(std::max(amp, eps)) + constant;

        const float half = 127.0f;

        const float out = (density <= half) ? density : 0.5f * (density + half);
        return to_u8(out);
    }
};

static Pedf make_pedf(float mean_amplitude) {
    const float d_min = 30.0f;   // darkest output level we will use
    const float m_mult = 40.0f;  // decades of dynamic range to show

    const float c_low = 0.8f * mean_amplitude;
    const float c_high = m_mult * c_low;

    Pedf pedf;
    pedf.eps = 1e-5f;
    pedf.slope = (255.0f - d_min) / std::log10(c_high / c_low);
    pedf.constant = d_min - pedf.slope * std::log10(c_low);

    return pedf;
}

static double degrees_to_radians(double degrees) {
    return degrees * 3.14159265358979323846 / 180.0;
}

static std::string make_sicd(FileReader &reader, const Nitf &nitf,
                             const Options &options,
                             const std::string &input_path,
                             const std::vector<uint8_t> &xml) {
    const ImageHeader &first = nitf.image_segments.front().header;

    // 64 bits per pixel: a 32-bit real part and a 32-bit imaginary part.
    check_supported(first, 64);
    if (first.pvtype != PixelValueType::C) {
        throw std::runtime_error(
            std::string("SICD images are expected to have PVTYPE=C "
                        "(complex float); this file has PVTYPE=") +
            pixel_value_type_name(first.pvtype));
    }
    if (first.imode == ImageMode::B) {
        throw std::runtime_error(
            "blocked SICD data (IMODE=B) is not supported");
    }

    std::vector<MmapFile> maps;
    const SegmentStack<Complex32BE> stack =
        map_segments<Complex32BE>(reader, nitf, maps);

    // first pass: mean amplitude
    double mean_of_means = 0.0;
    for (const BlockedView<Complex32BE> &view : stack.views) {
        double sum = 0.0;
        for (uint32_t row = 0; row < view.n_rows; ++row) {
            double row_sum = 0.0;
            for (uint32_t col = 0; col < view.n_cols; ++col) {
                row_sum += amplitude(view.at(row, col));
            }
            sum += row_sum;
        }
        const double pixel_count =
            static_cast<double>(view.n_rows) * static_cast<double>(view.n_cols);
        mean_of_means += sum / pixel_count;
    }
    const float mean =
        static_cast<float>(mean_of_means / static_cast<double>(stack.views.size()));

    const Pedf pedf = make_pedf(mean);

    //ground resolution, so the output is shaped like the scene 
    // SICD pixels are not square on the ground, so the output aspect ratio is
    // taken from the ground extent rather than the pixel counts. Same formula
    // as sarpys get_ground_resolution().
    const SicdGeometry geometry = read_sicd_geometry(xml);

    const double graze = degrees_to_radians(geometry.graze_ang);
    const double twist = degrees_to_radians(geometry.twist_ang);

    const double row_res = std::fabs(geometry.row_ss / std::cos(graze));
    const double col_res =
        std::sqrt(std::pow(std::tan(graze) * std::tan(twist) * geometry.row_ss, 2.0) +
                  std::pow(geometry.col_ss / std::cos(twist), 2.0));

    const OutputSize out =
        calc_size(static_cast<float>(stack.n_rows * row_res),
                  static_cast<float>(stack.n_cols * col_res), options.size);

    // second pass: remap and resample 
    const auto sample = [&stack, &pedf](uint32_t row, uint32_t col,
                                        int channel) {
        (void)channel;  // only one
        return static_cast<float>(pedf.remap(amplitude(stack.at(row, col))));
    };

    const std::vector<uint8_t> pixels = resample(
        sample, 1, stack.n_rows, stack.n_cols, out.rows, out.cols);

    const std::string path = output_path(options, input_path);
    write_png(path, out.cols, out.rows, 1, pixels);
    return path;
}

static std::string process_file(const std::string &input_path,
                                const Options &options) {

    FileReader reader(input_path);
    const Nitf nitf = read_nitf(reader);

    if (nitf.image_segments.empty()) {
        throw std::runtime_error("file has no image segments");
    }

    std::vector<uint8_t> xml;
    bool is_sicd = false;
    if (!nitf.des_segments.empty()) {
        xml = read_segment_bytes(reader, nitf.des_segments.front().data_offset,
                                 nitf.des_segments.front().data_size);
        is_sicd = looks_like_sicd(xml);
    }

    if (is_sicd) {
        return make_sicd(reader, nitf, options, input_path, xml);
    }

    const ImageRep irep = nitf.image_segments.front().header.irep;
    switch (irep) {
    case ImageRep::MONO:
        return make_mono(reader, nitf, options, input_path);
    case ImageRep::RGB:
        return make_rgb(reader, nitf, options, input_path);
    // case ImageRep::RGBLUT:
    //     return make_rgb_lut(reader, nitf, options, input_path);
    default:
        throw std::runtime_error(
            std::string("image representation IREP=") + image_rep_name(irep) +
            " is not supported");
    }
}

static void open_in_viewer(const std::string &path) {
    // Hand the file to whatever the desktop uses for PNGs.
    const std::string command = "xdg-open '" + path + "' >/dev/null 2>&1 &";
    const int status = std::system(command.c_str());
    (void)status;
}

int main(int argc, char *argv[]) {
    argparse::ArgumentParser program("nitv", "0.1.0");
    program.add_description("Write out the image data from a NITF file.");

    program.add_argument("input")
        .help("Input NITF file(s)")
        .nargs(argparse::nargs_pattern::at_least_one);

    program.add_argument("--output")
        .help("Output folder")
        .default_value(std::string("."));

    program.add_argument("--size")
        .help("sqrt(num-pixels), e.g. --size 50 gives a 50^2 pixel image. "
              "The input aspect ratio is preserved. 0 means native resolution.")
        .default_value(std::string("256"));

    program.add_argument("--open")
        .help("Open each PNG in the default image viewer when it is written")
        .flag();

    try {
        program.parse_args(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n\n" << program;
        return 2;
    }

    Options options;
    try {
        options.output_dir = program.get<std::string>("--output");
        options.size = static_cast<uint32_t>(
            std::stoul(program.get<std::string>("--size")));
        options.open_when_done = program.get<bool>("--open");

        std::filesystem::create_directories(options.output_dir);
    } catch (const std::exception &error) {
        std::cerr << "nitv: " << error.what() << "\n";
        return 2;
    }

    const auto inputs = program.get<std::vector<std::string>>("input");

    // I think I did this because I wanted to 
    // keep going after a failure so that one bad file in a batch does not stop the rest but I don't think I'm gonna do stuff in batches anytime soon
    // but gonna keep it in anyways since it works
    int failures = 0;
    for (const std::string &input : inputs) {
        try {
            const std::string written = process_file(input, options);
            std::cout << "wrote " << written << "\n";
            if (options.open_when_done) {
                open_in_viewer(written);
            }
        } catch (const std::exception &error) {
            std::cerr << "nitv: " << input << ": " << error.what() << "\n";
            ++failures;
        }
    }

    return failures == 0 ? 0 : 1;
}
