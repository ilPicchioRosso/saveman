#pragma once
#include <cstdint>
#include <functional>
#include <string>

// file corrente, byte fatti, byte totali; ritorna false per annullare
using BackupCb = std::function<bool(const std::string& file, uint64_t done, uint64_t total)>;

// Comprime ricorsivamente la cartella `root` in `zipPath`. In caso di errore cancella lo zip parziale.
bool zipDirectory(const std::string& root, const std::string& zipPath, const BackupCb& cb, std::string& err,
                  bool allowEmpty = false);
