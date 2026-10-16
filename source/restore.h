#pragma once
#include <cstdint>
#include <functional>
#include <string>

// etichetta, byte fatti, byte totali; false = annulla
using RestoreCb = std::function<bool(const std::string& what, uint64_t done, uint64_t total)>;

// Controlla l'integrita' (CRC di ogni file) senza scrivere nulla.
bool verifyZip(const std::string& zipPath, const RestoreCb& cb, std::string& err);
// Cancella tutto il contenuto di `root` (la cartella resta).
bool wipeDirectory(const std::string& root, std::string& err);
// Estrae lo zip dentro `root`. Rifiuta percorsi con ".." o assoluti.
bool extractZip(const std::string& zipPath, const std::string& root, const RestoreCb& cb, std::string& err);
