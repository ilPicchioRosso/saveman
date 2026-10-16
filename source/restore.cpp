#include "restore.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <vector>

#include "zipreader.h"

namespace {
// Normalizza il nome di un'entry. false = percorso pericoloso.
bool cleanPath(const std::string& in, std::string& out) {
    std::string n = in;
    for (char& c : n) if (c == '\\') c = '/';
    std::vector<std::string> parts;
    size_t i = 0;
    while (i <= n.size()) {
        size_t j = n.find('/', i);
        std::string seg = n.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (seg == "..") return false;
        if (!seg.empty() && seg != ".") {
            if (seg.find(':') != std::string::npos) return false;
            parts.push_back(seg);
        }
        if (j == std::string::npos) break;
        i = j + 1;
    }
    out.clear();
    for (size_t k = 0; k < parts.size(); k++) out += (k ? "/" : "") + parts[k];
    return !out.empty();
}

void makeDirs(const std::string& root, const std::string& rel) {
    std::string cur = root;
    size_t i = 0;
    while (i < rel.size()) {
        size_t j = rel.find('/', i);
        std::string seg = rel.substr(i, j == std::string::npos ? std::string::npos : j - i);
        if (cur.empty() || cur.back() != '/') cur += '/';
        cur += seg;
        mkdir(cur.c_str(), 0777);
        if (j == std::string::npos) break;
        i = j + 1;
    }
}

bool wipe(const std::string& dir, bool removeSelf, std::string& err) {
    DIR* d = opendir(dir.c_str());
    if (!d) { err = "Impossibile aprire " + dir; return false; }
    std::vector<std::string> names;
    while (dirent* e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..") names.push_back(n);
    }
    closedir(d);
    for (const std::string& n : names) {
        std::string p = dir;
        if (p.back() != '/') p += '/';
        p += n;
        struct stat st;
        if (stat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!wipe(p, true, err)) return false;
        } else if (remove(p.c_str()) != 0) {
            err = "Impossibile eliminare " + p;
            return false;
        }
    }
    if (removeSelf && rmdir(dir.c_str()) != 0) { err = "Impossibile eliminare " + dir; return false; }
    return true;
}
}  // namespace

bool verifyZip(const std::string& zipPath, const RestoreCb& cb, std::string& err) {
    ZipReader zr;
    if (!zr.open(zipPath)) { err = zr.error(); return false; }
    if (zr.entries().empty()) { err = "Lo zip e' vuoto"; return false; }
    uint64_t total = zr.totalSize(), done = 0;
    for (const auto& e : zr.entries()) {
        std::string rel;
        if (!cleanPath(e.name, rel)) { err = "Percorso non valido nello zip: " + e.name; return false; }
        if (!zr.extract(e, nullptr, [&](uint64_t n) { done += n; return cb ? cb(rel, done, total) : true; })) {
            err = zr.error();
            return false;
        }
    }
    return true;
}

bool wipeDirectory(const std::string& root, std::string& err) { return wipe(root, false, err); }

bool extractZip(const std::string& zipPath, const std::string& root, const RestoreCb& cb, std::string& err) {
    ZipReader zr;
    if (!zr.open(zipPath)) { err = zr.error(); return false; }
    uint64_t total = zr.totalSize(), done = 0;
    for (const auto& e : zr.entries()) {
        std::string rel;
        if (!cleanPath(e.name, rel)) { err = "Percorso non valido nello zip: " + e.name; return false; }
        if (e.isDir()) { makeDirs(root, rel); continue; }

        size_t slash = rel.rfind('/');
        if (slash != std::string::npos) makeDirs(root, rel.substr(0, slash));
        std::string dest = root;
        if (dest.back() != '/') dest += '/';
        dest += rel;

        FILE* out = fopen(dest.c_str(), "wb");
        if (!out) { err = "Impossibile creare " + rel; return false; }
        bool ok = zr.extract(e, out, [&](uint64_t n) { done += n; return cb ? cb(rel, done, total) : true; });
        if (fclose(out) != 0 && ok) { err = "Scrittura fallita: " + rel; return false; }
        if (!ok) { err = zr.error(); return false; }
    }
    return true;
}
