#pragma once
#include <string>

#include "config.h"
#include <cstdint>
#include <vector>

#include "http.h"
#include "library.h"

struct DriveAuthPrompt {
    std::string deviceCode, userCode, url;
    int interval = 5;
    int expiresIn = 1800;
};

enum class PollResult { Pending, Done, Failed };

// Flusso OAuth "dispositivi con input limitato": l'utente inserisce il codice da telefono su google.com/device.
bool driveStartAuth(const Config& cfg, DriveAuthPrompt& out, std::string& err);
PollResult driveAuthPoll(Config& cfg, DriveAuthPrompt& prompt, std::string& err);  // se Done, imposta cfg.refreshToken

bool driveRefresh(const Config& cfg, std::string& accessToken, std::string& err);  // err == "invalid_grant" => rifare il login
bool driveEnsureFolder(const std::string& token, const std::string& name, std::string& folderId, std::string& err);
bool driveUpload(const std::string& token, const std::string& folderId, const std::string& path,
                 const std::string& driveName, const HttpProgress& prog, std::string& err);

struct RemoteBackup {
    std::string id, name, created;
    uint64_t size = 0;
    BackupName info;
    bool selected = false;
};

// Elenca gli zip presenti nella cartella (piu' recenti per primi, max 200).
bool driveListBackups(const std::string& token, const std::string& folderId, std::vector<RemoteBackup>& out,
                      std::string& err);
// Scarica in destPath (passando da destPath + ".part"). Controlla la dimensione se expectedSize > 0.
bool driveDownload(const std::string& token, const std::string& fileId, const std::string& destPath,
                   uint64_t expectedSize, const HttpProgress& prog, std::string& err);
bool driveDelete(const std::string& token, const std::string& fileId, std::string& err);
