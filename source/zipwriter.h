#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// Scrittore ZIP minimale (deflate, nomi UTF-8, niente ZIP64: file < 4 GB).
class ZipWriter {
public:
    using BytesCb = std::function<bool(uint64_t)>;  // byte appena letti; false = annulla
    ~ZipWriter();
    bool open(const std::string& path);
    bool addDirectory(const std::string& name);  // il nome deve finire con '/'
    bool addFile(const std::string& name, const std::string& srcPath, const BytesCb& cb);
    bool close();
    const std::string& error() const { return err_; }

private:
    struct Entry {
        std::string name;
        uint32_t crc = 0, csize = 0, usize = 0, offset = 0;
        uint16_t method = 0, dtime = 0, ddate = 0;
    };
    bool put16(uint16_t v);
    bool put32(uint32_t v);
    bool putRaw(const void* p, size_t n);
    bool writeLocal(const Entry& e);
    void stamp(Entry& e);
    bool beginEntry(Entry& e);

    FILE* f_ = nullptr;
    std::vector<Entry> entries_;
    std::string err_;
};
