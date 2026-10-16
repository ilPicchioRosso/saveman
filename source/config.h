#pragma once
#include <string>

struct Config {
    std::string clientId, clientSecret, refreshToken;
    std::string folderName = "Saveman";
    bool keepLocal = true;  // tiene lo zip anche sulla microSD dopo l'upload
    int localKeep = 5;      // quanti backup locali tenere per gioco (0 = nessun limite)
};

extern const char* const kBaseDir;     // sdmc:/switch/saveman
extern const char* const kBackupsDir;  // sdmc:/switch/saveman/backups

void ensureDirs();
bool loadConfig(Config& c);  // false se il file non esiste (in quel caso ne crea uno di esempio)
bool saveConfig(const Config& c);
