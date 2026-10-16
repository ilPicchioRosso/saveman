#include "library.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>

std::string safeName(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        bool good = isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '(' || c == ')' || c == '.';
        char ch = good ? (char)c : '_';
        if (ch == '_' && !o.empty() && o.back() == '_') continue;
        o += ch;
    }
    size_t a = o.find_first_not_of(" ._");
    size_t b = o.find_last_not_of(" .");
    o = (a == std::string::npos) ? "" : o.substr(a, b - a + 1);
    if (o.size() > 60) o.resize(60);
    return o.empty() ? "save" : o;
}

std::string safeFileName(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|')
            o += '_';
        else
            o += (char)c;
    }
    size_t b = o.find_last_not_of(" .");
    o = (b == std::string::npos) ? "" : o.substr(0, b + 1);
    if (o.size() > 150) o.resize(150);
    return o.empty() ? "backup.zip" : o;
}

std::string makeBackupName(const std::string& title, uint64_t tid, const std::string& user,
                           const std::string& stamp, bool preRestore) {
    char t[24];
    snprintf(t, sizeof t, "%016llX", (unsigned long long)tid);
    return safeName(title) + " [" + t + "] - " + safeName(user) + " - " + stamp +
           (preRestore ? " (pre-restore)" : "") + ".zip";
}

static bool isHex16(const std::string& s) {
    if (s.size() != 16) return false;
    for (char c : s) if (!isxdigit((unsigned char)c)) return false;
    return true;
}

BackupName parseBackupName(const std::string& fileName) {
    BackupName r;
    std::string n = fileName;
    if (n.size() > 4) {
        std::string ext = n.substr(n.size() - 4);
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        if (ext == ".zip") n.resize(n.size() - 4);
    }
    const std::string pre = " (pre-restore)";
    if (n.size() > pre.size() && n.compare(n.size() - pre.size(), pre.size(), pre) == 0) {
        r.preRestore = true;
        n.resize(n.size() - pre.size());
    }
    const std::string sep = " - ";
    size_t s2 = n.rfind(sep);
    if (s2 == std::string::npos || s2 == 0) { r.title = fileName; return r; }
    size_t s1 = n.rfind(sep, s2 - 1);
    if (s1 == std::string::npos) { r.title = fileName; return r; }

    r.stamp = n.substr(s2 + sep.size());
    r.user = n.substr(s1 + sep.size(), s2 - (s1 + sep.size()));
    std::string t = n.substr(0, s1);
    if (t.size() > 19 && t.back() == ']' && t[t.size() - 18] == '[' && t[t.size() - 19] == ' ') {
        std::string hex = t.substr(t.size() - 17, 16);
        if (isHex16(hex)) {
            r.tid = strtoull(hex.c_str(), nullptr, 16);
            r.hasTid = true;
            t.resize(t.size() - 19);
        }
    }
    r.title = t;
    r.valid = true;
    return r;
}

std::string prettyStamp(const std::string& stamp) {
    // 2026-10-16_12-30-00 [...]
    if (stamp.size() >= 19 && stamp[4] == '-' && stamp[7] == '-' && stamp[10] == '_' && stamp[13] == '-') {
        return stamp.substr(8, 2) + "/" + stamp.substr(5, 2) + "/" + stamp.substr(0, 4) + " " +
               stamp.substr(11, 2) + ":" + stamp.substr(14, 2);
    }
    return stamp;
}

std::string prettySize(uint64_t b) {
    char s[32];
    if (b >= 1024ull * 1024 * 1024) snprintf(s, sizeof s, "%.1f GB", b / (1024.0 * 1024 * 1024));
    else if (b >= 1024ull * 1024) snprintf(s, sizeof s, "%.1f MB", b / (1024.0 * 1024));
    else snprintf(s, sizeof s, "%llu KB", (unsigned long long)((b + 1023) / 1024));
    return s;
}

std::vector<LocalBackup> listLocalBackups(const std::string& dir) {
    std::vector<LocalBackup> out;
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    while (dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (name.size() < 5) continue;
        std::string ext = name.substr(name.size() - 4);
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        if (ext != ".zip") continue;
        LocalBackup b;
        b.name = name;
        b.path = dir + "/" + name;
        struct stat st;
        if (stat(b.path.c_str(), &st) != 0 || S_ISDIR(st.st_mode)) continue;
        b.size = (uint64_t)st.st_size;
        b.mtime = st.st_mtime;
        b.info = parseBackupName(name);
        out.push_back(std::move(b));
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const LocalBackup& a, const LocalBackup& b) {
        // prima il timestamp scritto nel nome (affidabile anche se l'orologio della FAT e' strano), poi mtime
        if (a.info.stamp != b.info.stamp && !a.info.stamp.empty() && !b.info.stamp.empty())
            return a.info.stamp > b.info.stamp;
        if (a.mtime != b.mtime) return a.mtime > b.mtime;
        return a.name < b.name;
    });
    return out;
}

PrunePlan planPrune(const std::vector<LocalBackup>& all, int keepPerGame) {
    PrunePlan plan;
    if (keepPerGame <= 0) return plan;
    std::map<std::string, int> seen;  // la lista e' gia' ordinata dal piu' recente
    for (const LocalBackup& b : all) {
        std::string key;
        if (b.info.hasTid) {
            char t[24];
            snprintf(t, sizeof t, "%016llX", (unsigned long long)b.info.tid);
            key = t;
        } else {
            key = "n:" + b.info.title;
        }
        int limit = keepPerGame;
        if (b.info.preRestore) { key += "|pre"; limit = 2; }
        if (++seen[key] > limit) {
            plan.paths.push_back(b.path);
            plan.bytes += b.size;
        }
    }
    return plan;
}
