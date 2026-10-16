#pragma once
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

struct HttpResult {
    long status = 0;
    std::string body;
    std::string location;  // header Location (serve per l'upload resumable)
    std::string error;     // vuoto = richiesta andata a buon fine a livello di rete
    bool ok() const { return error.empty(); }
};

// fatti, totali; ritorna false per annullare
using HttpProgress = std::function<bool(double done, double total)>;

std::string urlEncode(const std::string& s);
HttpResult httpGet(const std::string& url, const std::vector<std::string>& headers);
HttpResult httpPost(const std::string& url, const std::string& body, const std::vector<std::string>& headers);
HttpResult httpDelete(const std::string& url, const std::vector<std::string>& headers);
HttpResult httpPutFile(const std::string& url, const std::vector<std::string>& headers, FILE* f, long long size,
                       const HttpProgress& prog);
// Scarica il corpo della risposta direttamente in `out` (la risposta di errore non viene salvata in r.body).
HttpResult httpGetToFile(const std::string& url, const std::vector<std::string>& headers, FILE* out,
                         const HttpProgress& prog);
