#pragma once

#include <cstdint>
#include <string>
#include <vector>


// TODO: Add a comment explaining all enums and field keys. Also add more field keys
// TODO: Maybe forget about that since might employ six/nitro
class FileReader {
public:
    explicit FileReader(const std::string &path);
    ~FileReader();

    FileReader(const FileReader &) = delete;
    FileReader &operator=(const FileReader &) = delete;

    uint64_t pos() const { return pos_; }
    void seek(uint64_t position) { pos_ = position; }

    int fd() const { return fd_; }
    uint64_t file_size() const { return file_size_; }
    void read_raw(void *dst, std::size_t count, const char *name);
    std::string read_string(int count, const char *name);

    uint64_t read_uint(int count, const char *name);

private:
    int fd_ = -1;
    uint64_t pos_ = 0;
    uint64_t file_size_ = 0;
    std::string path_;
};



// IREP. what the pixels are meant to look like.
enum class ImageRep {
    MONO,      // single band greyscale
    RGB,       // three bands, true colour
    RGBLUT,    // single band index into a colour table
    MULTI,     // multiband
    NODISPLY,  // not intended for display (SICD complex data uses this)
    NVECTOR,
    POLAR,
    VPH,
    YCbCr601,
    UNKNOWN,
};
ImageRep parse_image_rep(const std::string &text);
const char *image_rep_name(ImageRep value);

// IMODE. How bands are interleaved in the data.
enum class ImageMode {
    B,        // band interleaved by block
    P,        // band interleaved by pixel  (R,G,B,R,G,B,...)
    R,        // band interleaved by row
    S,        // band sequential
    UNKNOWN,
};
ImageMode parse_image_mode(const std::string &text);
const char *image_mode_name(ImageMode value);

// PVTYPE. The numeric type of a single band sample.
enum class PixelValueType {
    INT,      // unsigned integer
    BILEVEL,  // one bit per pixel
    SI,       // two's complement signed integer
    R,        // IEEE float
    C,        // complex float (real then imaginary)
    UNKNOWN,
};
PixelValueType parse_pixel_value_type(const std::string &text);
const char *pixel_value_type_name(PixelValueType value);

// IC is the compression code. We keep it as a plain string because the only
// thing we do with it is check whether it is "NC" (uncompressed, no mask).
// This helper says whether a COMRAT field follows it in the subheader.
bool ic_has_comrat(const std::string &ic);

// ---------------------------------------------------------------------------


// One bands metadata, including its colour table if it has one.
struct Band {
    std::string irepband;  // IREPBAND: "R", "G", "B", "LU", "M", ...
    std::string isubcat;
    std::string ifc;
    std::string imflt;
    int nluts = 0;   // NLUTS:  number of look-up tables for this band
    int nelut = 0;   // NELUT:  entries per look-up table
    // lutd[i] has nelut raw bytes. For RGB/LUT imagery there are 3 of these:
    // the red, green and blue output values for each possible pixel index.
    std::vector<std::vector<uint8_t>> lutd;
};

struct ImageHeader {
    std::string im;      // IM, always "IM"
    std::string iid1;
    std::string idatim;
    std::string tgtid;
    std::string iid2;
    std::string encryp;
    std::string isorce;

    // The number of significant rows and columns. Blocked data is padded out
    // to whole blocks, so the stored data can be larger than this.
    uint32_t nrows = 0;
    uint32_t ncols = 0;

    std::string pvtype_text;
    PixelValueType pvtype = PixelValueType::UNKNOWN;

    std::string irep_text;
    ImageRep irep = ImageRep::UNKNOWN;

    std::string icat;
    int abpp = 0;          // ABPP: actual bits per pixel per band
    std::string pjust;
    std::string icords;
    std::string igeolo;    // present only when ICORDS is not blank

    int nicom = 0;
    std::vector<std::string> icoms;

    std::string ic;        // IC: compression code; "NC" means uncompressed
    std::string comrat;    // present only for compressed data

    int nbands = 0;        // NBANDS; 0 means "look at XBANDS instead"
    uint32_t xbands = 0;
    std::vector<Band> bands;

    std::string isync;

    std::string imode_text;
    ImageMode imode = ImageMode::UNKNOWN;

    // The block grid. nbpr/nbpc blocks of nppbh x nppbv pixels each.
    uint32_t nbpr = 0;     // blocks per row    (i.e. number of block columns)
    uint32_t nbpc = 0;     // blocks per column (i.e. number of block rows)
    uint32_t nppbh = 0;    // pixels per block, horizontal
    uint32_t nppbv = 0;    // pixels per block, vertical

    int nbpp = 0;          // NBPP: bits per pixel per band, as stored
    int idlvl = 0;
    int ialvl = 0;
    std::string iloc;
    std::string imag;

    uint32_t udidl = 0;
    int udofl = 0;
    std::vector<uint8_t> udid;

    uint32_t ixshdl = 0;
    int ixsofl = 0;
    std::vector<uint8_t> ixshd;
};

struct DesHeader {
    std::string de;      // DE, always "DE"
    std::string desid;   // e.g. "XML_DATA_CONTENT"
    int desver = 0;
    std::string desoflw; // present only when DESID is "TRE_OVERFLOW"
    int desitem = 0;
    int desshl = 0;
    std::vector<uint8_t> desshf;
};


struct SegmentTableEntry {
    uint64_t subheader_size = 0;
    uint64_t item_size = 0;
};

struct FileHeader {
    std::string fhdr;   // "NITF"
    std::string fver;   // "02.10"
    int clevel = 0;
    std::string stype;
    std::string ostaid;
    std::string fdt;
    std::string ftitle;
    uint32_t fscop = 0;
    uint32_t fscpys = 0;
    std::string encryp;
    uint8_t fbkgc[3] = {0, 0, 0};
    std::string oname;
    std::string ophone;
    uint64_t fl = 0;    // total file length
    uint32_t hl = 0;    // file header length

    int numi = 0;
    std::vector<SegmentTableEntry> image_table;
    int nums = 0;
    std::vector<SegmentTableEntry> graphic_table;
    int numx = 0;
    int numt = 0;
    std::vector<SegmentTableEntry> text_table;
    int numdes = 0;
    std::vector<SegmentTableEntry> des_table;
    int numres = 0;
    std::vector<SegmentTableEntry> res_table;

    uint32_t udhdl = 0;
    int udhofl = 0;
    std::vector<uint8_t> udhd;
    uint32_t xhdl = 0;
    int xhdlofl = 0;
    std::vector<uint8_t> xhd;
};

struct ImageSegment {
    ImageHeader header;
    uint64_t data_offset = 0;
    uint64_t data_size = 0;
};

struct DesSegment {
    DesHeader header;
    uint64_t data_offset = 0;
    uint64_t data_size = 0;
};

struct Nitf {
    FileHeader file_header;
    std::vector<ImageSegment> image_segments;
    std::vector<DesSegment> des_segments;
};


Nitf read_nitf(FileReader &reader);
std::vector<uint8_t> read_segment_bytes(FileReader &reader, uint64_t offset,
                                        uint64_t size);
