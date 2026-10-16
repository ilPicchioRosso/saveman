// saveman - backup, download e ripristino dei salvataggi Switch con Google Drive
#include <curl/curl.h>
#include <switch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "config.h"
#include "drive.h"
#include "library.h"
#include "saves.h"
#include "ui.h"

namespace {
using Clock = std::chrono::steady_clock;

const char* const kVersion = "1.1.0";

const Color cText = {236, 240, 255, 255};
const Color cDim = {140, 150, 185, 255};
const Color cCard = {27, 32, 58, 255};
const Color cCardSel = {40, 52, 104, 255};
const Color cModal = {30, 36, 66, 255};
const Color cAccent = {94, 196, 255, 255};
const Color cGood = {88, 214, 141, 255};
const Color cBad = {255, 99, 99, 255};
const Color cWarn = {255, 196, 87, 255};

constexpr int kVisible = 6, kRowH = 78, kRowPitch = 86, kListY = 104;

enum Tab { TabSaves = 0, TabDrive = 1, TabLocal = 2 };

struct ListState {
    int cursor = 0, scroll = 0;
};

struct App {
    Config cfg;
    bool cfgFound = false;
    std::vector<SaveEntry> saves;
    std::vector<LocalBackup> locals;
    std::vector<RemoteBackup> remotes;
    bool remotesLoaded = false;
    std::string remotesError, folderId;
    int tab = TabSaves;
    ListState ls[3];
    PadState pad;
    bool linked() const { return !cfg.refreshToken.empty(); }
} app;

// ---------------------------------------------------------------- utilita'
std::string timestamp() {
    char b[32];
    time_t t = time(nullptr);
    strftime(b, sizeof b, "%Y-%m-%d_%H-%M-%S", localtime(&t));
    return b;
}

std::string hex16(u64 v) {
    char b[24];
    snprintf(b, sizeof b, "%016llX", (unsigned long long)v);
    return b;
}

bool hasInternet() {
    NifmInternetConnectionType type;
    u32 strength;
    NifmInternetConnectionStatus st;
    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &st))) return false;
    return st == NifmInternetConnectionStatus_Connected;
}

SDL_Texture* iconFor(SaveEntry& e) {
    if (!e.icon && !e.iconData.empty()) e.icon = ui::textureFromImage(e.iconData.data(), e.iconData.size());
    return e.icon;
}

SDL_Texture* iconByTid(u64 tid) {
    for (auto& s : app.saves) if (s.titleId == tid) return iconFor(s);
    return nullptr;
}

int tabCount(int t) {
    if (t == TabSaves) return (int)app.saves.size();
    if (t == TabDrive) return (int)app.remotes.size();
    return (int)app.locals.size();
}

bool& selRef(int t, int i) {
    if (t == TabSaves) return app.saves[i].selected;
    if (t == TabDrive) return app.remotes[i].selected;
    return app.locals[i].selected;
}

std::vector<int> targetsOf(int t) {  // i selezionati, oppure l'elemento sotto il cursore
    std::vector<int> v;
    for (int i = 0; i < tabCount(t); i++) if (selRef(t, i)) v.push_back(i);
    if (v.empty() && tabCount(t) > 0) v.push_back(app.ls[t].cursor);
    return v;
}

void clearSelection(int t) {
    for (int i = 0; i < tabCount(t); i++) selRef(t, i) = false;
}

void clampCursor(int t) {
    ListState& l = app.ls[t];
    int n = tabCount(t);
    l.cursor = std::max(0, std::min(n - 1, l.cursor));
    if (n == 0) l.cursor = 0;
    if (l.cursor < l.scroll) l.scroll = l.cursor;
    if (l.cursor >= l.scroll + kVisible) l.scroll = l.cursor - kVisible + 1;
    l.scroll = std::max(0, std::min(l.scroll, std::max(0, n - kVisible)));
}

void moveCursor(int t, int d) {
    app.ls[t].cursor += d;
    clampCursor(t);
}

void refreshLocals() {
    app.locals = listLocalBackups(kBackupsDir);
    clampCursor(TabLocal);
}

uint64_t localTotalBytes() {
    uint64_t t = 0;
    for (const auto& b : app.locals) t += b.size;
    return t;
}

bool sdFree(uint64_t& freeBytes) {
    struct statvfs sv;
    if (statvfs("sdmc:/", &sv) != 0) return false;
    freeBytes = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
    return true;
}

// applica la regola "tieni gli ultimi N backup per gioco"; ritorna quanti file ha eliminato
int autoPrune(uint64_t* freed = nullptr) {
    if (app.cfg.localKeep <= 0) return 0;
    std::vector<LocalBackup> all = listLocalBackups(kBackupsDir);
    PrunePlan plan = planPrune(all, app.cfg.localKeep);
    int n = 0;
    for (const auto& p : plan.paths) if (remove(p.c_str()) == 0) n++;
    if (freed) *freed = plan.bytes;
    return n;
}

// ---------------------------------------------------------------- disegno: schermata principale
struct RowView {
    std::string title, sub, right;
    SDL_Texture* icon = nullptr;
    bool selected = false;
};

RowView rowView(int t, int i) {
    RowView r;
    if (t == TabSaves) {
        SaveEntry& e = app.saves[i];
        r.title = e.title;
        r.sub = e.user;
        r.icon = iconFor(e);
        r.selected = e.selected;
    } else if (t == TabDrive) {
        RemoteBackup& b = app.remotes[i];
        r.title = b.info.valid ? b.info.title : b.name;
        r.sub = b.info.valid ? b.info.user + "  -  " + prettyStamp(b.info.stamp) : "File sul Drive";
        if (b.info.preRestore) r.sub += "  -  copia di sicurezza";
        r.right = prettySize(b.size);
        r.icon = b.info.hasTid ? iconByTid(b.info.tid) : nullptr;
        r.selected = b.selected;
    } else {
        LocalBackup& b = app.locals[i];
        r.title = b.info.valid ? b.info.title : b.name;
        r.sub = b.info.valid ? b.info.user + "  -  " + prettyStamp(b.info.stamp) : "File sulla microSD";
        if (b.info.preRestore) r.sub += "  -  copia di sicurezza";
        r.right = prettySize(b.size);
        r.icon = b.info.hasTid ? iconByTid(b.info.tid) : nullptr;
        r.selected = b.selected;
    }
    return r;
}

void drawIcon(SDL_Texture* t, const std::string& title, int x, int y, int size) {
    if (t) {
        ui::drawTexture(t, x, y, size, size);
        return;
    }
    ui::fillRound(x, y, size, size, size / 6, {60, 68, 110, 255});
    std::string letter = "?";
    if (!title.empty() && (unsigned char)title[0] < 0x80 && isalnum((unsigned char)title[0]))
        letter = std::string(1, (char)toupper((unsigned char)title[0]));
    ui::textCenter(letter, x + size / 2, y + size / 2 - (size > 100 ? 34 : 17), cText, size > 100 ? ui::Huge : ui::Big);
}

void drawHeader() {
    ui::text("saveman", 48, 20, cAccent, ui::Big);

    static const char* names[3] = {"Salvataggi", "Su Drive", "Sulla microSD"};
    int x = 262;
    ui::fillRound(x, 28, 30, 30, 9, {58, 66, 110, 255});
    ui::textCenter("L", x + 15, 31, cText, ui::Small);
    x += 42;
    for (int t = 0; t < 3; t++) {
        std::string label = names[t];
        int n = (t == TabDrive && !app.remotesLoaded) ? -1 : tabCount(t);
        if (n >= 0) label += "  " + std::to_string(n);
        int w = ui::textWidth(label, ui::Small) + 36;
        bool act = (t == app.tab);
        ui::fillRound(x, 22, w, 42, 21, act ? cAccent : cCard);
        ui::textCenter(label, x + w / 2, 31, act ? Color{14, 18, 36, 255} : cDim, ui::Small);
        x += w + 8;
    }
    ui::fillRound(x + 2, 28, 30, 30, 9, {58, 66, 110, 255});
    ui::textCenter("R", x + 17, 31, cText, ui::Small);

    std::string label = app.linked() ? "Drive collegato" : "Drive non collegato";
    int w = ui::textWidth(label, ui::Small) + 56;
    int cx = 1240 - w;
    ui::fillRound(cx, 22, w, 42, 21, cCard);
    ui::fillRound(cx + 18, 37, 12, 12, 6, app.linked() ? cGood : cWarn);
    ui::text(label, cx + 40, 31, cText, ui::Small);
}

void drawEmptyState() {
    std::string a, b;
    if (app.tab == TabSaves) {
        a = "Nessun salvataggio trovato";
        b = "Avvia un gioco almeno una volta per creare il salvataggio.";
    } else if (app.tab == TabDrive) {
        if (!app.linked()) {
            a = "Google Drive non collegato";
            b = "Premi A per collegarlo e vedere i tuoi backup.";
        } else if (!app.remotesError.empty()) {
            a = "Impossibile leggere il Drive";
            b = app.remotesError + "  -  premi Y per riprovare.";
        } else if (!app.remotesLoaded) {
            a = "Backup sul Drive";
            b = "Premi Y per caricare l'elenco.";
        } else {
            a = "Nessun backup sul Drive";
            b = "Carica un backup dalla scheda Salvataggi.";
        }
    } else {
        a = "Nessun backup sulla microSD";
        b = "I backup che crei (o scarichi) compaiono qui.";
    }
    ui::textCenter(a, 430, 290, cText, ui::Big);
    ui::textCenter(ui::fit(b, 740, ui::Normal), 430, 340, cDim, ui::Normal);
}

void drawPanel() {
    ui::fillRound(840, kListY, 400, 508, 20, cCard);
    int n = tabCount(app.tab);
    int cx = 1040;
    if (n == 0) {
        if (app.tab == TabLocal) {
            ui::textCenter("Spazio backup", cx, kListY + 40, cDim, ui::Small);
        }
    } else {
        RowView rv = rowView(app.tab, app.ls[app.tab].cursor);
        drawIcon(rv.icon, rv.title, cx - 80, kListY + 24, 160);
        ui::textCenter(ui::fit(rv.title, 360, ui::Normal), cx, kListY + 200, cText, ui::Normal);
        ui::textCenter(ui::fit(rv.sub, 360, ui::Small), cx, kListY + 240, cDim, ui::Small);
        if (app.tab == TabSaves) {
            ui::textCenter("ID " + hex16(app.saves[app.ls[0].cursor].titleId), cx, kListY + 270, cDim, ui::Small);
        } else {
            ui::textCenter(rv.right, cx, kListY + 270, cDim, ui::Small);
        }
    }

    int sel = 0;
    for (int i = 0; i < n; i++) if (selRef(app.tab, i)) sel++;
    ui::textCenter(sel ? std::to_string(sel) + " selezionati" : "Nessuna selezione", cx, kListY + 318,
                   sel ? cAccent : cDim, ui::Small);

    if (app.tab == TabLocal) {
        char b[96];
        snprintf(b, sizeof b, "%d backup  -  %s", (int)app.locals.size(), prettySize(localTotalBytes()).c_str());
        ui::textCenter(b, cx, kListY + 396, cText, ui::Small);
        uint64_t fr = 0;
        if (sdFree(fr)) ui::textCenter("Spazio libero: " + prettySize(fr), cx, kListY + 426, cDim, ui::Small);
        std::string rule = app.cfg.localKeep > 0 ? "Pulizia automatica: ultimi " + std::to_string(app.cfg.localKeep) + " per gioco"
                                                 : "Pulizia automatica: disattivata";
        ui::textCenter(rule, cx, kListY + 456, cDim, ui::Small);
    } else if (app.tab == TabSaves) {
        ui::textCenter(sel ? "A: azioni sui selezionati" : "A: azioni su questo gioco", cx, kListY + 350, cDim, ui::Small);
    }
}

void drawMain() {
    ui::background();
    drawHeader();

    int t = app.tab;
    int n = tabCount(t);
    if (n == 0) drawEmptyState();
    ListState& l = app.ls[t];
    for (int i = l.scroll; i < n && i < l.scroll + kVisible; i++) {
        RowView rv = rowView(t, i);
        int y = kListY + (i - l.scroll) * kRowPitch;
        bool cur = (i == l.cursor);
        if (cur) ui::fillRound(37, y - 3, 766, kRowH + 6, 20, cAccent);
        ui::fillRound(40, y, 760, kRowH, 17, cur ? cCardSel : cCard);

        ui::fillRound(58, y + 25, 28, 28, 8, rv.selected ? cAccent : Color{70, 80, 125, 255});
        if (!rv.selected) ui::fillRound(61, y + 28, 22, 22, 6, cur ? cCardSel : cCard);

        drawIcon(rv.icon, rv.title, 104, y + 7, 64);
        int maxW = rv.right.empty() ? 590 : 480;
        ui::text(ui::fit(rv.title, maxW, ui::Normal), 188, y + 8, cText, ui::Normal);
        ui::text(ui::fit(rv.sub, maxW, ui::Small), 188, y + 44, cDim, ui::Small);
        if (!rv.right.empty()) ui::textRight(rv.right, 780, y + 26, cDim, ui::Small);
    }
    if (n > kVisible) {
        int trackH = kVisible * kRowPitch - 8;
        int thumbH = std::max(30, trackH * kVisible / n);
        int thumbY = kListY + (trackH - thumbH) * l.scroll / std::max(1, n - kVisible);
        ui::fillRound(812, kListY, 6, trackH, 3, {36, 42, 74, 255});
        ui::fillRound(812, thumbY, 6, thumbH, 3, cAccent);
    }

    drawPanel();

    int x = 48;
    x = ui::hint(x, 650, "L/R", "Schede");
    x = ui::hint(x, 650, "A", t == TabDrive && !app.linked() ? "Collega" : "Azioni");
    x = ui::hint(x, 650, "X", "Seleziona");
    x = ui::hint(x, 650, "Y", t == TabSaves ? "Tutti" : (t == TabDrive ? "Aggiorna" : "Pulisci"));
    x = ui::hint(x, 650, "-", "Impostazioni");
    ui::hint(x, 650, "+", "Esci");
}

// ---------------------------------------------------------------- finestre modali
void dim() { ui::fillRect(0, 0, 1280, 720, {5, 6, 14, 190}); }

void drawCard(const std::string& title, Color tc, const std::vector<std::string>& lines, int extra) {
    int h = 130 + (int)lines.size() * 34 + extra;
    int y = (720 - h) / 2;
    ui::fillRound(220, y, 840, h, 24, cModal);
    ui::text(title, 260, y + 26, tc, ui::Big);
    for (size_t i = 0; i < lines.size(); i++) ui::text(lines[i], 260, y + 86 + (int)i * 34, cText, ui::Normal);
}

void showMessage(const std::string& title, Color tc, const std::string& body) {
    auto lines = ui::wrap(body, 760, ui::Normal);
    if (lines.size() > 9) lines.resize(9);
    while (appletMainLoop()) {
        padUpdate(&app.pad);
        if (padGetButtonsDown(&app.pad) & (HidNpadButton_A | HidNpadButton_B)) break;
        drawMain();
        dim();
        drawCard(title, tc, lines, 70);
        int h = 130 + (int)lines.size() * 34 + 70;
        ui::hint(260, (720 - h) / 2 + h - 56, "A", "OK");
        ui::present();
    }
}

bool confirmBox(const std::string& title, Color tc, const std::string& body, const std::string& yes) {
    auto lines = ui::wrap(body, 760, ui::Normal);
    if (lines.size() > 9) lines.resize(9);
    while (appletMainLoop()) {
        padUpdate(&app.pad);
        u64 down = padGetButtonsDown(&app.pad);
        if (down & HidNpadButton_A) return true;
        if (down & HidNpadButton_B) return false;
        drawMain();
        dim();
        drawCard(title, tc, lines, 70);
        int h = 130 + (int)lines.size() * 34 + 70;
        int y = (720 - h) / 2 + h - 56;
        int x = ui::hint(260, y, "A", yes);
        ui::hint(x, y, "B", "Annulla");
        ui::present();
    }
    return false;
}

// Menu a scelta singola. Ritorna l'indice, oppure -1 con B.
int menuBox(const std::string& title, const std::vector<std::string>& items, const std::string& subtitle = "") {
    int sel = 0, scroll = 0;
    const int maxVis = 7;
    const int n = (int)items.size();
    while (appletMainLoop()) {
        padUpdate(&app.pad);
        u64 down = padGetButtonsDown(&app.pad);
        if (down & HidNpadButton_B) return -1;
        if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) sel = std::min(n - 1, sel + 1);
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) sel = std::max(0, sel - 1);
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + maxVis) scroll = sel - maxVis + 1;
        if (down & HidNpadButton_A) return sel;

        int vis = std::min(n, maxVis);
        int top = 88 + (subtitle.empty() ? 0 : 34);
        int h = top + vis * 56 + 80;
        int x0 = 250, w = 780, y0 = (720 - h) / 2;
        drawMain();
        dim();
        ui::fillRound(x0, y0, w, h, 24, cModal);
        ui::text(title, x0 + 36, y0 + 22, cAccent, ui::Big);
        if (!subtitle.empty()) ui::text(ui::fit(subtitle, w - 72, ui::Small), x0 + 36, y0 + 72, cDim, ui::Small);
        for (int i = scroll; i < n && i < scroll + maxVis; i++) {
            int ry = y0 + top + (i - scroll) * 56;
            bool danger = items[i].rfind("Elimina", 0) == 0;
            if (i == sel) ui::fillRound(x0 + 28, ry, w - 56, 48, 14, {52, 66, 128, 255});
            if (i == sel) ui::fillRound(x0 + 28, ry + 8, 5, 32, 3, cAccent);
            ui::text(ui::fit(items[i], w - 120, ui::Normal), x0 + 52, ry + 9, danger ? cBad : cText, ui::Normal);
        }
        int hx = ui::hint(x0 + 36, y0 + h - 56, "A", "Scegli");
        ui::hint(hx, y0 + h - 56, "B", "Chiudi");
        ui::present();
    }
    return -1;
}

void loadingFrame(const std::string& msg) {
    drawMain();
    dim();
    drawCard(msg, cAccent, {}, 20);
    ui::present();
}

// ---------------------------------------------------------------- progresso
struct Progress {
    std::string title, label, info;
    Clock::time_point last = Clock::now() - std::chrono::seconds(1);

    // false = l'utente tiene premuto B per annullare
    bool tick(double frac, bool force = false) {
        padUpdate(&app.pad);
        if (padGetButtons(&app.pad) & HidNpadButton_B) return false;
        auto now = Clock::now();
        if (!force && now - last < std::chrono::milliseconds(60)) return true;
        last = now;
        frac = std::max(0.0, std::min(1.0, frac));

        drawMain();
        dim();
        std::vector<std::string> lines = {ui::fit(label, 760, ui::Normal), info};
        drawCard(title, cAccent, lines, 80);
        int h = 130 + 2 * 34 + 80, y = (720 - h) / 2;
        ui::fillRound(260, y + h - 70, 760, 18, 9, {50, 58, 100, 255});
        int w = (int)(760 * frac);
        if (w > 18) ui::fillRound(260, y + h - 70, w, 18, 9, cAccent);
        ui::text("Tieni premuto B per annullare", 260, y + h - 40, cDim, ui::Small);
        ui::present();
        return true;
    }
};

std::string bytesInfo(double done, double total) { return prettySize((uint64_t)done) + " / " + prettySize((uint64_t)total); }

// ---------------------------------------------------------------- login Google
bool runAuth() {
    DriveAuthPrompt p;
    std::string err;
    if (!driveStartAuth(app.cfg, p, err)) {
        showMessage("Collegamento non riuscito", cBad, err);
        return false;
    }
    auto start = Clock::now();
    auto lastPoll = start;
    while (appletMainLoop()) {
        padUpdate(&app.pad);
        if (padGetButtonsDown(&app.pad) & HidNpadButton_B) return false;

        int left = p.expiresIn - (int)std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start).count();
        if (left <= 0) {
            showMessage("Codice scaduto", cWarn, "Il codice non e' stato inserito in tempo. Riprova.");
            return false;
        }

        drawMain();
        dim();
        ui::fillRound(180, 130, 920, 460, 24, cModal);
        ui::text("Collega Google Drive", 220, 156, cAccent, ui::Big);
        ui::text("1. Sul telefono apri questo indirizzo:", 220, 232, cText, ui::Normal);
        ui::text(p.url, 250, 270, cAccent, ui::Big);
        ui::text("2. Inserisci questo codice:", 220, 340, cText, ui::Normal);
        ui::fillRound(250, 380, 420, 90, 18, {20, 24, 46, 255});
        ui::textCenter(p.userCode, 460, 395, cText, ui::Huge);
        ui::text("3. Accedi con il tuo account e consenti l'accesso.", 220, 494, cText, ui::Normal);
        char b[64];
        snprintf(b, sizeof b, "In attesa... scade tra %d:%02d", left / 60, left % 60);
        ui::text(b, 220, 540, cDim, ui::Small);
        ui::hint(800, 536, "B", "Annulla");
        ui::present();

        if (Clock::now() - lastPoll >= std::chrono::seconds(p.interval)) {
            lastPoll = Clock::now();
            PollResult r = driveAuthPoll(app.cfg, p, err);
            if (r == PollResult::Done) {
                saveConfig(app.cfg);
                return true;
            }
            if (r == PollResult::Failed) {
                showMessage("Collegamento non riuscito", cBad, err);
                return false;
            }
        }
        svcSleepThread(50'000'000);
    }
    return false;
}

bool getAccessToken(std::string& token) {
    std::string err;
    if (app.cfg.refreshToken.empty() && !runAuth()) return false;
    if (driveRefresh(app.cfg, token, err)) return true;
    if (err == "invalid_grant") {  // token revocato o scaduto: nuovo login
        app.cfg.refreshToken.clear();
        saveConfig(app.cfg);
        if (!runAuth()) return false;
        if (driveRefresh(app.cfg, token, err)) return true;
    }
    showMessage("Accesso a Google non riuscito", cBad, err);
    return false;
}

// Controlli comuni prima di parlare con Drive: configurazione, rete, login, cartella.
bool prepareDrive(std::string& token, std::string& folderId) {
    if (app.cfg.clientId.empty() || app.cfg.clientSecret.empty()) {
        showMessage("Configurazione mancante", cWarn,
                    "Apri sdmc:/switch/saveman/config.json dalla microSD e inserisci client_id e client_secret "
                    "(vedi README). Poi riavvia saveman.");
        return false;
    }
    if (!hasInternet()) {
        showMessage("Nessuna connessione", cWarn, "La Switch non e' connessa a internet. Controlla il Wi-Fi.");
        return false;
    }
    if (!getAccessToken(token)) return false;
    if (app.folderId.empty()) {
        std::string err;
        if (!driveEnsureFolder(token, app.cfg.folderName, app.folderId, err)) {
            showMessage("Cartella Drive non disponibile", cBad, err);
            app.folderId.clear();
            return false;
        }
    }
    folderId = app.folderId;
    return true;
}

bool loadRemotes() {
    loadingFrame("Lettura dei backup dal Drive...");
    std::string token, folder, err;
    if (!prepareDrive(token, folder)) {
        app.remotesError = "accesso a Drive non riuscito";
        return false;
    }
    std::vector<RemoteBackup> list;
    if (!driveListBackups(token, folder, list, err)) {
        app.remotesError = err;
        showMessage("Impossibile leggere il Drive", cBad, err);
        return false;
    }
    app.remotes = std::move(list);
    app.remotesLoaded = true;
    app.remotesError.clear();
    app.ls[TabDrive] = ListState();
    return true;
}

// ---------------------------------------------------------------- operazioni
void summary(const std::string& okTitle, const std::string& warnTitle, const std::string& line,
             const std::vector<std::string>& errors) {
    std::string msg = line;
    if (!errors.empty())
        msg += "\n\nErrori:\n" + errors.front() +
               (errors.size() > 1 ? "\n(+" + std::to_string(errors.size() - 1) + " altri)" : "");
    showMessage(errors.empty() ? okTitle : warnTitle, errors.empty() ? cGood : cWarn, msg);
}

void runBackup(const std::vector<SaveEntry*>& targets, bool upload) {
    if (targets.empty()) return;
    std::string token, folderId, err;
    if (upload && !prepareDrive(token, folderId)) return;

    ensureDirs();
    int okCount = 0;
    std::vector<std::string> errors;
    Progress prog;

    for (size_t i = 0; i < targets.size(); i++) {
        SaveEntry& s = *targets[i];
        std::string counter = targets.size() > 1 ? " (" + std::to_string(i + 1) + "/" + std::to_string(targets.size()) + ")" : "";
        std::string name = makeBackupName(s.title, s.titleId, s.user, timestamp());
        std::string zipPath = std::string(kBackupsDir) + "/" + name;

        prog.title = "Backup in corso" + counter;
        prog.label = s.title;
        prog.info = "Lettura del salvataggio...";
        prog.tick(0, true);
        bool ok = backupSave(s, zipPath, [&](const std::string& file, uint64_t done, uint64_t total) {
            prog.label = s.title + "  -  " + file;
            prog.info = bytesInfo((double)done, (double)total);
            return prog.tick(total ? (double)done / (double)total : 0);
        }, err);
        if (!ok) { errors.push_back(s.title + ": " + err); continue; }
        if (!upload) { okCount++; continue; }

        std::string tk;
        if (!driveRefresh(app.cfg, tk, err)) { errors.push_back(s.title + ": " + err); continue; }
        prog.title = "Upload su Drive" + counter;
        prog.label = s.title;
        prog.info = "Connessione...";
        prog.tick(0, true);
        ok = driveUpload(tk, folderId, zipPath, name, [&](double done, double total) {
            prog.info = bytesInfo(done, total);
            return prog.tick(total > 0 ? done / total : 0);
        }, err);
        if (ok) {
            okCount++;
            if (!app.cfg.keepLocal) remove(zipPath.c_str());
        } else {
            errors.push_back(s.title + ": " + err);
            app.folderId.clear();  // se la cartella e' stata eliminata dal Drive, la ricrea al giro dopo
        }
    }

    uint64_t freed = 0;
    int pruned = autoPrune(&freed);
    refreshLocals();
    if (upload) app.remotesLoaded = false;
    clearSelection(TabSaves);

    std::string line = upload ? std::to_string(okCount) + " di " + std::to_string(targets.size()) +
                                    " backup caricati su Drive (cartella \"" + app.cfg.folderName + "\")."
                              : std::to_string(okCount) + " di " + std::to_string(targets.size()) +
                                    " backup salvati sulla microSD.";
    if (pruned > 0) line += "\nPulizia automatica: eliminati " + std::to_string(pruned) + " vecchi backup (" + prettySize(freed) + ").";
    summary("Backup completato", "Backup terminato con errori", line, errors);
}

void runUploadLocal(const std::vector<int>& idx) {
    std::string token, folderId, err;
    if (!prepareDrive(token, folderId)) return;
    int okCount = 0;
    std::vector<std::string> errors;
    Progress prog;
    for (size_t k = 0; k < idx.size(); k++) {
        LocalBackup& b = app.locals[idx[k]];
        std::string tk;
        if (!driveRefresh(app.cfg, tk, err)) { errors.push_back(b.name + ": " + err); continue; }
        prog.title = "Upload su Drive" + (idx.size() > 1 ? " (" + std::to_string(k + 1) + "/" + std::to_string(idx.size()) + ")" : "");
        prog.label = b.name;
        prog.info = "Connessione...";
        prog.tick(0, true);
        bool ok = driveUpload(tk, folderId, b.path, b.name, [&](double done, double total) {
            prog.info = bytesInfo(done, total);
            return prog.tick(total > 0 ? done / total : 0);
        }, err);
        if (ok) okCount++;
        else { errors.push_back(b.name + ": " + err); app.folderId.clear(); }
    }
    clearSelection(TabLocal);
    app.remotesLoaded = false;
    summary("Upload completato", "Upload terminato con errori",
            std::to_string(okCount) + " di " + std::to_string(idx.size()) + " backup caricati su Drive.", errors);
}

void deleteLocal(const std::vector<int>& idx) {
    std::string what = idx.size() == 1 ? "\"" + app.locals[idx[0]].name + "\"" : std::to_string(idx.size()) + " backup";
    if (!confirmBox("Eliminare dalla microSD?", cBad, what + " verra' eliminato definitivamente dalla microSD. "
                    "Le copie su Google Drive non vengono toccate.", "Elimina"))
        return;
    int n = 0;
    for (int i : idx) if (remove(app.locals[i].path.c_str()) == 0) n++;
    refreshLocals();
    clearSelection(TabLocal);
    showMessage("Fatto", cGood, std::to_string(n) + " backup eliminati dalla microSD.");
}

void cleanupLocal() {
    if (app.cfg.localKeep <= 0) {
        showMessage("Pulizia disattivata", cWarn,
                    "Imposta quanti backup tenere per gioco da Impostazioni (tasto -), poi riprova.");
        return;
    }
    PrunePlan plan = planPrune(app.locals, app.cfg.localKeep);
    if (plan.paths.empty()) {
        showMessage("Niente da pulire", cGood, "Hai gia' al massimo " + std::to_string(app.cfg.localKeep) + " backup per gioco.");
        return;
    }
    if (!confirmBox("Pulire i vecchi backup?", cWarn,
                    "Tengo gli ultimi " + std::to_string(app.cfg.localKeep) + " backup per ogni gioco. Verranno eliminati " +
                        std::to_string(plan.paths.size()) + " file e liberati " + prettySize(plan.bytes) + " sulla microSD.",
                    "Pulisci"))
        return;
    int n = 0;
    for (const auto& p : plan.paths) if (remove(p.c_str()) == 0) n++;
    refreshLocals();
    showMessage("Pulizia completata", cGood, std::to_string(n) + " backup eliminati (" + prettySize(plan.bytes) + " liberati).");
}

void deleteRemote(const std::vector<int>& idx) {
    std::string what = idx.size() == 1 ? "\"" + app.remotes[idx[0]].name + "\"" : std::to_string(idx.size()) + " backup";
    if (!confirmBox("Eliminare dal Drive?", cBad, what + " verra' eliminato dal tuo Google Drive. Le copie sulla microSD non vengono toccate.",
                    "Elimina"))
        return;
    std::string token, folderId, err;
    if (!prepareDrive(token, folderId)) return;
    int ok = 0;
    std::vector<std::string> errors;
    for (int i : idx) {
        loadingFrame("Eliminazione dal Drive...");
        std::string tk;
        if (!driveRefresh(app.cfg, tk, err)) { errors.push_back(err); continue; }
        if (driveDelete(tk, app.remotes[i].id, err)) ok++;
        else errors.push_back(app.remotes[i].name + ": " + err);
    }
    loadRemotes();
    summary("Eliminazione completata", "Eliminazione con errori", std::to_string(ok) + " backup eliminati dal Drive.", errors);
}

// scarica nella cartella dei backup; outPath = file finale
bool downloadOne(const RemoteBackup& r, const std::string& token, Progress& prog, std::string& outPath, std::string& err) {
    ensureDirs();
    outPath = std::string(kBackupsDir) + "/" + safeFileName(r.name);
    struct stat st;
    if (stat(outPath.c_str(), &st) == 0 && (uint64_t)st.st_size == r.size && r.size > 0) return true;  // gia' presente
    prog.label = r.name;
    prog.info = "Connessione...";
    prog.tick(0, true);
    return driveDownload(token, r.id, outPath, r.size, [&](double done, double total) {
        prog.info = bytesInfo(done, total > 0 ? total : (double)r.size);
        return prog.tick((total > 0 ? total : (double)r.size) > 0 ? done / (total > 0 ? total : (double)r.size) : 0);
    }, err);
}

SaveEntry* chooseRestoreTarget(const LocalBackup& b) {
    std::vector<SaveEntry*> cands;
    std::string sub;
    if (b.info.hasTid) {
        for (auto& s : app.saves) if (s.titleId == b.info.tid) cands.push_back(&s);
        if (cands.empty()) {
            showMessage("Salvataggio non trovato", cWarn,
                        "Su questa console non esiste ancora un salvataggio per \"" + b.info.title + "\". Avvia il gioco "
                        "almeno una volta con l'utente di destinazione, poi riprova.");
            return nullptr;
        }
        std::vector<SaveEntry*> same;
        for (auto* c : cands) if (c->user == b.info.user) same.push_back(c);
        if (same.size() == 1) return same[0];
        if (cands.size() == 1) return cands[0];
        sub = "Il backup e' di \"" + b.info.user + "\": scegli l'utente di destinazione";
    } else {
        for (auto& s : app.saves) cands.push_back(&s);
        if (cands.empty()) {
            showMessage("Nessun salvataggio", cWarn, "Non ci sono salvataggi su cui ripristinare.");
            return nullptr;
        }
        sub = "Backup senza ID del gioco: scegli tu la destinazione (attenzione al gioco giusto!)";
    }
    std::vector<std::string> labels;
    for (auto* c : cands) labels.push_back(c->title + "  -  " + c->user);
    int pick = menuBox("Dove ripristinare?", labels, sub);
    return pick < 0 ? nullptr : cands[pick];
}

void runRestore(LocalBackup b) {  // per valore: la lista dei backup viene ricaricata durante l'operazione
    SaveEntry* t = chooseRestoreTarget(b);
    if (!t) return;

    std::string body = "Backup: " + (b.info.valid ? b.info.title + " (" + prettyStamp(b.info.stamp) + ")" : b.name) +
                       "\nDestinazione: " + t->title + " - " + t->user +
                       "\n\nIl salvataggio attuale verra' sostituito. Prima ne viene creata una copia di sicurezza sulla microSD.";
    if (!confirmBox("Ripristinare questo backup?", cWarn, body, "Ripristina")) return;

    std::string err;
    Progress prog;

    // 1. controllo di integrita' dello zip (prima di toccare qualsiasi cosa)
    prog.title = "Controllo del backup";
    prog.label = b.name;
    prog.info = "Verifica dei file...";
    prog.tick(0, true);
    if (!verifyZip(b.path, [&](const std::string& f, uint64_t d, uint64_t tot) {
            prog.label = f;
            prog.info = bytesInfo((double)d, (double)tot);
            return prog.tick(tot ? (double)d / (double)tot : 0);
        }, err)) {
        showMessage("Backup non valido", cBad, err + "\n\nIl salvataggio non e' stato toccato.");
        return;
    }

    // 2. copia di sicurezza del salvataggio attuale
    std::string safety = makeBackupName(t->title, t->titleId, t->user, timestamp(), true);
    std::string safetyPath = std::string(kBackupsDir) + "/" + safety;
    prog.title = "Copia di sicurezza";
    prog.label = t->title;
    prog.info = "Salvo lo stato attuale...";
    prog.tick(0, true);
    bool haveSafety = backupSave(*t, safetyPath, [&](const std::string& f, uint64_t d, uint64_t tot) {
        prog.label = f;
        prog.info = bytesInfo((double)d, (double)tot);
        return prog.tick(tot ? (double)d / (double)tot : 0);
    }, err, true);
    if (!haveSafety) {
        if (!confirmBox("Copia di sicurezza non riuscita", cBad,
                        err + "\n\nVuoi ripristinare comunque, SENZA copia di sicurezza?", "Continua"))
            return;
    }

    // 3. ripristino vero e proprio
    prog.title = "Ripristino in corso";
    prog.label = b.name;
    prog.info = "Scrittura del salvataggio...";
    prog.tick(0, true);
    bool ok = restoreSave(*t, b.path, [&](const std::string& f, uint64_t d, uint64_t tot) {
        prog.label = f;
        prog.info = bytesInfo((double)d, (double)tot);
        return prog.tick(tot ? (double)d / (double)tot : 0);
    }, err);

    autoPrune();
    refreshLocals();
    if (ok) {
        showMessage("Ripristino completato", cGood,
                    "Il salvataggio di \"" + t->title + "\" (" + t->user + ") e' stato ripristinato." +
                        (haveSafety ? "\n\nLa versione precedente e' salvata sulla microSD come copia di sicurezza (scheda Sulla microSD)." : ""));
    } else {
        showMessage("Ripristino non riuscito", cBad,
                    err + "\n\nIl salvataggio non e' stato modificato (nessun commit)." +
                        (haveSafety ? " Hai comunque la copia di sicurezza sulla microSD." : ""));
    }
}

void runDownload(const std::vector<int>& idx, bool thenRestore) {
    std::string token, folderId, err;
    if (!prepareDrive(token, folderId)) return;
    Progress prog;
    int ok = 0;
    std::vector<std::string> errors;
    std::string lastPath;
    for (size_t k = 0; k < idx.size(); k++) {
        const RemoteBackup& r = app.remotes[idx[k]];
        std::string tk;
        if (!driveRefresh(app.cfg, tk, err)) { errors.push_back(r.name + ": " + err); continue; }
        prog.title = "Download dal Drive" + (idx.size() > 1 ? " (" + std::to_string(k + 1) + "/" + std::to_string(idx.size()) + ")" : "");
        std::string path;
        if (downloadOne(r, tk, prog, path, err)) { ok++; lastPath = path; }
        else errors.push_back(r.name + ": " + err);
    }
    refreshLocals();
    clearSelection(TabDrive);

    if (thenRestore && ok == 1 && errors.empty()) {
        for (const auto& b : app.locals) {
            if (b.path == lastPath) { runRestore(b); return; }
        }
    }
    summary("Download completato", "Download terminato con errori",
            std::to_string(ok) + " di " + std::to_string(idx.size()) + " backup scaricati nella scheda \"Sulla microSD\".", errors);
}

// ---------------------------------------------------------------- azioni per scheda
void actionsSaves() {
    if (app.saves.empty()) return;
    std::vector<int> idx = targetsOf(TabSaves);
    std::vector<SaveEntry*> targets;
    for (int i : idx) targets.push_back(&app.saves[i]);
    std::string sub = targets.size() == 1 ? targets[0]->title + "  -  " + targets[0]->user
                                          : std::to_string(targets.size()) + " salvataggi selezionati";
    int c = menuBox("Cosa vuoi fare?", {"Backup e carica su Drive", "Solo backup sulla microSD"}, sub);
    if (c == 0) runBackup(targets, true);
    else if (c == 1) runBackup(targets, false);
}

void actionsLocal() {
    if (app.locals.empty()) return;
    std::vector<int> idx = targetsOf(TabLocal);
    bool single = idx.size() == 1;
    std::string sub = single ? app.locals[idx[0]].name : std::to_string(idx.size()) + " backup selezionati";
    std::vector<std::string> items;
    if (single) items = {"Ripristina nel gioco", "Carica su Drive", "Elimina dalla microSD"};
    else items = {"Carica su Drive (" + std::to_string(idx.size()) + ")", "Elimina dalla microSD (" + std::to_string(idx.size()) + ")"};
    int c = menuBox("Cosa vuoi fare?", items, sub);
    if (c < 0) return;
    if (single) {
        if (c == 0) runRestore(app.locals[idx[0]]);
        else if (c == 1) runUploadLocal(idx);
        else deleteLocal(idx);
    } else {
        if (c == 0) runUploadLocal(idx);
        else deleteLocal(idx);
    }
}

void actionsDrive() {
    if (!app.linked()) {
        if (app.cfg.clientId.empty() || app.cfg.clientSecret.empty()) {
            std::string t, f;
            prepareDrive(t, f);  // mostra il messaggio di configurazione mancante
            return;
        }
        if (runAuth()) loadRemotes();
        return;
    }
    if (!app.remotesLoaded) {
        loadRemotes();
        return;
    }
    if (app.remotes.empty()) return;
    std::vector<int> idx = targetsOf(TabDrive);
    bool single = idx.size() == 1;
    std::string sub = single ? app.remotes[idx[0]].name : std::to_string(idx.size()) + " backup selezionati";
    std::vector<std::string> items;
    if (single) items = {"Scarica e ripristina nel gioco", "Scarica sulla microSD", "Elimina dal Drive"};
    else items = {"Scarica sulla microSD (" + std::to_string(idx.size()) + ")", "Elimina dal Drive (" + std::to_string(idx.size()) + ")"};
    int c = menuBox("Cosa vuoi fare?", items, sub);
    if (c < 0) return;
    if (single) {
        if (c == 0) runDownload(idx, true);
        else if (c == 1) runDownload(idx, false);
        else deleteRemote(idx);
    } else {
        if (c == 0) runDownload(idx, false);
        else deleteRemote(idx);
    }
}

void settingsMenu() {
    for (;;) {
        std::string keep = app.cfg.localKeep > 0 ? std::to_string(app.cfg.localKeep) : "nessun limite";
        std::vector<std::string> items = {
            app.linked() ? "Account Google: collegato (scollega)" : "Account Google: non collegato",
            "Backup da tenere per gioco sulla microSD: " + keep,
            std::string("Dopo l'upload: ") + (app.cfg.keepLocal ? "tieni lo zip sulla microSD" : "elimina lo zip dalla microSD"),
            "Informazioni su saveman",
        };
        int c = menuBox("Impostazioni", items);
        if (c < 0) return;
        if (c == 0) {
            if (!app.linked()) {
                showMessage("Drive non collegato", cDim, "Per collegarlo vai nella scheda \"Su Drive\" e premi A.");
            } else if (confirmBox("Scollegare Google?", cWarn, "Il token di accesso verra' cancellato dalla microSD. Dovrai rifare il login.", "Scollega")) {
                app.cfg.refreshToken.clear();
                app.remotes.clear();
                app.remotesLoaded = false;
                app.folderId.clear();
                saveConfig(app.cfg);
            }
        } else if (c == 1) {
            static const int steps[] = {0, 1, 2, 3, 5, 10};
            int next = steps[0];
            for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++)
                if (steps[i] == app.cfg.localKeep) { next = steps[(i + 1) % (sizeof(steps) / sizeof(steps[0]))]; break; }
                else if (steps[i] > app.cfg.localKeep) { next = steps[i]; break; }
            app.cfg.localKeep = next;
            saveConfig(app.cfg);
        } else if (c == 2) {
            app.cfg.keepLocal = !app.cfg.keepLocal;
            saveConfig(app.cfg);
        } else {
            showMessage("saveman " + std::string(kVersion), cAccent,
                        "Backup, download e ripristino dei salvataggi con Google Drive.\n"
                        "Codice scritto in collaborazione con Claude (Anthropic).\n"
                        "Licenza MIT - vedi il README.");
        }
    }
}

void enterTab(int t) {
    app.tab = t;
    clampCursor(t);
    if (t == TabDrive && app.linked() && !app.remotesLoaded) {
        if (hasInternet()) loadRemotes();
        else app.remotesError = "nessuna connessione a internet";
    }
    if (t == TabLocal) refreshLocals();
}

void loadingScreen(const std::string& s) {
    ui::background();
    ui::textCenter("saveman", 640, 250, cAccent, ui::Huge);
    ui::textCenter(s, 640, 340, cDim, ui::Normal);
    ui::present();
}
}  // namespace

int main() {
    romfsInit();
    socketInitializeDefault();
    nifmInitialize(NifmServiceType_User);
    setInitialize();
    accountInitialize(AccountServiceType_System);
    nsInitialize();
    plInitialize(PlServiceType_User);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&app.pad);

    if (!ui::init()) {
        ui::shutdown();
        return 1;
    }

    ensureDirs();
    app.cfgFound = loadConfig(app.cfg);
    loadingScreen("Lettura dei salvataggi...");
    app.saves = listSaves();
    refreshLocals();

    if (!app.cfgFound || app.cfg.clientId.empty() || app.cfg.clientSecret.empty())
        showMessage("Prima configurazione", cWarn,
                    "Per usare Google Drive apri sdmc:/switch/saveman/config.json dalla microSD e inserisci client_id e "
                    "client_secret (vedi README). Intanto puoi gia' sfogliare i salvataggi e fare backup sulla microSD.");

    int holdFrames = 0;
    while (appletMainLoop()) {
        padUpdate(&app.pad);
        u64 down = padGetButtonsDown(&app.pad);
        u64 held = padGetButtons(&app.pad);
        int t = app.tab;

        if (down & HidNpadButton_Plus) break;

        if (down & HidNpadButton_L) enterTab((t + 2) % 3);
        if (down & HidNpadButton_R) enterTab((t + 1) % 3);
        t = app.tab;

        if (down & (HidNpadButton_Down | HidNpadButton_StickLDown)) moveCursor(t, 1);
        if (down & (HidNpadButton_Up | HidNpadButton_StickLUp)) moveCursor(t, -1);
        if (down & HidNpadButton_Left) moveCursor(t, -kVisible);
        if (down & HidNpadButton_Right) moveCursor(t, kVisible);
        if (held & (HidNpadButton_Down | HidNpadButton_Up | HidNpadButton_StickLDown | HidNpadButton_StickLUp)) {
            holdFrames++;
            if (holdFrames > 20 && holdFrames % 5 == 0)
                moveCursor(t, (held & (HidNpadButton_Down | HidNpadButton_StickLDown)) ? 1 : -1);
        } else {
            holdFrames = 0;
        }

        int n = tabCount(t);
        if (n > 0 && (down & HidNpadButton_X)) selRef(t, app.ls[t].cursor) = !selRef(t, app.ls[t].cursor);

        if (down & HidNpadButton_Y) {
            if (t == TabSaves && n > 0) {
                bool all = std::all_of(app.saves.begin(), app.saves.end(), [](const SaveEntry& s) { return s.selected; });
                for (auto& s : app.saves) s.selected = !all;
            } else if (t == TabDrive) {
                if (app.linked()) loadRemotes();
            } else if (t == TabLocal) {
                refreshLocals();
                cleanupLocal();
            }
        }

        if (down & HidNpadButton_A) {
            if (t == TabSaves) actionsSaves();
            else if (t == TabDrive) actionsDrive();
            else actionsLocal();
        }

        if (down & HidNpadButton_Minus) settingsMenu();

        drawMain();
        ui::present();
    }

    for (auto& s : app.saves) if (s.icon) SDL_DestroyTexture(s.icon);
    ui::shutdown();
    curl_global_cleanup();
    plExit();
    nsExit();
    accountExit();
    setExit();
    nifmExit();
    socketExit();
    romfsExit();
    return 0;
}
