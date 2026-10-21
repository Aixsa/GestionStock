#pragma once

#include <filesystem>
#include <string>

inline std::filesystem::path pathFromUtf8(const std::string& value) {
#ifdef _WIN32
    std::u8string encoded;
    encoded.reserve(value.size());
    for (const unsigned char byte : value) encoded.push_back(static_cast<char8_t>(byte));
    return std::filesystem::path(encoded);
#else
    return std::filesystem::path(value);
#endif
}

inline std::string pathToUtf8(const std::filesystem::path& value) {
    const auto encoded = value.u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}
