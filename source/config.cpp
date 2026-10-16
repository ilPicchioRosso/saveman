#include "config.h"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>

#include "jsonlite.h"

const char* const kBaseDir = "sdmc:/switch/saveman";
const char* const kBackupsDir = "sdmc:/switch/saveman/backups";
static const char* const kConfigPath = "sdmc:/switch/saveman/config.json";

void ensureDirs() {
    mkdir("sdmc:/switch", 0777);
    mkdir(kBaseDir, 0777);
    mkdir(kBackupsDir, 0777);
}

bool saveConfig(const Config& c) {
    ensureDirs();
    FILE* f = fopen(kConfigPath, "wb");
    if (!f) return false;
    fprintf(f,
            "{\n"
            "  \"client_id\": \"%s\",\n"
            "  \"client_secret\": \"%s\",\n"
            "  \"refresh_token\": \"%s\",\n"
            "  \"folder_name\": \"%s\",\n"
            "  \"keep_local\": %s,\n"
            "  \"local_keep_last\": %d\n"
            "}\n",
            jsonEscape(c.clientId).c_str(), jsonEscape(c.clientSecret).c_str(),
            jsonEscape(c.refreshToken).c_str(), jsonEscape(c.folderName).c_str(),
            c.keepLocal ? "true" : "false", c.localKeep);
    fclose(f);
    return true;
}

bool loadConfig(Config& c) {
    FILE* f = fopen(kConfigPath, "rb");
    if (!f) {
        saveConfig(c);  // crea il template da compilare
        return false;
    }
    std::string s;
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);

    c.clientId = jsonGet(s, "client_id");
    c.clientSecret = jsonGet(s, "client_secret");
    c.refreshToken = jsonGet(s, "refresh_token");
    std::string folder = jsonGet(s, "folder_name");
    if (!folder.empty()) c.folderName = folder;
    std::string keep = jsonGet(s, "keep_local");
    if (!keep.empty()) c.keepLocal = (keep != "false");
    std::string lk = jsonGet(s, "local_keep_last");
    if (!lk.empty()) c.localKeep = atoi(lk.c_str());
    if (c.localKeep < 0) c.localKeep = 0;
    return true;
}
