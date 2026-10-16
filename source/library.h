#pragma once
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// Nome file dei backup:  Titolo [0100XXXXXXXXXXXX] - Utente - 2026-10-16_12-30-00[ (pre-restore)].zip
struct BackupName {
    bool valid = false;       // riconosciuto come backup di saveman (o compatibile)
    bool hasTid = false;      // contiene l'ID del gioco -> ripristino automatico
    bool preRestore = false;  // copia di sicurezza fatta prima di un ripristino
    uint64_t tid = 0;
    std::string title, user, stamp;
};

std::string safeName(const std::string& s);  // solo caratteri sicuri per FAT32/Drive
std::string safeFileName(const std::string& s);  // per nomi arrivati da Drive
std::string makeBackupName(const std::string& title, uint64_t tid, const std::string& user,
                           const std::string& stamp, bool preRestore = false);  // include ".zip"
BackupName parseBackupName(const std::string& fileName);
std::string prettyStamp(const std::string& stamp);  // "2026-10-16_12-30-00" -> "16/10/2026 12:30"
std::string prettySize(uint64_t bytes);

struct LocalBackup {
    std::string path, name;
    uint64_t size = 0;
    time_t mtime = 0;
    BackupName info;
    bool selected = false;
};

std::vector<LocalBackup> listLocalBackups(const std::string& dir);  // piu' recenti per primi

struct PrunePlan {
    std::vector<std::string> paths;
    uint64_t bytes = 0;
};
// Tiene gli ultimi `keepPerGame` backup per ogni gioco (e gli ultimi 2 "pre-restore"); il resto va eliminato.
PrunePlan planPrune(const std::vector<LocalBackup>& all, int keepPerGame);
