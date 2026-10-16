#include "saves.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>

namespace {
struct TitleInfo {
    std::string name;
    std::vector<u8> icon;
};

std::string hex16(u64 v) {
    char b[24];
    snprintf(b, sizeof b, "%016llX", (unsigned long long)v);
    return b;
}

std::string userNickname(AccountUid uid) {
    AccountProfile prof;
    AccountProfileBase base;
    memset(&base, 0, sizeof base);
    if (R_SUCCEEDED(accountGetProfile(&prof, uid))) {
        accountProfileGet(&prof, nullptr, &base);
        accountProfileClose(&prof);
    }
    base.nickname[sizeof base.nickname - 1] = 0;
    return base.nickname[0] ? std::string(base.nickname) : std::string("Utente");
}

TitleInfo titleInfo(u64 tid, NsApplicationControlData* buf) {
    TitleInfo t;
    u64 outSize = 0;
    Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, tid, buf,
                                            sizeof(NsApplicationControlData), &outSize);
    if (R_SUCCEEDED(rc) && outSize >= sizeof(buf->nacp)) {
        NacpLanguageEntry* le = nullptr;
        if (R_SUCCEEDED(nacpGetLanguageEntry(&buf->nacp, &le)) && le && le->name[0]) {
            le->name[sizeof le->name - 1] = 0;
            t.name = le->name;
        }
        size_t iconSize = (size_t)(outSize - sizeof(buf->nacp));
        if (iconSize > 0 && iconSize <= sizeof(buf->icon)) t.icon.assign(buf->icon, buf->icon + iconSize);
    }
    if (t.name.empty()) t.name = "Titolo sconosciuto (" + hex16(tid) + ")";
    return t;
}

std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}
}  // namespace

std::vector<SaveEntry> listSaves() {
    std::vector<SaveEntry> out;
    std::map<u64, TitleInfo> titles;
    std::map<std::string, std::string> users;

    auto* ctl = (NsApplicationControlData*)malloc(sizeof(NsApplicationControlData));
    if (!ctl) return out;

    FsSaveDataInfoReader reader;
    if (R_SUCCEEDED(fsOpenSaveDataInfoReader(&reader, FsSaveDataSpaceId_User))) {
        FsSaveDataInfo info[16];
        s64 count = 0;
        while (R_SUCCEEDED(fsSaveDataInfoReaderRead(&reader, info, 16, &count)) && count > 0) {
            for (s64 i = 0; i < count; i++) {
                const FsSaveDataInfo& si = info[i];
                if (si.save_data_type != FsSaveDataType_Account || si.application_id == 0) continue;
                if (si.save_data_index != 0) continue;

                SaveEntry e;
                e.titleId = si.application_id;
                e.uid = si.uid;

                auto ti = titles.find(e.titleId);
                if (ti == titles.end()) ti = titles.emplace(e.titleId, titleInfo(e.titleId, ctl)).first;
                e.title = ti->second.name;
                e.iconData = ti->second.icon;

                char key[40];
                snprintf(key, sizeof key, "%016llX%016llX", (unsigned long long)si.uid.uid[0],
                         (unsigned long long)si.uid.uid[1]);
                auto ui = users.find(key);
                if (ui == users.end()) ui = users.emplace(key, userNickname(si.uid)).first;
                e.user = ui->second;

                out.push_back(std::move(e));
            }
        }
        fsSaveDataInfoReaderClose(&reader);
    }
    free(ctl);

    std::sort(out.begin(), out.end(), [](const SaveEntry& a, const SaveEntry& b) {
        int c = lower(a.title).compare(lower(b.title));
        return c != 0 ? c < 0 : a.user < b.user;
    });
    return out;
}

namespace {
bool mountSave(const SaveEntry& s, std::string& err) {
    FsSaveDataAttribute attr;
    memset(&attr, 0, sizeof attr);
    attr.application_id = s.titleId;
    attr.uid = s.uid;
    attr.save_data_type = FsSaveDataType_Account;

    FsFileSystem fs;
    Result rc = fsOpenSaveDataFileSystem(&fs, FsSaveDataSpaceId_User, &attr);
    if (R_FAILED(rc)) {
        char b[160];
        snprintf(b, sizeof b,
                 "Impossibile aprire il salvataggio (0x%X). Se il gioco e' in esecuzione chiudilo e riprova.", rc);
        err = b;
        return false;
    }
    if (fsdevMountDevice("save", fs) < 0) {
        fsFsClose(&fs);
        err = "Impossibile montare il salvataggio";
        return false;
    }
    return true;
}
}  // namespace

bool backupSave(const SaveEntry& s, const std::string& zipPath, const BackupCb& cb, std::string& err,
                bool allowEmpty) {
    if (!mountSave(s, err)) return false;
    bool ok = zipDirectory("save:/", zipPath, cb, err, allowEmpty);
    fsdevUnmountDevice("save");
    return ok;
}

bool restoreSave(const SaveEntry& s, const std::string& zipPath, const RestoreCb& cb, std::string& err) {
    if (!mountSave(s, err)) return false;
    bool ok = wipeDirectory("save:/", err) && extractZip(zipPath, "save:/", cb, err);
    if (ok) {
        Result rc = fsdevCommitDevice("save");
        if (R_FAILED(rc)) {
            char b[96];
            snprintf(b, sizeof b, "Commit del salvataggio fallito (0x%X)", rc);
            err = b;
            ok = false;
        }
    }
    // Se ok == false non c'e' stato commit: le modifiche vengono scartate alla chiusura.
    fsdevUnmountDevice("save");
    return ok;
}
