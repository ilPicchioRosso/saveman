#include "zipwriter.h"

#include <zlib.h>

#include <cstring>
#include <ctime>

namespace {
constexpr size_t kChunk = 64 * 1024;
}

ZipWriter::~ZipWriter() {
    if (f_) fclose(f_);
}

bool ZipWriter::open(const std::string& path) {
    f_ = fopen(path.c_str(), "wb");
    if (!f_) { err_ = "Impossibile creare " + path; return false; }
    return true;
}

bool ZipWriter::putRaw(const void* p, size_t n) {
    if (n && fwrite(p, 1, n, f_) != n) { err_ = "Scrittura su SD fallita (spazio esaurito?)"; return false; }
    return true;
}
bool ZipWriter::put16(uint16_t v) {
    unsigned char b[2] = {(unsigned char)(v & 0xFF), (unsigned char)(v >> 8)};
    return putRaw(b, 2);
}
bool ZipWriter::put32(uint32_t v) {
    unsigned char b[4] = {(unsigned char)(v & 0xFF), (unsigned char)((v >> 8) & 0xFF),
                          (unsigned char)((v >> 16) & 0xFF), (unsigned char)(v >> 24)};
    return putRaw(b, 4);
}

void ZipWriter::stamp(Entry& e) {
    time_t t = time(nullptr);
    struct tm* lt = localtime(&t);
    int year = lt ? lt->tm_year : 100;
    if (year < 80) year = 80;
    if (lt) {
        e.ddate = (uint16_t)(((year - 80) << 9) | ((lt->tm_mon + 1) << 5) | lt->tm_mday);
        e.dtime = (uint16_t)((lt->tm_hour << 11) | (lt->tm_min << 5) | (lt->tm_sec / 2));
    } else {
        e.ddate = (uint16_t)((20 << 9) | (1 << 5) | 1);
        e.dtime = 0;
    }
}

bool ZipWriter::writeLocal(const Entry& e) {
    return put32(0x04034b50) && put16(20) && put16(0x0800) && put16(e.method) && put16(e.dtime) &&
           put16(e.ddate) && put32(e.crc) && put32(e.csize) && put32(e.usize) &&
           put16((uint16_t)e.name.size()) && put16(0) && putRaw(e.name.data(), e.name.size());
}

bool ZipWriter::beginEntry(Entry& e) {
    if (e.name.size() > 0xFFFF) { err_ = "Nome file troppo lungo"; return false; }
    if (entries_.size() >= 65535) { err_ = "Troppi file nel salvataggio"; return false; }
    long off = ftell(f_);
    if (off < 0 || (unsigned long)off > 0xFFFFFF00UL) { err_ = "Archivio troppo grande (>4 GB)"; return false; }
    e.offset = (uint32_t)off;
    stamp(e);
    return true;
}

bool ZipWriter::addDirectory(const std::string& name) {
    Entry e;
    e.name = name;
    e.method = 0;
    if (!beginEntry(e) || !writeLocal(e)) return false;
    entries_.push_back(e);
    return true;
}

bool ZipWriter::addFile(const std::string& name, const std::string& src, const BytesCb& cb) {
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) { err_ = "Impossibile leggere " + name; return false; }

    Entry e;
    e.name = name;
    e.method = 8;
    if (!beginEntry(e) || !writeLocal(e)) { fclose(in); return false; }  // CRC e dimensioni vengono corretti dopo

    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (deflateInit2(&zs, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        fclose(in);
        err_ = "Errore di inizializzazione zlib";
        return false;
    }

    std::vector<unsigned char> inb(kChunk), outb(kChunk);
    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t usz = 0, csz = 0;
    bool ok = true;

    for (;;) {
        size_t n = fread(inb.data(), 1, kChunk, in);
        if (ferror(in)) { err_ = "Errore di lettura di " + name; ok = false; break; }
        bool last = n < kChunk;
        crc = crc32(crc, inb.data(), (uInt)n);
        usz += n;
        zs.next_in = inb.data();
        zs.avail_in = (uInt)n;
        int flush = last ? Z_FINISH : Z_NO_FLUSH;
        do {
            zs.next_out = outb.data();
            zs.avail_out = (uInt)kChunk;
            if (deflate(&zs, flush) == Z_STREAM_ERROR) { err_ = "Errore zlib"; ok = false; break; }
            size_t have = kChunk - zs.avail_out;
            if (have && !putRaw(outb.data(), have)) { ok = false; break; }
            csz += have;
        } while (zs.avail_out == 0);
        if (!ok) break;
        if (cb && n && !cb(n)) { err_ = "Annullato"; ok = false; break; }
        if (last) break;
    }
    deflateEnd(&zs);
    fclose(in);
    if (!ok) return false;

    if (usz > 0xFFFFFFF0ULL || csz > 0xFFFFFFF0ULL) { err_ = "File troppo grande (>4 GB): " + name; return false; }
    e.crc = (uint32_t)crc;
    e.csize = (uint32_t)csz;
    e.usize = (uint32_t)usz;

    long end = ftell(f_);
    if (fseek(f_, (long)e.offset + 14, SEEK_SET) != 0) { err_ = "Errore di seek"; return false; }
    if (!put32(e.crc) || !put32(e.csize) || !put32(e.usize)) return false;
    if (fseek(f_, end, SEEK_SET) != 0) { err_ = "Errore di seek"; return false; }

    entries_.push_back(e);
    return true;
}

bool ZipWriter::close() {
    if (!f_) return false;
    long cdStart = ftell(f_);
    bool ok = cdStart >= 0;
    for (const Entry& e : entries_) {
        ok = ok && put32(0x02014b50) && put16(20) && put16(20) && put16(0x0800) && put16(e.method) &&
             put16(e.dtime) && put16(e.ddate) && put32(e.crc) && put32(e.csize) && put32(e.usize) &&
             put16((uint16_t)e.name.size()) && put16(0) && put16(0) && put16(0) && put16(0) &&
             put32(e.name.back() == '/' ? 0x10u : 0u) && put32(e.offset) && putRaw(e.name.data(), e.name.size());
        if (!ok) break;
    }
    long cdEnd = ftell(f_);
    ok = ok && put32(0x06054b50) && put16(0) && put16(0) && put16((uint16_t)entries_.size()) &&
         put16((uint16_t)entries_.size()) && put32((uint32_t)(cdEnd - cdStart)) && put32((uint32_t)cdStart) &&
         put16(0);
    int rc = fclose(f_);
    f_ = nullptr;
    if (rc != 0 && ok) { err_ = "Chiusura dell'archivio fallita"; ok = false; }
    return ok;
}
