#include "zipreader.h"

#include <zlib.h>

#include <cstring>

namespace {
constexpr size_t kChunk = 64 * 1024;
uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
}  // namespace

ZipReader::~ZipReader() {
    if (f_) fclose(f_);
}

uint64_t ZipReader::totalSize() const {
    uint64_t t = 0;
    for (const Entry& e : entries_) t += e.usize;
    return t;
}

bool ZipReader::open(const std::string& path) {
    f_ = fopen(path.c_str(), "rb");
    if (!f_) return fail("Impossibile aprire il file zip");
    if (fseek(f_, 0, SEEK_END) != 0) return fail("Lettura dello zip fallita");
    long size = ftell(f_);
    if (size < 22) return fail("File troppo piccolo: non e' uno zip valido");

    long tail = size < 65557 ? size : 65557;
    std::vector<unsigned char> buf((size_t)tail);
    fseek(f_, size - tail, SEEK_SET);
    if (fread(buf.data(), 1, buf.size(), f_) != buf.size()) return fail("Lettura dello zip fallita");

    long eocd = -1;
    for (long i = tail - 22; i >= 0; i--) {
        if (rd32(&buf[i]) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) return fail("Non e' uno zip valido (manca la fine dell'archivio)");

    uint16_t count = rd16(&buf[eocd + 10]);
    uint32_t cdSize = rd32(&buf[eocd + 12]);
    uint32_t cdOff = rd32(&buf[eocd + 16]);
    if (count == 0xFFFF || cdSize == 0xFFFFFFFFu || cdOff == 0xFFFFFFFFu)
        return fail("Zip64 non supportato");
    if ((long)cdOff + (long)cdSize > size) return fail("Zip danneggiato");

    std::vector<unsigned char> cd(cdSize);
    fseek(f_, (long)cdOff, SEEK_SET);
    if (cdSize && fread(cd.data(), 1, cdSize, f_) != cdSize) return fail("Lettura dello zip fallita");

    size_t p = 0;
    for (uint16_t i = 0; i < count; i++) {
        if (p + 46 > cd.size() || rd32(&cd[p]) != 0x02014b50) return fail("Zip danneggiato (indice)");
        Entry e;
        uint16_t flags = rd16(&cd[p + 8]);
        e.method = rd16(&cd[p + 10]);
        e.crc = rd32(&cd[p + 16]);
        e.csize = rd32(&cd[p + 20]);
        e.usize = rd32(&cd[p + 24]);
        uint16_t nl = rd16(&cd[p + 28]), el = rd16(&cd[p + 30]), cl = rd16(&cd[p + 32]);
        e.offset = rd32(&cd[p + 42]);
        if (p + 46 + nl > cd.size()) return fail("Zip danneggiato (nomi)");
        e.name.assign((const char*)&cd[p + 46], nl);
        if (flags & 1) return fail("Zip cifrato: non supportato");
        if (e.method != 0 && e.method != 8) return fail("Metodo di compressione non supportato");
        entries_.push_back(std::move(e));
        p += 46 + nl + el + cl;
    }
    return true;
}

bool ZipReader::extract(const Entry& e, FILE* out, const BytesCb& cb) {
    if (e.isDir()) return true;
    unsigned char lh[30];
    if (fseek(f_, (long)e.offset, SEEK_SET) != 0 || fread(lh, 1, 30, f_) != 30 || rd32(lh) != 0x04034b50)
        return fail("Zip danneggiato: " + e.name);
    long dataStart = (long)e.offset + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (fseek(f_, dataStart, SEEK_SET) != 0) return fail("Zip danneggiato: " + e.name);

    std::vector<unsigned char> inb(kChunk), outb(kChunk);
    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t produced = 0, remaining = e.csize;

    z_stream zs;
    memset(&zs, 0, sizeof zs);
    bool inflating = (e.method == 8);
    if (inflating && inflateInit2(&zs, -MAX_WBITS) != Z_OK) return fail("Errore di inizializzazione zlib");

    bool ok = true;
    bool streamEnd = !inflating;
    auto emit = [&](const unsigned char* data, size_t n) -> bool {
        if (!n) return true;
        crc = crc32(crc, data, (uInt)n);
        produced += n;
        if (out && fwrite(data, 1, n, out) != n) { err_ = "Scrittura fallita (spazio esaurito?)"; return false; }
        if (cb && !cb(n)) { err_ = "Annullato"; return false; }
        return true;
    };

    while (ok && remaining > 0) {
        size_t n = remaining < kChunk ? (size_t)remaining : kChunk;
        if (fread(inb.data(), 1, n, f_) != n) { err_ = "Zip troncato: " + e.name; ok = false; break; }
        remaining -= n;
        if (!inflating) { ok = emit(inb.data(), n); continue; }
        zs.next_in = inb.data();
        zs.avail_in = (uInt)n;
        do {
            zs.next_out = outb.data();
            zs.avail_out = (uInt)kChunk;
            int r = inflate(&zs, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END && r != Z_BUF_ERROR) { err_ = "Dati compressi corrotti: " + e.name; ok = false; break; }
            if (!emit(outb.data(), kChunk - zs.avail_out)) { ok = false; break; }
            if (r == Z_STREAM_END) { streamEnd = true; break; }
        } while (zs.avail_out == 0);
    }
    if (inflating) inflateEnd(&zs);
    if (!ok) return false;
    if (!streamEnd) return fail("Dati compressi incompleti: " + e.name);
    if (produced != e.usize) return fail("Dimensione non corrispondente: " + e.name);
    if ((uint32_t)crc != e.crc) return fail("Controllo CRC fallito (file corrotto): " + e.name);
    return true;
}
