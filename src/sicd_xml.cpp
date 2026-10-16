#include "sicd_xml.hpp"

#include <cstdlib>
#include <stdexcept>

// All thiss is half assed because I can just use six/nitro for
// xml stuff but i'm handrolling this just to get stuff off the ground
// TODO: Interface this with six/nitro?? Is that overkill? Maybe if I turn this into an actual IFP it isnt

static bool find_element(const std::string &xml, const std::string &tag,
                         std::size_t search_begin, std::size_t search_end,
                         std::size_t *content_begin,
                         std::size_t *content_end) {
    const std::string open = "<" + tag;
    const std::string close = "</" + tag + ">";

    std::size_t at = search_begin;
    while (true) {
        at = xml.find(open, at);
        if (at == std::string::npos || at >= search_end) {
            return false;
        }


        const std::size_t after_name = at + open.size();
        if (after_name >= xml.size()) {
            return false;
        }
        const char next = xml[after_name];
        if (next != '>' && next != ' ' && next != '\t' && next != '\n' &&
            next != '\r' && next != '/') {
            at = after_name;
            continue;
        }
        const std::size_t open_end = xml.find('>', at);
        if (open_end == std::string::npos || open_end >= search_end) {
            return false;
        }

        const std::size_t close_at = xml.find(close, open_end + 1);
        if (close_at == std::string::npos || close_at > search_end) {
            return false;
        }

        *content_begin = open_end + 1;
        *content_end = close_at;
        return true;
    }
}

static bool read_double(const std::string &xml, const std::string &tag,
                        std::size_t search_begin, std::size_t search_end,
                        double *out) {
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!find_element(xml, tag, search_begin, search_end, &begin, &end)) {
        return false;
    }
    const std::string text = xml.substr(begin, end - begin);
    *out = std::strtod(text.c_str(), nullptr);
    return true;
}



std::string first_element_name(const std::vector<uint8_t> &xml) {
    const std::string text(xml.begin(), xml.end());

    std::size_t at = 0;
    while (at < text.size()) {
        at = text.find('<', at);
        if (at == std::string::npos) {
            return "";
        }

        if (text.compare(at, 2, "<?") == 0) {  // <?xml ... ?>
            at = text.find("?>", at);
            if (at == std::string::npos) {
                return "";
            }
            at += 2;
            continue;
        }
        if (text.compare(at, 4, "<!--") == 0) {  // <!-- comment -->
            at = text.find("-->", at);
            if (at == std::string::npos) {
                return "";
            }
            at += 3;
            continue;
        }
        if (text.compare(at, 2, "<!") == 0) {  // <!DOCTYPE ...>
            at = text.find('>', at);
            if (at == std::string::npos) {
                return "";
            }
            at += 1;
            continue;
        }

        std::size_t name_end = at + 1;
        while (name_end < text.size()) {
            const char c = text[name_end];
            if (c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                c == '/') {
                break;
            }
            ++name_end;
        }
        return text.substr(at + 1, name_end - (at + 1));
    }
    return "";
}

bool looks_like_sicd(const std::vector<uint8_t> &xml) {
    const std::string name = first_element_name(xml);
    return name.find("SICD") != std::string::npos;
}

SicdGeometry read_sicd_geometry(const std::vector<uint8_t> &xml) {
    const std::string text(xml.begin(), xml.end());
    SicdGeometry geometry;

    // Grid/Row/SS and Grid/Col/SS
    std::size_t grid_begin = 0;
    std::size_t grid_end = 0;
    if (!find_element(text, "Grid", 0, text.size(), &grid_begin, &grid_end)) {
        throw std::runtime_error("SICD XML has no <Grid> element");
    }

    std::size_t row_begin = 0;
    std::size_t row_end = 0;
    if (!find_element(text, "Row", grid_begin, grid_end, &row_begin, &row_end)) {
        throw std::runtime_error("SICD XML has no <Grid><Row> element");
    }
    if (!read_double(text, "SS", row_begin, row_end, &geometry.row_ss)) {
        throw std::runtime_error("SICD XML has no <Grid><Row><SS> value");
    }

    std::size_t col_begin = 0;
    std::size_t col_end = 0;
    if (!find_element(text, "Col", grid_begin, grid_end, &col_begin, &col_end)) {
        throw std::runtime_error("SICD XML has no <Grid><Col> element");
    }
    if (!read_double(text, "SS", col_begin, col_end, &geometry.col_ss)) {
        throw std::runtime_error("SICD XML has no <Grid><Col><SS> value");
    }




    //SCPCOA/GrazeAng and SCPCOA/TwistAng 
    std::size_t scpcoa_begin = 0;
    std::size_t scpcoa_end = 0;
    if (!find_element(text, "SCPCOA", 0, text.size(), &scpcoa_begin,
                      &scpcoa_end)) {
        throw std::runtime_error("SICD XML has no <SCPCOA> element");
    }
    if (!read_double(text, "GrazeAng", scpcoa_begin, scpcoa_end,
                     &geometry.graze_ang)) {
        throw std::runtime_error("SICD XML has no <SCPCOA><GrazeAng> value");
    }
    if (!read_double(text, "TwistAng", scpcoa_begin, scpcoa_end,
                     &geometry.twist_ang)) {
        throw std::runtime_error("SICD XML has no <SCPCOA><TwistAng> value");
    }

    return geometry;
}
