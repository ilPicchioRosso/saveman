#include "http.h"

#include <curl/curl.h>
#include <strings.h>

#include <cstring>

namespace {
size_t writeCb(char* p, size_t s, size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, s * n);
    return s * n;
}

size_t fileWriteCb(char* p, size_t s, size_t n, void* ud) { return fwrite(p, s, n, static_cast<FILE*>(ud)); }

size_t headerCb(char* p, size_t s, size_t n, void* ud) {
    static const char key[] = "location:";
    std::string line(p, s * n);
    if (line.size() > sizeof(key) - 1 && strncasecmp(line.c_str(), key, sizeof(key) - 1) == 0) {
        std::string v = line.substr(sizeof(key) - 1);
        size_t a = v.find_first_not_of(" \t");
        size_t b = v.find_last_not_of(" \t\r\n");
        *static_cast<std::string*>(ud) = (a == std::string::npos) ? "" : v.substr(a, b - a + 1);
    }
    return s * n;
}

struct ProgState {
    const HttpProgress* fn = nullptr;
    bool cancelled = false;
};

int xferCb(void* ud, curl_off_t dt, curl_off_t dn, curl_off_t ut, curl_off_t un) {
    ProgState* p = static_cast<ProgState*>(ud);
    if (p->fn && *p->fn) {
        bool uploading = (ut > 0 || un > 0);
        double done = uploading ? (double)un : (double)dn;
        double total = uploading ? (double)ut : (double)dt;
        if (!(*p->fn)(done, total)) {
            p->cancelled = true;
            return 1;
        }
    }
    return 0;
}

#ifdef __SWITCH__
// Preferisce il bundle di certificati sulla microSD (se presente), altrimenti quello nel romfs.
const char* caPath() {
    static const char* sd = "sdmc:/switch/saveman/cacert.pem";
    FILE* f = fopen(sd, "rb");
    if (f) {
        fclose(f);
        return sd;
    }
    return "romfs:/cacert.pem";
}
#endif

struct Req {
    const std::string* postBody = nullptr;
    FILE* putFile = nullptr;
    long long putSize = 0;
    FILE* dlFile = nullptr;
    const char* method = nullptr;
};

HttpResult run(const std::string& url, const std::vector<std::string>& headers, const Req& rq,
               const HttpProgress& prog) {
    HttpResult r;
    CURL* c = curl_easy_init();
    if (!c) { r.error = "curl non disponibile"; return r; }

    curl_slist* hl = nullptr;
    for (const auto& h : headers) hl = curl_slist_append(hl, h.c_str());
    if (rq.putFile) hl = curl_slist_append(hl, "Expect:");

    ProgState ps;
    ps.fn = &prog;

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, "saveman/1.1 (Nintendo Switch)");
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hl);
    if (rq.dlFile) {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, fileWriteCb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, rq.dlFile);
    } else {
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
    }
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, headerCb);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &r.location);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, rq.dlFile ? 1L : 0L);  // i download possono essere reindirizzati
#ifdef __SWITCH__
    curl_easy_setopt(c, CURLOPT_CAINFO, caPath());
#endif
    if (rq.method) curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, rq.method);
    if (rq.postBody) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, rq.postBody->c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)rq.postBody->size());
    }
    if (rq.putFile) {
        curl_easy_setopt(c, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(c, CURLOPT_READDATA, rq.putFile);
        curl_easy_setopt(c, CURLOPT_INFILESIZE_LARGE, (curl_off_t)rq.putSize);
    }
    if (rq.putFile || rq.dlFile) {
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferCb);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &ps);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    }

    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK) {
        r.error = ps.cancelled ? "Annullato" : std::string("Errore di rete: ") + curl_easy_strerror(rc);
    }
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &r.status);
    curl_slist_free_all(hl);
    curl_easy_cleanup(c);
    return r;
}
}  // namespace

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            o += (char)c;
        } else {
            o += '%';
            o += hex[c >> 4];
            o += hex[c & 15];
        }
    }
    return o;
}

HttpResult httpGet(const std::string& url, const std::vector<std::string>& headers) {
    return run(url, headers, Req{}, nullptr);
}

HttpResult httpPost(const std::string& url, const std::string& body, const std::vector<std::string>& headers) {
    Req rq;
    rq.postBody = &body;
    return run(url, headers, rq, nullptr);
}

HttpResult httpDelete(const std::string& url, const std::vector<std::string>& headers) {
    Req rq;
    rq.method = "DELETE";
    return run(url, headers, rq, nullptr);
}

HttpResult httpPutFile(const std::string& url, const std::vector<std::string>& headers, FILE* f, long long size,
                       const HttpProgress& prog) {
    Req rq;
    rq.putFile = f;
    rq.putSize = size;
    return run(url, headers, rq, prog);
}

HttpResult httpGetToFile(const std::string& url, const std::vector<std::string>& headers, FILE* out,
                         const HttpProgress& prog) {
    Req rq;
    rq.dlFile = out;
    return run(url, headers, rq, prog);
}
