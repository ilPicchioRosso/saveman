#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// Lettore ZIP minimale (metodi: stored e deflate; niente ZIP64 ne' cifratura).
class ZipReader {
public:
    struct Entry {
        std::string name;
        uint16_t method = 0;
        uint32_t crc = 0, csize = 0, usize = 0, offset = 0;
        bool isDir() const { return !name.empty() && name.back() == '/'; }
    };
    using BytesCb = std::function<bool(uint64_t)>;  // byte decompressi; false = annulla

    ~ZipReader();
    bool open(const std::string& path);
    const std::vector<Entry>& entries() const { return entries_; }
    uint64_t totalSize() const;
    // Decomprime l'entry in `out` (nullptr = solo verifica di CRC e dimensioni).
    bool extract(const Entry& e, FILE* out, const BytesCb& cb);
    const std::string& error() const { return err_; }

private:
    bool fail(const std::string& m) { err_ = m; return false; }
    FILE* f_ = nullptr;
    std::vector<Entry> entries_;
    std::string err_;
};
