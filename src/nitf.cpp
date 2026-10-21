#include "nitf.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>

// skip the securtiy section so we don't gotta 
//parse that stuff
static const int SECURITY_BLOCK_BYTES = 167;


FileReader::FileReader(const std::string &path) : path_(path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) {
        throw std::runtime_error("cannot open " + path + ": " +
                                 std::strerror(errno));
    }
    struct stat info {};
    if (::fstat(fd_, &info) != 0) {
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error("cannot stat " + path + ": " +
                                 std::strerror(errno));
    }
    file_size_ = static_cast<uint64_t>(info.st_size);
}

FileReader::~FileReader() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void FileReader::read_raw(void *dst, std::size_t count, const char *name) {
    // pread() rather than read() so that the cursor we track is the only
    // cursor and nothing else can move it out from under
    std::size_t done = 0;
    while (done < count) {
        const ssize_t got = ::pread(fd_, static_cast<char *>(dst) + done,
                                    count - done,
                                    static_cast<off_t>(pos_ + done));
        if (got < 0) {
            throw std::runtime_error(std::string("read error on field ") +
                                     name + ": " + std::strerror(errno));
        }
        if (got == 0) {
            throw std::runtime_error(
                std::string("unexpected end of file reading field ") + name +
                " at offset " + std::to_string(pos_ + done));
        }
        done += static_cast<std::size_t>(got);
    }
    pos_ += count;
}

// strip leading and trailing spaces, tabs and NULs. NITF pads with spaces, but
// some writers pad with NULs cuz idk
static std::string trim(const std::string &text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    const auto is_pad = [](char c) {
        return c == ' ' || c == '\t' || c == '\0' || c == '\r' || c == '\n';
    };
    while (begin < end && is_pad(text[begin])) {
        ++begin;
    }
    while (end > begin && is_pad(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string FileReader::read_string(int count, const char *name) {
    std::string buffer(static_cast<std::size_t>(count), '\0');
    read_raw(&buffer[0], buffer.size(), name);
    return trim(buffer);
}

uint64_t FileReader::read_uint(int count, const char *name) {
    std::string buffer(static_cast<std::size_t>(count), '\0');
    read_raw(&buffer[0], buffer.size(), name);
    const std::string text = trim(buffer);

    // A blank field means "not present", which the standard treats as zero.
    if (text.empty()) {
        return 0;
    }

    uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {

            // Doing what vaughn did in the rust crate. fall back to the default
            // rather than failing the whole file.
            return 0;
        }
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }

    return value;
}


ImageRep parse_image_rep(const std::string &text) {
    if (text == "MONO")     return ImageRep::MONO;
    if (text == "RGB")      return ImageRep::RGB;
    if (text == "RGB/LUT")  return ImageRep::RGBLUT;
    if (text == "MULTI")    return ImageRep::MULTI;
    if (text == "NODISPLY") return ImageRep::NODISPLY;
    if (text == "")         return ImageRep::NVECTOR;
    if (text == "POLAR")    return ImageRep::POLAR;
    if (text == "VPH")      return ImageRep::VPH;
    if (text == "YCbCr601") return ImageRep::YCbCr601;
    return ImageRep::UNKNOWN;
}

const char *image_rep_name(ImageRep value) {
    switch (value) {
    case ImageRep::MONO:     return "MONO";
    case ImageRep::RGB:      return "RGB";
    case ImageRep::RGBLUT:   return "RGB/LUT";
    case ImageRep::MULTI:    return "MULTI";
    case ImageRep::NODISPLY: return "NODISPLY";
    case ImageRep::NVECTOR:  return "NVECTOR";
    case ImageRep::POLAR:    return "POLAR";
    case ImageRep::VPH:      return "VPH";
    case ImageRep::YCbCr601: return "YCbCr601";
    case ImageRep::UNKNOWN:  return "UNKNOWN";
    }
    return "UNKNOWN";
}

ImageMode parse_image_mode(const std::string &text) {
    if (text == "B") return ImageMode::B;
    if (text == "P") return ImageMode::P;
    if (text == "R") return ImageMode::R;
    if (text == "S") return ImageMode::S;
    return ImageMode::UNKNOWN;
}

const char *image_mode_name(ImageMode value) {
    switch (value) {
    case ImageMode::B:       return "B";
    case ImageMode::P:       return "P";
    case ImageMode::R:       return "R";
    case ImageMode::S:       return "S";
    case ImageMode::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

PixelValueType parse_pixel_value_type(const std::string &text) {
    if (text == "INT") return PixelValueType::INT;
    if (text == "B")   return PixelValueType::BILEVEL;
    if (text == "SI")  return PixelValueType::SI;
    if (text == "R")   return PixelValueType::R;
    if (text == "C")   return PixelValueType::C;
    return PixelValueType::UNKNOWN;
}

const char *pixel_value_type_name(PixelValueType value) {
    switch (value) {
    case PixelValueType::INT:     return "INT";
    case PixelValueType::BILEVEL: return "B";
    case PixelValueType::SI:      return "SI";
    case PixelValueType::R:       return "R";
    case PixelValueType::C:       return "C";
    case PixelValueType::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

bool ic_has_comrat(const std::string &ic) {
    return ic == "C1" || ic == "C3" || ic == "C4" || ic == "C5" ||
           ic == "C8" || ic == "M1" || ic == "M3" || ic == "M4" ||
           ic == "M5" || ic == "M8" || ic == "I1";
}

//eawch row is a subheader length followed by
// a data length nd the widths differ per segment type
static std::vector<SegmentTableEntry>
read_segment_table(FileReader &reader, int count, int subheader_width,
                   int item_width, const char *subheader_name,
                   const char *item_name) {
    std::vector<SegmentTableEntry> table;
    table.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        SegmentTableEntry entry;
        entry.subheader_size = reader.read_uint(subheader_width, subheader_name);
        entry.item_size = reader.read_uint(item_width, item_name);
        table.push_back(entry);
    }
    return table;
}

// everything here is sequential prolly can make better in future and read_raw() under the hood moves the 
// position pointer inside of the header
static FileHeader read_file_header(FileReader &reader) {
    FileHeader header;

    header.fhdr = reader.read_string(4, "FHDR");
    header.fver = reader.read_string(5, "FVER");
    if (header.fhdr != "NITF") {
        throw std::runtime_error("not a NITF file (FHDR is \"" + header.fhdr +
                                 "\", expected \"NITF\")");
    }
    if (header.fver != "02.10") {
    }

    header.clevel = static_cast<int>(reader.read_uint(2, "CLEVEL"));
    header.stype = reader.read_string(4, "STYPE");
    header.ostaid = reader.read_string(10, "OSTAID");
    header.fdt = reader.read_string(14, "FDT");
    header.ftitle = reader.read_string(80, "FTITLE");

    reader.seek(reader.pos() + SECURITY_BLOCK_BYTES);  // skip security block

    header.fscop = static_cast<uint32_t>(reader.read_uint(5, "FSCOP"));
    header.fscpys = static_cast<uint32_t>(reader.read_uint(5, "FSCPYS"));
    header.encryp = reader.read_string(1, "ENCRYP");
    reader.read_raw(header.fbkgc, 3, "FBKGC");  // raw bytes, not ASCII
    header.oname = reader.read_string(24, "ONAME");
    header.ophone = reader.read_string(18, "OPHONE");
    header.fl = reader.read_uint(12, "FL");
    header.hl = static_cast<uint32_t>(reader.read_uint(6, "HL"));

    header.numi = static_cast<int>(reader.read_uint(3, "NUMI"));
    header.image_table =
        read_segment_table(reader, header.numi, 6, 10, "LISH", "LI");

    header.nums = static_cast<int>(reader.read_uint(3, "NUMS"));
    header.graphic_table =
        read_segment_table(reader, header.nums, 4, 6, "LSSH", "LS");

    header.numx = static_cast<int>(reader.read_uint(3, "NUMX"));

    header.numt = static_cast<int>(reader.read_uint(3, "NUMT"));
    header.text_table =
        read_segment_table(reader, header.numt, 4, 5, "LTSH", "LT");

    header.numdes = static_cast<int>(reader.read_uint(3, "NUMDES"));
    header.des_table =
        read_segment_table(reader, header.numdes, 4, 9, "LDSH", "LD");

    header.numres = static_cast<int>(reader.read_uint(3, "NUMRES"));
    header.res_table =
        read_segment_table(reader, header.numres, 4, 7, "LRESH", "LRE");

    // UDHDL and XHDL include the 3 bytes of their own overflow field, so the
    // payload is (length - 3) bytes. 
    // DESSHL and RESSHL further down
    // do NOT work this way cause they are the payload length on its own.
    header.udhdl = static_cast<uint32_t>(reader.read_uint(5, "UDHDL"));
    if (header.udhdl != 0) {
        header.udhofl = static_cast<int>(reader.read_uint(3, "UDHOFL"));
        header.udhd.resize(header.udhdl - 3);
        reader.read_raw(header.udhd.data(), header.udhd.size(), "UDHD");
    }

    header.xhdl = static_cast<uint32_t>(reader.read_uint(5, "XHDL"));
    if (header.xhdl != 0) {
        header.xhdlofl = static_cast<int>(reader.read_uint(3, "XHDLOFL"));
        header.xhd.resize(header.xhdl - 3);
        reader.read_raw(header.xhd.data(), header.xhd.size(), "XHD");
    }

    return header;
}


static std::vector<Band> read_bands(FileReader &reader, uint32_t band_count) {
    std::vector<Band> bands;
    bands.reserve(band_count);

    for (uint32_t i = 0; i < band_count; ++i) {
        Band band;
        band.irepband = reader.read_string(2, "IREPBAND");
        band.isubcat = reader.read_string(6, "ISUBCAT");
        band.ifc = reader.read_string(1, "IFC");
        band.imflt = reader.read_string(3, "IMFLT");
        band.nluts = static_cast<int>(reader.read_uint(1, "NLUTS"));

        if (band.nluts != 0) {
            band.nelut = static_cast<int>(reader.read_uint(5, "NELUT"));
            band.lutd.resize(static_cast<std::size_t>(band.nluts));
            for (auto &lut : band.lutd) {
                lut.resize(static_cast<std::size_t>(band.nelut));
                reader.read_raw(lut.data(), lut.size(), "LUTD");
            }
        }
        bands.push_back(std::move(band));
    }
    return bands;
}
// sequential as well, like the file header reader
static ImageHeader read_image_header(FileReader &reader) {
    ImageHeader header;

    header.im = reader.read_string(2, "IM");
    header.iid1 = reader.read_string(10, "IID1");
    header.idatim = reader.read_string(14, "IDATIM");
    header.tgtid = reader.read_string(17, "TGTID");
    header.iid2 = reader.read_string(80, "IID2");

    reader.seek(reader.pos() + SECURITY_BLOCK_BYTES);  // skip security block

    header.encryp = reader.read_string(1, "ENCRYP");
    header.isorce = reader.read_string(42, "ISORCE");
    header.nrows = static_cast<uint32_t>(reader.read_uint(8, "NROWS"));
    header.ncols = static_cast<uint32_t>(reader.read_uint(8, "NCOLS"));

    header.pvtype_text = reader.read_string(3, "PVTYPE");
    header.pvtype = parse_pixel_value_type(header.pvtype_text);

    header.irep_text = reader.read_string(8, "IREP");
    header.irep = parse_image_rep(header.irep_text);

    header.icat = reader.read_string(8, "ICAT");
    header.abpp = static_cast<int>(reader.read_uint(2, "ABPP"));
    header.pjust = reader.read_string(1, "PJUST");

    // IGEOLO is present only when ICORDS is not blank
    header.icords = reader.read_string(1, "ICORDS");
    if (!header.icords.empty()) {
        header.igeolo = reader.read_string(60, "IGEOLO");
    }

    header.nicom = static_cast<int>(reader.read_uint(1, "NICOM"));
    for (int i = 0; i < header.nicom; ++i) {
        header.icoms.push_back(reader.read_string(80, "ICOM"));
    }

    // COMRAT is present only for some compression codes
    header.ic = reader.read_string(2, "IC");
    if (ic_has_comrat(header.ic)) {
        header.comrat = reader.read_string(4, "COMRAT");
    }

    // NBANDS is a single digit. If the image has 10 or more bands it is 0 and
    // the real count lives in the 5-digit XBANDS field that follows. Stupid shit
    // but maybe I'm stupid and this has a good reason.
    header.nbands = static_cast<int>(reader.read_uint(1, "NBANDS"));
    if (header.nbands != 0) {
        header.bands = read_bands(reader, static_cast<uint32_t>(header.nbands));
    } else {
        header.xbands = static_cast<uint32_t>(reader.read_uint(5, "XBANDS"));
        header.bands = read_bands(reader, header.xbands);
    }

    header.isync = reader.read_string(1, "ISYNC");
    header.imode_text = reader.read_string(1, "IMODE");
    header.imode = parse_image_mode(header.imode_text);

    header.nbpr = static_cast<uint32_t>(reader.read_uint(4, "NBPR"));
    header.nbpc = static_cast<uint32_t>(reader.read_uint(4, "NBPC"));
    header.nppbh = static_cast<uint32_t>(reader.read_uint(4, "NPPBH"));
    header.nppbv = static_cast<uint32_t>(reader.read_uint(4, "NPPBV"));
    header.nbpp = static_cast<int>(reader.read_uint(2, "NBPP"));

    header.idlvl = static_cast<int>(reader.read_uint(3, "IDLVL"));
    header.ialvl = static_cast<int>(reader.read_uint(3, "IALVL"));
    header.iloc = reader.read_string(10, "ILOC");
    header.imag = reader.read_string(4, "IMAG");

    header.udidl = static_cast<uint32_t>(reader.read_uint(5, "UDIDL"));
    if (header.udidl != 0) {
        header.udofl = static_cast<int>(reader.read_uint(3, "UDOFL"));
        header.udid.resize(header.udidl - 3);
        reader.read_raw(header.udid.data(), header.udid.size(), "UDID");
    }

    header.ixshdl = static_cast<uint32_t>(reader.read_uint(5, "IXSHDL"));
    if (header.ixshdl != 0) {
        header.ixsofl = static_cast<int>(reader.read_uint(3, "IXSOFL"));
        header.ixshd.resize(header.ixshdl - 3);
        reader.read_raw(header.ixshd.data(), header.ixshd.size(), "IXSHD");
    }

    return header;
}


static DesHeader read_des_header(FileReader &reader) {
    DesHeader header;

    header.de = reader.read_string(2, "DE");
    header.desid = reader.read_string(25, "DESID");
    header.desver = static_cast<int>(reader.read_uint(2, "DESVER"));

    reader.seek(reader.pos() + SECURITY_BLOCK_BYTES);  // skip security block

    if (header.desid == "TRE_OVERFLOW") {
        header.desoflw = reader.read_string(6, "DESOFLW");
        header.desitem = static_cast<int>(reader.read_uint(3, "DESITEM"));
    }

    // Unlike UDHDL/XHDL/UDIDL/IXSHDL above, DESSHL is the payload length on
    // its own -- there is no 3-byte overflow field to account for. Again,
    // probably reason but for now just gotta take it like this
    header.desshl = static_cast<int>(reader.read_uint(4, "DESSHL"));
    if (header.desshl != 0) {
        header.desshf.resize(static_cast<std::size_t>(header.desshl));
        reader.read_raw(header.desshf.data(), header.desshf.size(), "DESSHF");
    }

    return header;
}


// The file headers segment table already told us how long each subheader is.
// If the parse disagrees, every offset after this point is wrong, so say so
// loudly and so this check catches field-width mistakes immediately.
static void check_subheader_length(const char *kind, int index,
                                   uint64_t parsed, uint64_t expected) {
    if (parsed != expected) {
        std::fprintf(stderr,
                     "nitv: warning: %s subheader %d parsed as %llu bytes but "
                     "the file header's segment table says %llu; every offset "
                     "after this point is suspect\n",
                     kind, index, static_cast<unsigned long long>(parsed),
                     static_cast<unsigned long long>(expected));
    }
}

Nitf read_nitf(FileReader &reader) {
    Nitf nitf;
    reader.seek(0);
    nitf.file_header = read_file_header(reader);

    //image segments
    for (int i = 0; i < nitf.file_header.numi; ++i) {
        const SegmentTableEntry &entry = nitf.file_header.image_table[i];
        const uint64_t header_offset = reader.pos();

        ImageSegment segment;
        segment.header = read_image_header(reader);
        check_subheader_length("image", i, reader.pos() - header_offset,
                               entry.subheader_size);

        // trust the segment table for the data offset rather than wherever the parse happened to stop
        segment.data_offset = header_offset + entry.subheader_size;
        segment.data_size = entry.item_size;
        reader.seek(segment.data_offset + segment.data_size);

        nitf.image_segments.push_back(std::move(segment));
    }

    // TODO : do graphing, text, and reserved extensions at some point
    for (const SegmentTableEntry &entry : nitf.file_header.graphic_table) {
        reader.seek(reader.pos() + entry.subheader_size + entry.item_size);
    }
    for (const SegmentTableEntry &entry : nitf.file_header.text_table) {
        reader.seek(reader.pos() + entry.subheader_size + entry.item_size);
    }

    // data extension segment
    for (int i = 0; i < nitf.file_header.numdes; ++i) {
        const SegmentTableEntry &entry = nitf.file_header.des_table[i];
        const uint64_t header_offset = reader.pos();

        DesSegment segment;
        segment.header = read_des_header(reader);
        check_subheader_length("DES", i, reader.pos() - header_offset,
                               entry.subheader_size);

        segment.data_offset = header_offset + entry.subheader_size;
        segment.data_size = entry.item_size;
        reader.seek(segment.data_offset + segment.data_size);

        nitf.des_segments.push_back(std::move(segment));
    }

    for (const SegmentTableEntry &entry : nitf.file_header.res_table) {
        reader.seek(reader.pos() + entry.subheader_size + entry.item_size);
    }

    return nitf;
}

std::vector<uint8_t> read_segment_bytes(FileReader &reader, uint64_t offset,
                                        uint64_t size) {
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    if (size != 0) {
        const uint64_t saved = reader.pos();
        reader.seek(offset);
        reader.read_raw(bytes.data(), bytes.size(), "segment data");
        reader.seek(saved);
    }
    return bytes;
}
