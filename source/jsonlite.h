#pragma once
// Helper minimali per i JSON piatti restituiti da Google e per il file di config.
// Non e' un parser vero: basta per chiavi semplici (stringhe, numeri, bool).
#include <string>
#include <vector>

inline std::string jsonGet(const std::string& j, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    size_t p = j.find(pat);
    while (p != std::string::npos) {
        size_t q = p + pat.size();
        while (q < j.size() && (j[q] == ' ' || j[q] == '\n' || j[q] == '\r' || j[q] == '\t')) q++;
        if (q < j.size() && j[q] == ':') { p = q + 1; break; }
        p = j.find(pat, p + 1);  // era un valore, non una chiave
    }
    if (p == std::string::npos) return "";
    while (p < j.size() && (j[p] == ' ' || j[p] == '\n' || j[p] == '\r' || j[p] == '\t')) p++;
    if (p >= j.size()) return "";
    std::string out;
    if (j[p] == '"') {
        for (p++; p < j.size() && j[p] != '"'; p++) {
            if (j[p] == '\\' && p + 1 < j.size()) {
                p++;
                char c = j[p];
                if (c == 'n') out += '\n';
                else if (c == 't') out += '\t';
                else out += c;
            } else {
                out += j[p];
            }
        }
    } else {
        while (p < j.size() && j[p] != ',' && j[p] != '}' && j[p] != '\n' && j[p] != ' ' && j[p] != '\r')
            out += j[p++];
    }
    return out;
}

inline std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { /* scarta i controlli */ }
        else o += (char)c;
    }
    return o;
}

// Restituisce gli oggetti {...} contenuti nell'array "arrayKey": [ {...}, {...} ]
inline std::vector<std::string> jsonObjectsIn(const std::string& j, const std::string& arrayKey) {
    std::vector<std::string> out;
    size_t p = j.find("\"" + arrayKey + "\"");
    if (p == std::string::npos) return out;
    p = j.find('[', p);
    if (p == std::string::npos) return out;
    int depth = 0;
    bool inStr = false;
    size_t start = 0;
    for (size_t i = p + 1; i < j.size(); i++) {
        char c = j[i];
        if (inStr) {
            if (c == '\\') i++;
            else if (c == '"') inStr = false;
            continue;
        }
        if (c == '"') inStr = true;
        else if (c == '{') { if (depth++ == 0) start = i; }
        else if (c == '}') { if (--depth == 0) out.push_back(j.substr(start, i - start + 1)); }
        else if (c == ']' && depth == 0) break;
    }
    return out;
}
