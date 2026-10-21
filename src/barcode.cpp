#include "barcode.hpp"

#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
constexpr std::array<std::string_view, 107> patterns{
    "212222","222122","222221","121223","121322","131222","122213","122312","132212","221213",
    "221312","231212","112232","122132","122231","113222","123122","123221","223211","221132",
    "221231","213212","223112","312131","311222","321122","321221","312212","322112","322211",
    "212123","212321","232121","111323","131123","131321","112313","132113","132311","211313",
    "231113","231311","112133","112331","132131","113123","113321","133121","313121","211331",
    "231131","213113","213311","213131","311123","311321","331121","312113","312311","332111",
    "314111","221411","431111","111224","111422","121124","121421","141122","141221","112214",
    "112412","122114","122411","142112","142211","241211","221114","413111","241112","134111",
    "111242","121142","121241","114212","124112","124211","411212","421112","421211","212141",
    "214121","412121","111143","111341","131141","114113","114311","411113","411311","113141",
    "114131","311141","411131","211412","211214","211232","2331112"
};

std::string escapeXml(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result += c;
        }
    }
    return result;
}
}

std::string barcodeSvg(const std::string& value, int height, bool showText) {
    return barcodeSvg(value, 1.6, height, showText);
}

std::string barcodeSvg(const std::string& value, double moduleWidth, int height, bool showText) {
    if (value.empty()) return {};
    if (value.size() > 80) throw std::runtime_error("La valeur du code-barres est trop longue (80 caractères maximum).");
    if (!std::isfinite(moduleWidth) || moduleWidth <= 0 || height < 16 || height > 400)
        throw std::runtime_error("Dimensions de code-barres invalides.");
    std::vector<int> codes;
    codes.reserve(value.size() + 3);
    int checksum = 104;
    for (std::size_t i = 0; i < value.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (c < 32 || c > 126) throw std::runtime_error("Ce code-barres contient un caractère non pris en charge.");
        const int code = static_cast<int>(c) - 32;
        codes.push_back(code);
        checksum += code * static_cast<int>(i + 1);
    }
    codes.push_back(checksum % 103);
    codes.push_back(106);
    int modules = 0;
    auto addWidth = [&modules](std::string_view pattern) {
        for (const char digit : pattern) modules += digit - '0';
    };
    addWidth(patterns[104]);
    for (const int code : codes) addWidth(patterns[static_cast<std::size_t>(code)]);
    constexpr int quiet = 10;
    const double textHeight = showText ? 14.0 : 0.0;
    const double totalWidth = static_cast<double>(modules + quiet * 2) * moduleWidth;
    const double totalHeight = static_cast<double>(height) + textHeight;
    std::ostringstream svg;
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << totalWidth << ' ' << totalHeight
        << "\" width=\"" << totalWidth << "\" height=\"" << totalHeight << "\" role=\"img\" aria-label=\""
        << escapeXml(value) << "\">";
    double x = quiet * moduleWidth;
    auto drawPattern = [&](std::string_view pattern) {
        bool bar = true;
        for (const char digit : pattern) {
            const double width = static_cast<double>(digit - '0') * moduleWidth;
            if (bar) svg << "<rect x=\"" << x << "\" y=\"0\" width=\"" << width << "\" height=\"" << height << "\" fill=\"#111111\"/>";
            x += width;
            bar = !bar;
        }
    };
    drawPattern(patterns[104]);
    for (const int code : codes) drawPattern(patterns[static_cast<std::size_t>(code)]);
    if (showText) {
        svg << "<text x=\"" << totalWidth / 2.0 << "\" y=\"" << (height + 12) << "\" text-anchor=\"middle\" "
            << "font-family=\"monospace\" font-size=\"11\" letter-spacing=\"1\" fill=\"#111111\">" << escapeXml(value) << "</text>";
    }
    svg << "</svg>";
    return svg.str();
}
