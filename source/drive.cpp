#include "drive.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "jsonlite.h"

namespace {
const char* const kScope = "https://www.googleapis.com/auth/drive.file";

std::string googleError(const HttpResult& r) {
    if (!r.error.empty()) return r.error;
    std::string m = jsonGet(r.body, "error_description");
    if (m.empty()) m = jsonGet(r.body, "message");
    if (m.empty()) m = jsonGet(r.body, "error");
    if (m.empty()) m = "HTTP " + std::to_string(r.status);
    return m;
}

std::string bearer(const std::string& token) { return "Authorization: Bearer " + token; }
}  // namespace

bool driveStartAuth(const Config& cfg, DriveAuthPrompt& out, std::string& err) {
    std::string form = "client_id=" + urlEncode(cfg.clientId) + "&scope=" + urlEncode(kScope);
    HttpResult r = httpPost("https://oauth2.googleapis.com/device/code", form, {});
    out.deviceCode = jsonGet(r.body, "device_code");
    if (!r.ok() || out.deviceCode.empty()) { err = googleError(r); return false; }
    out.userCode = jsonGet(r.body, "user_code");
    out.url = jsonGet(r.body, "verification_url");
    if (out.url.empty()) out.url = jsonGet(r.body, "verification_uri");
    std::string iv = jsonGet(r.body, "interval");
    std::string ex = jsonGet(r.body, "expires_in");
    out.interval = iv.empty() ? 5 : atoi(iv.c_str());
    out.expiresIn = ex.empty() ? 1800 : atoi(ex.c_str());
    if (out.interval < 1) out.interval = 5;
    return true;
}

PollResult driveAuthPoll(Config& cfg, DriveAuthPrompt& p, std::string& err) {
    std::string form = "client_id=" + urlEncode(cfg.clientId) + "&client_secret=" + urlEncode(cfg.clientSecret) +
                       "&device_code=" + urlEncode(p.deviceCode) +
                       "&grant_type=" + urlEncode("urn:ietf:params:oauth:grant-type:device_code");
    HttpResult r = httpPost("https://oauth2.googleapis.com/token", form, {});
    if (!r.ok()) { err = r.error; return PollResult::Pending; }  // errore di rete momentaneo: riprova

    std::string refresh = jsonGet(r.body, "refresh_token");
    if (!refresh.empty()) { cfg.refreshToken = refresh; return PollResult::Done; }

    std::string e = jsonGet(r.body, "error");
    if (e == "authorization_pending") return PollResult::Pending;
    if (e == "slow_down") { p.interval += 5; return PollResult::Pending; }
    err = e == "access_denied" ? "Accesso negato dall'utente" : googleError(r);
    return PollResult::Failed;
}

bool driveRefresh(const Config& cfg, std::string& accessToken, std::string& err) {
    std::string form = "client_id=" + urlEncode(cfg.clientId) + "&client_secret=" + urlEncode(cfg.clientSecret) +
                       "&refresh_token=" + urlEncode(cfg.refreshToken) + "&grant_type=refresh_token";
    HttpResult r = httpPost("https://oauth2.googleapis.com/token", form, {});
    accessToken = jsonGet(r.body, "access_token");
    if (r.ok() && !accessToken.empty()) return true;
    std::string e = jsonGet(r.body, "error");
    err = (e == "invalid_grant") ? "invalid_grant" : googleError(r);
    return false;
}

bool driveEnsureFolder(const std::string& token, const std::string& name, std::string& folderId, std::string& err) {
    std::string safe;
    for (char c : name) {
        if (c == '\'' || c == '\\') safe += '\\';
        safe += c;
    }
    std::string q = "name='" + safe + "' and mimeType='application/vnd.google-apps.folder' and trashed=false";
    HttpResult r = httpGet("https://www.googleapis.com/drive/v3/files?q=" + urlEncode(q) +
                               "&fields=" + urlEncode("files(id)") + "&spaces=drive",
                           {bearer(token)});
    if (!r.ok() || r.status != 200) { err = googleError(r); return false; }
    std::string id = jsonGet(r.body, "id");
    if (!id.empty()) { folderId = id; return true; }

    std::string body = "{\"name\":\"" + jsonEscape(name) + "\",\"mimeType\":\"application/vnd.google-apps.folder\"}";
    r = httpPost("https://www.googleapis.com/drive/v3/files?fields=id", body,
                 {bearer(token), "Content-Type: application/json; charset=UTF-8"});
    id = jsonGet(r.body, "id");
    if (!r.ok() || id.empty()) { err = googleError(r); return false; }
    folderId = id;
    return true;
}

bool driveUpload(const std::string& token, const std::string& folderId, const std::string& path,
                 const std::string& driveName, const HttpProgress& prog, std::string& err) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { err = "Impossibile aprire lo zip da caricare"; return false; }
    fseek(f, 0, SEEK_END);
    long long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::string meta = "{\"name\":\"" + jsonEscape(driveName) + "\",\"parents\":[\"" + jsonEscape(folderId) + "\"]}";
    HttpResult init = httpPost("https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable&fields=id", meta,
                               {bearer(token), "Content-Type: application/json; charset=UTF-8",
                                "X-Upload-Content-Type: application/zip",
                                "X-Upload-Content-Length: " + std::to_string(size)});
    if (!init.ok() || init.status != 200 || init.location.empty()) {
        fclose(f);
        err = googleError(init);
        return false;
    }

    HttpResult up = httpPutFile(init.location, {"Content-Type: application/zip"}, f, size, prog);
    fclose(f);
    if (!up.ok() || (up.status != 200 && up.status != 201)) { err = googleError(up); return false; }
    return true;
}

bool driveListBackups(const std::string& token, const std::string& folderId, std::vector<RemoteBackup>& out,
                      std::string& err) {
    out.clear();
    std::string q = "'" + folderId + "' in parents and trashed=false";
    HttpResult r = httpGet("https://www.googleapis.com/drive/v3/files?q=" + urlEncode(q) +
                               "&fields=" + urlEncode("files(id,name,size,createdTime)") +
                               "&orderBy=" + urlEncode("createdTime desc") + "&pageSize=200&spaces=drive",
                           {bearer(token)});
    if (!r.ok() || r.status != 200) { err = googleError(r); return false; }
    for (const std::string& obj : jsonObjectsIn(r.body, "files")) {
        RemoteBackup b;
        b.id = jsonGet(obj, "id");
        b.name = jsonGet(obj, "name");
        b.created = jsonGet(obj, "createdTime");
        b.size = strtoull(jsonGet(obj, "size").c_str(), nullptr, 10);
        if (b.id.empty() || b.name.size() < 5) continue;
        std::string ext = b.name.substr(b.name.size() - 4);
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        if (ext != ".zip") continue;
        b.info = parseBackupName(b.name);
        out.push_back(std::move(b));
    }
    return true;
}

bool driveDownload(const std::string& token, const std::string& fileId, const std::string& destPath,
                   uint64_t expectedSize, const HttpProgress& prog, std::string& err) {
    std::string part = destPath + ".part";
    FILE* f = fopen(part.c_str(), "wb");
    if (!f) { err = "Impossibile creare il file sulla microSD"; return false; }
    HttpResult r = httpGetToFile("https://www.googleapis.com/drive/v3/files/" + urlEncode(fileId) + "?alt=media",
                                 {bearer(token)}, f, prog);
    long long got = ftell(f);
    bool closeOk = fclose(f) == 0;
    if (!r.ok() || r.status != 200) {
        err = !r.ok() ? r.error : "HTTP " + std::to_string(r.status);
        remove(part.c_str());
        return false;
    }
    if (!closeOk || (expectedSize > 0 && (uint64_t)got != expectedSize)) {
        err = "Download incompleto (dimensione diversa da quella su Drive)";
        remove(part.c_str());
        return false;
    }
    remove(destPath.c_str());
    if (rename(part.c_str(), destPath.c_str()) != 0) {
        err = "Impossibile rinominare il file scaricato";
        remove(part.c_str());
        return false;
    }
    return true;
}

bool driveDelete(const std::string& token, const std::string& fileId, std::string& err) {
    HttpResult r = httpDelete("https://www.googleapis.com/drive/v3/files/" + urlEncode(fileId), {bearer(token)});
    if (!r.ok() || (r.status != 204 && r.status != 200)) { err = googleError(r); return false; }
    return true;
}
