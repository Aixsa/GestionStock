#include "security.hpp"

#include <glib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {
constexpr unsigned int iterations = 120000;

std::string hex(const unsigned char* bytes, std::size_t length) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < length; ++i) out << std::setw(2) << static_cast<unsigned int>(bytes[i]);
    return out.str();
}

std::vector<unsigned char> unhex(const std::string& value) {
    if (value.size() % 2 != 0) return {};
    std::vector<unsigned char> result(value.size() / 2);
    for (std::size_t i = 0; i < result.size(); ++i) {
        const auto digit=[](char c)->int { if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1; };
        const int high=digit(value[i*2]),low=digit(value[i*2+1]);
        if(high<0||low<0)return {};
        result[i]=static_cast<unsigned char>((high<<4)|low);
    }
    return result;
}

std::array<unsigned char, 32> derive(const std::string& password, const std::vector<unsigned char>& salt) {
    std::array<unsigned char,64> key{};
    if(password.size()>key.size()){
        GChecksum* hash=g_checksum_new(G_CHECKSUM_SHA256);g_checksum_update(hash,reinterpret_cast<const guchar*>(password.data()),static_cast<gssize>(password.size()));gsize keySize=32;g_checksum_get_digest(hash,key.data(),&keySize);g_checksum_free(hash);
    }else std::copy(password.begin(),password.end(),key.begin());
    std::array<unsigned char,64> innerPad{},outerPad{};
    for(std::size_t i=0;i<key.size();++i){innerPad[i]=static_cast<unsigned char>(key[i]^0x36);outerPad[i]=static_cast<unsigned char>(key[i]^0x5c);}
    GChecksum* inner=g_checksum_new(G_CHECKSUM_SHA256);GChecksum* outer=g_checksum_new(G_CHECKSUM_SHA256);
    if(!inner||!outer){if(inner)g_checksum_free(inner);if(outer)g_checksum_free(outer);throw std::runtime_error("Impossible de créer la fonction de dérivation du mot de passe.");}
    const auto hmac=[&](const unsigned char* data,std::size_t length){
        std::array<unsigned char,32> result{};std::array<unsigned char,32> innerDigest{};gsize digestLength=innerDigest.size();
        g_checksum_reset(inner);g_checksum_update(inner,innerPad.data(),innerPad.size());g_checksum_update(inner,data,static_cast<gssize>(length));g_checksum_get_digest(inner,innerDigest.data(),&digestLength);
        digestLength=result.size();g_checksum_reset(outer);g_checksum_update(outer,outerPad.data(),outerPad.size());g_checksum_update(outer,innerDigest.data(),innerDigest.size());g_checksum_get_digest(outer,result.data(),&digestLength);return result;
    };
    std::vector<unsigned char> first = salt;
    first.insert(first.end(), {0, 0, 0, 1});
    std::array<unsigned char, 32> u=hmac(first.data(),first.size());
    std::array<unsigned char, 32> result=u;
    for (unsigned int round = 1; round < iterations; ++round) {
        u=hmac(u.data(),u.size());
        for (std::size_t i = 0; i < result.size(); ++i) result[i] ^= u[i];
    }
    g_checksum_free(inner);g_checksum_free(outer);
    return result;
}
}

bool passwordMeetsMinimum(const std::string& password) {
    if(password.size()>4096||!g_utf8_validate(password.data(),static_cast<gssize>(password.size()),nullptr))return false;
    return g_utf8_strlen(password.data(),static_cast<gssize>(password.size()))>=8;
}

std::string hashPassword(const std::string& password) {
    std::array<unsigned char, 16> salt{};
    std::random_device random;
    std::uniform_int_distribution<unsigned int> byteDistribution(0,255);
    for (auto& byte : salt) byte = static_cast<unsigned char>(byteDistribution(random));
    std::vector<unsigned char> saltVector(salt.begin(), salt.end());
    const auto key = derive(password, saltVector);
    return "pbkdf2-sha256$" + std::to_string(iterations) + "$" + hex(salt.data(), salt.size()) + "$" + hex(key.data(), key.size());
}

bool verifyPassword(const std::string& password, const std::string& storedHash) {
    const auto first = storedHash.find('$');
    const auto second = first == std::string::npos ? first : storedHash.find('$', first + 1);
    const auto third = second == std::string::npos ? second : storedHash.find('$', second + 1);
    if (first == std::string::npos || second == std::string::npos || third == std::string::npos || storedHash.substr(0, first) != "pbkdf2-sha256") return false;
    unsigned int count{};
    try {
        std::size_t parsed=0;
        const auto rounds=std::stoul(storedHash.substr(first+1,second-first-1),&parsed);
        if(parsed!=second-first-1||rounds>std::numeric_limits<unsigned int>::max())return false;
        count=static_cast<unsigned int>(rounds);
    }
    catch (...) { return false; }
    if (count != iterations) return false;
    const auto salt = unhex(storedHash.substr(second + 1, third - second - 1));
    const auto expected = unhex(storedHash.substr(third + 1));
    if (salt.size() != 16 || expected.size() != 32) return false;
    const auto actual = derive(password, salt);
    unsigned char difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) difference |= static_cast<unsigned char>(expected[i] ^ actual[i]);
    return difference == 0;
}
