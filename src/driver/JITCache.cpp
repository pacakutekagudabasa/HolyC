#ifdef HCC_HAS_LLVM

#include "JITCache.h"

#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unistd.h>

namespace holyc {

// Build time पर CMake -D के through compiler version string bake हुई है — cache invalidation के लिए
#ifndef HCC_VERSION_STRING
#define HCC_VERSION_STRING "0.1"
#endif

// ---------------------------------------------------------------------------
// Minimal SHA-256 implementation (public domain, कोई external deps नहीं)
// ---------------------------------------------------------------------------

static uint32_t rotr32(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static const uint32_t kK[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

/// data[0..len) का SHA-256 compute करता है; 32-byte digest लौटाता है।
static std::array<uint8_t, 32> sha256(const uint8_t* data, size_t len) {
    uint32_t h[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19
    };

    // Padding — SHA-256 message को block boundary तक pad करो
    uint64_t bitlen = (uint64_t)len * 8;
    size_t padlen = (len % 64 < 56) ? (56 - len % 64) : (120 - len % 64);
    size_t total = len + padlen + 8;
    std::vector<uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    msg.resize(total, 0);
    for (int i = 7; i >= 0; --i)
        msg[total - 8 + (7 - i)] = (bitlen >> (i * 8)) & 0xff;

    for (size_t i = 0; i < total; i += 64) {
        uint32_t w[64];
        for (int j = 0; j < 16; ++j)
            w[j] = ((uint32_t)msg[i+j*4]<<24)|((uint32_t)msg[i+j*4+1]<<16)|
                   ((uint32_t)msg[i+j*4+2]<<8)|(uint32_t)msg[i+j*4+3];
        for (int j = 16; j < 64; ++j) {
            uint32_t s0 = rotr32(w[j-15],7)^rotr32(w[j-15],18)^(w[j-15]>>3);
            uint32_t s1 = rotr32(w[j-2],17)^rotr32(w[j-2],19)^(w[j-2]>>10);
            w[j] = w[j-16]+s0+w[j-7]+s1;
        }
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],hh=h[7];
        for (int j = 0; j < 64; ++j) {
            uint32_t S1  = rotr32(e,6)^rotr32(e,11)^rotr32(e,25);
            uint32_t ch  = (e&f)^(~e&g);
            uint32_t t1  = hh+S1+ch+kK[j]+w[j];
            uint32_t S0  = rotr32(a,2)^rotr32(a,13)^rotr32(a,22);
            uint32_t maj = (a&b)^(a&c)^(b&c);
            uint32_t t2  = S0+maj;
            hh=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a; h[1]+=b; h[2]+=c; h[3]+=d;
        h[4]+=e; h[5]+=f; h[6]+=g; h[7]+=hh;
    }

    std::array<uint8_t, 32> digest;
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j)
            digest[i*4+j] = (h[i] >> (24 - j*8)) & 0xff;
    return digest;
}

// ---------------------------------------------------------------------------
// JITCache implementation — JIT compilation cache के methods
// ---------------------------------------------------------------------------

JITCache::JITCache(const std::string& cacheDir) : cacheDir_(cacheDir) {}

/// HCC_CACHE_DIR env var से cache directory लौटाता है, या ~/.cache/hcc fallback।
std::string JITCache::defaultDir() {
    const char* envDir = std::getenv("HCC_CACHE_DIR");
    if (envDir && *envDir) return envDir;
    const char* home = std::getenv("HOME");
    if (!home) return "/tmp/hcc_cache";
    return std::string(home) + "/.cache/hcc";
}

/// dir (parents सहित) create करता है अगर exist नहीं करता; failure पर false लौटाता है।
bool JITCache::ensureDir(const std::string& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return !ec;
}

/// Cache key को (version | triple | source) के hex SHA-256 के रूप में compute करता है।
std::string JITCache::computeKey(const std::string& preprocessed_src,
                                  const std::string& triple) {
    std::string data = std::string(HCC_VERSION_STRING) + "|" + triple + "|" + preprocessed_src;
    auto digest = sha256(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t b : digest) oss << std::setw(2) << (unsigned)b;
    return oss.str();
}

/// दिए गए source और triple के लिए cached .bc file का path लौटाता है, या miss पर ""।
std::string JITCache::lookup(const std::string& preprocessed_src,
                              const std::string& triple) const {
    std::string key = computeKey(preprocessed_src, triple);
    std::string path = cacheDir_ + "/" + key + ".bc";
    if (std::filesystem::exists(path)) return path;
    return "";
}

/// mod को bitcode के रूप में cache directory में atomic tmp-then-rename से write करता है।
void JITCache::store(const std::string& preprocessed_src,
                     const std::string& triple,
                     llvm::Module& mod) const {
    if (!ensureDir(cacheDir_)) return;
    std::string key = computeKey(preprocessed_src, triple);
    std::string finalPath = cacheDir_ + "/" + key + ".bc";
    std::string tmpPath   = finalPath + ".tmp." + std::to_string(getpid());

    std::error_code EC;
    llvm::raw_fd_ostream out(tmpPath, EC, llvm::sys::fs::OF_None);
    if (EC) return;
    llvm::WriteBitcodeToFile(mod, out);
    out.close();

    // Atomic rename: एक syscall में finalPath replace करता है, TOCTOU avoid करता है
    std::filesystem::rename(tmpPath, finalPath, EC);
    if (EC) {
        std::error_code removeEC;
        std::filesystem::remove(tmpPath, removeEC);
    }
}

} // namespace holyc

#endif // HCC_HAS_LLVM
