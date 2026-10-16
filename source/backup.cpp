#include "backup.h"

#include <dirent.h>
#include <sys/stat.h>

#include <cstdio>
#include <vector>

#include "zipwriter.h"

namespace {
struct Item {
    std::string rel, abs;
    bool dir;
    uint64_t size;
};

bool scan(const std::string& absDir, const std::string& rel, std::vector<Item>& out, uint64_t& total,
          std::string& err) {
    DIR* d = opendir(absDir.c_str());
    if (!d) { err = "Impossibile aprire la cartella " + absDir; return false; }
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name == "." || name == "..") continue;
        std::string a = absDir;
        if (a.empty() || a.back() != '/') a += '/';
        a += name;
        struct stat st;
        if (stat(a.c_str(), &st) != 0) continue;
        std::string r = rel.empty() ? name : rel + "/" + name;
        if (S_ISDIR(st.st_mode)) {
            out.push_back({r, a, true, 0});
            if (!scan(a, r, out, total, err)) { closedir(d); return false; }
        } else {
            out.push_back({r, a, false, (uint64_t)st.st_size});
            total += (uint64_t)st.st_size;
        }
    }
    closedir(d);
    return true;
}
}  // namespace

bool zipDirectory(const std::string& root, const std::string& zipPath, const BackupCb& cb, std::string& err,
                  bool allowEmpty) {
    std::vector<Item> items;
    uint64_t total = 0, done = 0;
    if (!scan(root, "", items, total, err)) return false;
    if (items.empty() && !allowEmpty) { err = "Il salvataggio e' vuoto"; return false; }

    ZipWriter zip;
    if (!zip.open(zipPath)) { err = zip.error(); return false; }

    bool ok = true;
    for (const Item& it : items) {
        if (it.dir) {
            ok = zip.addDirectory(it.rel + "/");
        } else {
            ok = zip.addFile(it.rel, it.abs, [&](uint64_t n) {
                done += n;
                return cb ? cb(it.rel, done, total) : true;
            });
        }
        if (!ok) break;
    }
    if (ok) ok = zip.close();
    if (!ok) {
        err = zip.error().empty() ? "Errore durante la creazione dello zip" : zip.error();
        remove(zipPath.c_str());
        return false;
    }
    return true;
}
