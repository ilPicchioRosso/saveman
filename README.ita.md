# saveman

**Backup dei salvataggi della Nintendo Switch su Google Drive, direttamente dalla console.**

saveman è un homebrew (`.nro`): scegli un gioco, premi **A**, e il salvataggio viene compresso in uno zip
e caricato in una cartella del tuo Google Drive. Poi lo ritrovi dall'app Drive del telefono.

**Cosa sa fare**
- **Backup** di uno o più giochi, con upload su Google Drive
- **Download** dei backup dal Drive, direttamente sulla Switch
- **Ripristino** di un backup nel gioco, con copia di sicurezza automatica
- **Gestione dello spazio**: pulizia automatica dei vecchi backup sulla microSD

🇬🇧 [English version](README.en.md)

---

## Cosa ti serve

- Una Switch con **custom firmware (Atmosphere)** e Homebrew Menu
- Un **account Google**
- Il **Wi-Fi** acceso sulla Switch
- Un **PC** per preparare la microSD (e per compilare, se non usi il file già pronto)

> ⚠️ Backup di **tuoi** salvataggi, per uso personale. Uso a tuo rischio. Progetto non affiliato a Nintendo.

---

## 1. Installazione (senza compilare)

1. Vai nella sezione **Releases** di questo repository e scarica `saveman.nro`.
2. Scarica anche la cartella `sd_card` di questo repository (contiene `cacert.pem`, vedi sotto).
3. Copia i file sulla microSD così:

```
sd:/
└── switch/
    ├── saveman.nro
    └── saveman/
        └── cacert.pem
```

`cacert.pem` è l'elenco dei certificati che permette alla Switch di collegarsi a Google in modo sicuro.
**Senza questo file vedrai l'errore** `problem with the SSL CA cert (path? access rights?)`.

Se vuoi compilare tu il programma, vai alla sezione [Compilare](#compilare-dai-sorgenti).

---

## 2. Collegare Google Drive (una volta sola, circa 10 minuti)

saveman usa **un progetto Google tuo**: così non c'è nessun server in mezzo e solo tu hai accesso ai tuoi file.
Serve una configurazione da fare una volta sola sul sito di Google. I nomi dei menu possono cambiare un po' nel tempo.

1. Apri <https://console.cloud.google.com> e accedi. In alto clicca sul selettore dei progetti → **Nuovo progetto** → nome `saveman` → **Crea**. Assicurati che sia il progetto selezionato.
2. Menu ☰ → **API e servizi** → **Libreria** → cerca **Google Drive API** → **Abilita**.
3. Menu ☰ → **Google Auth Platform** (o *API e servizi → Schermata consenso OAuth*) → **Inizia**.
   - Nome app: `saveman`
   - Email di assistenza: la tua
   - Pubblico: **Esterno**
   - Email di contatto: la tua
4. Pagina **Branding**. Google chiede questi campi per poter pubblicare l'app:
   - **Nome dell'app**: `saveman`
   - **Email di assistenza**: la tua
   - **URL della home page** e **URL delle norme sulla privacy**: servono due indirizzi web pubblici *qualsiasi*, non verranno mai controllati da nessuno. Puoi usare le due pagine già pronte nella cartella [`docs/`](docs/) attivando GitHub Pages sul tuo repository (Settings → Pages → Branch `main`, cartella `/docs`).
   - **Email dello sviluppatore**: la tua
5. Pagina **Accesso ai dati** (Data Access) → **Aggiungi o rimuovi ambiti** → seleziona `.../auth/drive.file` → **Salva**.
   Questo permesso fa vedere a saveman **solo i file che crea lui**, mai il resto del tuo Drive.
6. Pagina **Pubblico** (Audience) → **Pubblica app** → conferma. Lo stato deve diventare **In produzione**.
   Non serve nessuna verifica da parte di Google.
   > Se la lasci in stato *Test*, tutto funziona ma **dopo 7 giorni** saveman ti richiederà il login.
7. Pagina **Client** → **Crea client** → tipo applicazione: **TV e dispositivi con input limitato** → nome `saveman` → **Crea**.
   Copia **ID client** e **Client secret**: ti servono nel prossimo passo.

---

## 3. Primo avvio

1. Metti la microSD nella Switch e avvia **saveman** dall'Homebrew Menu.
   Ti dirà che manca la configurazione e creerà il file `sd:/switch/saveman/config.json`. Esci con **+**.
2. Rimetti la microSD nel PC e apri `config.json` con il Blocco note. Incolla i due valori tra le virgolette, senza toccare il resto:

```json
{
  "client_id": "123456789-abcdefg.apps.googleusercontent.com",
  "client_secret": "GOCSPX-xxxxxxxxxxxxxxxx",
  "refresh_token": "",
  "folder_name": "Saveman",
  "keep_local": true,
  "local_keep_last": 5
}
```

3. Riavvia saveman, scegli un gioco e premi **A**.
4. Compaiono **un indirizzo e un codice**. Dal telefono apri l'indirizzo, inserisci il codice, accedi col tuo account Google e consenti l'accesso.
   Se compare un avviso "app non verificata": **Avanzate → Continua** (è la tua app, creata da te al punto 2).
5. La Switch carica lo zip. Controlla Google Drive: troverai la cartella **Saveman**.

Il login si fa una volta sola: da quel momento basta premere **A**.

---

## Come si usa

L'app ha tre schede, che cambi con **L** e **R**:

| Scheda | Cosa mostra | Azioni (tasto **A**) |
|---|---|---|
| **Salvataggi** | i salvataggi presenti sulla Switch | backup + upload su Drive, oppure solo backup sulla microSD |
| **Su Drive** | i backup caricati sul tuo Drive | scarica e ripristina, scarica, elimina dal Drive |
| **Sulla microSD** | gli zip salvati sulla microSD | ripristina, carica su Drive, elimina |

| Tasto | Azione |
|---|---|
| ▲ ▼ | scorri l'elenco (◀ ▶ per pagine intere) |
| **L** / **R** | cambia scheda |
| **A** | apre il menu delle azioni (sull'elemento evidenziato, o su quelli selezionati) |
| **X** | seleziona / deseleziona |
| **Y** | Salvataggi: seleziona tutti · Su Drive: aggiorna l'elenco · Sulla microSD: pulisci i vecchi backup |
| **−** | impostazioni (scollega Google, limiti di spazio, informazioni) |
| **+** | esci |
| **B** (tenuto premuto) | annulla durante backup, download o ripristino |

Con **X** puoi selezionare più elementi e fare l'azione su tutti in una volta (per esempio caricare 10 backup, o eliminarli).

---

## Ripristinare un backup

1. Vai nella scheda **Sulla microSD** (oppure **Su Drive**, che prima scarica il file), evidenzia il backup e premi **A** → **Ripristina nel gioco**.
2. saveman capisce da solo a quale gioco e utente appartiene e te lo chiede per conferma.
3. Prima di scrivere qualsiasi cosa fa tre controlli di sicurezza:
   - **verifica l'integrità** dello zip (se è corrotto si ferma, e il salvataggio non viene toccato);
   - **crea una copia di sicurezza** del salvataggio attuale sulla microSD (la riconosci perché ha "(pre-restore)" nel nome);
   - scrive i file e fa il **commit** solo alla fine: se qualcosa va storto prima, le modifiche non vengono salvate.

Cose da sapere:
- Il salvataggio di destinazione **deve già esistere** su questa Switch: se è una console nuova, avvia il gioco una volta con quell'utente e poi ripristina.
- I backup creati da saveman contengono l'ID del gioco nel nome file, quindi la destinazione si sceglie da sola. Gli zip più vecchi o creati da altri programmi (per esempio JKSV) funzionano, ma ti chiede di scegliere tu il salvataggio di destinazione: fai attenzione a scegliere il gioco giusto.
- Chiudi il gioco prima di ripristinare.

---

## Tenere sotto controllo lo spazio

Ogni backup resta anche sulla microSD. Per non riempirla:

- **Pulizia automatica**: dopo ogni backup saveman tiene solo gli **ultimi N backup per gioco** (`local_keep_last`, di default 5). Si cambia dal menu **−** oppure nel `config.json`; `0` = nessun limite.
- **Elimina lo zip dopo l'upload**: nel menu **−** (o `"keep_local": false`) lo zip viene cancellato dalla microSD appena è su Drive. I backup "solo microSD" non vengono mai cancellati da questa opzione.
- **Pulizia manuale**: nella scheda *Sulla microSD* premi **Y**: ti mostra quanti file elimina e quanto spazio libera, e chiede conferma.
- Nella scheda *Sulla microSD* vedi sempre quanto spazio occupano i backup e quanto ne resta libero.

---

## Compilare dai sorgenti

### Windows

1. Installa **devkitPro** con l'installer: <https://github.com/devkitPro/installer/releases>. Lascia selezionato **Switch Development**.
2. Apri **MSYS2** dal menu Start, nella cartella *devkitPro*. **Non** usare il Prompt dei comandi o PowerShell.
3. Installa le librerie. **Su Windows il comando è `pacman`** (non `dkp-pacman`):

```
pacman -S --needed switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-curl switch-zlib switch-mbedtls
```

4. Entra nella cartella del progetto con `cd`. I percorsi Windows si scrivono con `/` e `/c/` al posto di `C:\`:

```
cd /c/Users/NOME/Downloads/saveman
ls
```

   `ls` deve mostrare `Makefile`, `source`, `romfs`. Se vedi solo un'altra cartella `saveman`, entraci con `cd saveman`.
   Se nel percorso ci sono spazi, mettilo tra virgolette: `cd "/c/Users/NOME/Download miei/saveman"`.

5. Compila:

```
make
```

Esce `saveman.nro` nella stessa cartella.

### Linux / macOS

Installa devkitPro (<https://devkitpro.org/wiki/Getting_Started>), poi:

```
sudo dkp-pacman -S --needed switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-curl switch-zlib switch-mbedtls
make
```

---

## Problemi comuni

| Cosa vedi | Cosa fare |
|---|---|
| `problem with the SSL CA cert (path? access rights?)` | Manca `sd:/switch/saveman/cacert.pem`. Copialo dalla cartella `sd_card`. |
| "Configurazione mancante" / "Prima configurazione" | Compila `client_id` e `client_secret` in `sd:/switch/saveman/config.json`, poi riavvia saveman. |
| Dopo circa 7 giorni chiede di nuovo il login | L'app Google è rimasta in stato *Test*. Pubblicala (punto 6 della sezione 2). |
| "Impossibile aprire il salvataggio (0x...)" | Il gioco è ancora aperto in background. Chiudilo e riprova. |
| Crash o schermo nero all'avvio | Avvia l'Homebrew Menu tenendo premuto **R** mentre lanci un gioco (title takeover): ha più RAM che dall'album. |
| L'elenco è vuoto | Mostra solo i salvataggi utente. Avvia il gioco almeno una volta. |
| "Salvataggio non trovato" nel ripristino | Su questa Switch non esiste ancora il salvataggio di quel gioco per l'utente: avvia il gioco una volta e riprova. |
| "Backup non valido" / "CRC fallito" | Lo zip è corrotto o incompleto (download interrotto?). Il salvataggio non è stato toccato: riscarica il file. |
| La microSD si sta riempiendo | Scheda *Sulla microSD* → **Y** per pulire, oppure abbassa `local_keep_last` dal menu **−**. |
| "Accesso negato" / "Codice scaduto" | Riprova: hai circa 30 minuti per inserire il codice dal telefono. |
| `dkp-pacman: command not found` (Windows) | Su Windows usa `pacman`, nel terminale **MSYS2** di devkitPro. |
| `make: *** No targets specified` o `No rule to make target` | Sei nella cartella sbagliata. Controlla con `ls` che ci sia il `Makefile`. |
| `cannot find -lzstd` | Togli ` -lzstd` dalla riga `LIBS` del `Makefile` (nelle versioni recenti è già tolto). |
| `DEVKITPRO is not set` | Non stai usando il terminale MSYS2 di devkitPro. |

---

## Come funziona (e privacy)

- Per il backup il programma legge i salvataggi (tipo *Account*) in sola lettura, li comprime in uno zip e li carica con le API di Google Drive. Solo il ripristino scrive nel salvataggio.
- Usa il permesso `drive.file`: vede, scarica ed elimina **solo** i file e le cartelle creati da saveman.
- Non esiste nessun server mio o di terzi: la Switch parla direttamente con Google.
- `config.json` contiene il tuo `client_secret` e il `refresh_token`. **Non condividerlo e non caricarlo su GitHub** (il `.gitignore` lo esclude già).
- Per scollegare l'account premi **−** nell'app, oppure revoca l'accesso da <https://myaccount.google.com/permissions>.

## Limiti

- Solo salvataggi utente, non quelli di sistema.
- Il ripristino richiede che il salvataggio di destinazione esista già sulla console.
- Su Drive vengono mostrati gli ultimi 200 backup.
- Nomi di giochi non latini (es. giapponese) potrebbero apparire con caratteri mancanti.

## Credits

- Progetto ideato e testato da **<tuo-nome>**.
- Codice scritto in collaborazione con **Claude** (Anthropic).
- [devkitPro](https://devkitpro.org) e [libnx](https://github.com/switchbrew/libnx)
- [SDL2](https://libsdl.org), SDL2_ttf, SDL2_image
- [libcurl](https://curl.se), [mbed TLS](https://github.com/Mbed-TLS/mbedtls), [zlib](https://zlib.net)
- Elenco certificati: bundle CA di Mozilla (via [curl.se/docs/caextract](https://curl.se/docs/caextract.html))

## Licenza

[MIT](LICENSE)
