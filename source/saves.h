#pragma once
#include <SDL2/SDL.h>
#include <switch.h>

#include <string>
#include <vector>

#include "backup.h"
#include "library.h"
#include "restore.h"

struct SaveEntry {
    u64 titleId = 0;
    AccountUid uid{};
    std::string title, user;
    std::vector<u8> iconData;      // JPEG 256x256 dell'icona del gioco
    SDL_Texture* icon = nullptr;   // creata al volo dalla UI
    bool selected = false;
};

// Elenca i salvataggi utente (type Account) presenti sulla console.
std::vector<SaveEntry> listSaves();

// Monta il salvataggio in sola lettura, lo comprime in zipPath e lo smonta.
bool backupSave(const SaveEntry& s, const std::string& zipPath, const BackupCb& cb, std::string& err,
                bool allowEmpty = false);

// ATTENZIONE: sostituisce il contenuto del salvataggio con quello dello zip (poi fa il commit).
// Se qualcosa va storto prima del commit, le modifiche non vengono salvate.
bool restoreSave(const SaveEntry& s, const std::string& zipPath, const RestoreCb& cb, std::string& err);
