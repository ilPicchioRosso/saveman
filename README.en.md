# saveman

**Back up your Nintendo Switch save data to Google Drive, straight from the console.**

saveman is a homebrew app (`.nro`): pick a game, press **A**, and the save is zipped and uploaded to a folder
in your own Google Drive. You can then grab it from the Drive app on your phone.

**What it can do**
- **Back up** one or more games, with upload to Google Drive
- **Download** backups from Drive, right on the Switch
- **Restore** a backup into the game, with an automatic safety copy
- **Space management**: automatic cleanup of old backups on the microSD

🇮🇹 [Versione italiana](README.md)

---

## Requirements

- A Switch running **custom firmware (Atmosphere)** with the Homebrew Menu
- A **Google account**
- **Wi-Fi** on the Switch
- A **PC** to prepare the microSD card (and to build, unless you use the prebuilt file)

> ⚠️ For backing up **your own** saves, personal use. Use at your own risk. Not affiliated with Nintendo.

---

## 1. Install (no compiling)

1. Open the **Releases** section of this repository and download `saveman.nro`.
2. Also download the `sd_card` folder from this repository (it contains `cacert.pem`).
3. Copy the files to your microSD like this:

```
sd:/
└── switch/
    ├── saveman.nro
    └── saveman/
        └── cacert.pem
```

`cacert.pem` is the list of certificates the Switch needs to talk to Google securely.
**Without it you will get** `problem with the SSL CA cert (path? access rights?)`.

To build it yourself, see [Building](#building-from-source).

---

## 2. Connect Google Drive (one time, about 10 minutes)

saveman uses **your own Google project**, so there is no server in between and only you can access your files.
Menu names on Google's site may change slightly over time.

1. Open <https://console.cloud.google.com> and sign in. Use the project picker at the top → **New project** → name it `saveman` → **Create**, and make sure it is selected.
2. Menu ☰ → **APIs & Services** → **Library** → search **Google Drive API** → **Enable**.
3. Menu ☰ → **Google Auth Platform** (or *APIs & Services → OAuth consent screen*) → **Get started**.
   - App name: `saveman`
   - Support email: yours
   - Audience: **External**
   - Contact email: yours
4. **Branding** page. Google asks for these fields before you can publish the app:
   - **App name**: `saveman`
   - **Support email**: yours
   - **Home page URL** and **Privacy policy URL**: any two public web addresses; nobody will review them. You can use the two ready-made pages in [`docs/`](docs/) by enabling GitHub Pages on your repository (Settings → Pages → branch `main`, folder `/docs`).
   - **Developer contact email**: yours
5. **Data Access** page → **Add or remove scopes** → select `.../auth/drive.file` → **Save**.
   This scope lets saveman see **only the files it creates itself**, never the rest of your Drive.
6. **Audience** page → **Publish app** → confirm. The status must become **In production**.
   No Google verification is needed.
   > If you leave it in *Testing*, everything works but **after 7 days** saveman will ask you to log in again.
7. **Clients** page → **Create client** → application type: **TVs and Limited Input devices** → name `saveman` → **Create**.
   Copy the **Client ID** and **Client secret**.

---

## 3. First run

1. Put the microSD in the Switch and launch **saveman** from the Homebrew Menu.
   It will say the setup is missing and create `sd:/switch/saveman/config.json`. Quit with **+**.
2. Put the microSD back in your PC and open `config.json` with Notepad. Paste the two values between the quotes and leave the rest alone:

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

3. Start saveman again, pick a game and press **A**.
4. An **address and a code** appear. On your phone open the address, enter the code, sign in with your Google account and allow access.
   If you see an "unverified app" warning: **Advanced → Continue** (it is your own app, created in step 2).
5. The Switch uploads the zip. Check Google Drive: you will find a **Saveman** folder.

You only log in once; after that just press **A**.

---

## How to use it

The app has three tabs, switched with **L** and **R**:

| Tab | What it shows | Actions (**A** button) |
|---|---|---|
| **Salvataggi** (Saves) | the saves on your Switch | back up + upload to Drive, or back up to the microSD only |
| **Su Drive** (On Drive) | backups uploaded to your Drive | download and restore, download, delete from Drive |
| **Sulla microSD** (On microSD) | the zips stored on the microSD | restore, upload to Drive, delete |

| Button | Action |
|---|---|
| ▲ ▼ | scroll the list (◀ ▶ for whole pages) |
| **L** / **R** | switch tab |
| **A** | open the actions menu (for the highlighted item, or the selected ones) |
| **X** | select / deselect |
| **Y** | Saves: select all · On Drive: refresh the list · On microSD: clean up old backups |
| **−** | settings (disconnect Google, space limits, about) |
| **+** | quit |
| **B** (hold) | cancel during backup, download or restore |

Use **X** to select several items and run the action on all of them at once (e.g. upload 10 backups, or delete them).

---

## Restoring a backup

1. Go to the **Sulla microSD** tab (or **Su Drive**, which downloads the file first), highlight the backup and press **A** → **Ripristina nel gioco** (restore into the game).
2. saveman works out which game and user it belongs to and asks you to confirm.
3. Before writing anything it runs three safety steps:
   - it **verifies the zip's integrity** (if it is corrupt it stops and the save is left untouched);
   - it **creates a safety copy** of the current save on the microSD (recognisable by "(pre-restore)" in the name);
   - it writes the files and only **commits** at the very end: if anything fails before that, the changes are not saved.

Good to know:
- The destination save **must already exist** on this Switch: on a new console, launch the game once with that user, then restore.
- Backups made by saveman include the game ID in the file name, so the destination is picked automatically. Older zips or zips made by other tools (e.g. JKSV) work too, but you are asked to choose the destination yourself: make sure you pick the right game.
- Close the game before restoring.

---

## Keeping an eye on space

Every backup also stays on the microSD. To avoid filling it up:

- **Automatic cleanup**: after each backup saveman keeps only the **last N backups per game** (`local_keep_last`, default 5). Change it from the **−** menu or in `config.json`; `0` = no limit.
- **Delete the zip after upload**: in the **−** menu (or `"keep_local": false`) the zip is removed from the microSD as soon as it is on Drive. "microSD only" backups are never deleted by this option.
- **Manual cleanup**: on the *Sulla microSD* tab press **Y**: it shows how many files it will delete and how much space it frees, and asks for confirmation.
- The *Sulla microSD* tab always shows how much space the backups use and how much is free.

---

## Building from source

### Windows

1. Install **devkitPro** with the installer: <https://github.com/devkitPro/installer/releases>. Keep **Switch Development** selected.
2. Open **MSYS2** from the Start menu, in the *devkitPro* folder. Do **not** use Command Prompt or PowerShell.
3. Install the libraries. **On Windows the command is `pacman`** (not `dkp-pacman`):

```
pacman -S --needed switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-curl switch-zlib switch-mbedtls
```

4. `cd` into the project folder. Windows paths use `/` and `/c/` instead of `C:\`:

```
cd /c/Users/NAME/Downloads/saveman
ls
```

   `ls` must show `Makefile`, `source`, `romfs`. If you only see another `saveman` folder, run `cd saveman`.
   If the path has spaces, use quotes: `cd "/c/Users/NAME/My Downloads/saveman"`.

5. Build:

```
make
```

You get `saveman.nro` in the same folder.

### Linux / macOS

Install devkitPro (<https://devkitpro.org/wiki/Getting_Started>), then:

```
sudo dkp-pacman -S --needed switch-dev switch-sdl2 switch-sdl2_ttf switch-sdl2_image switch-curl switch-zlib switch-mbedtls
make
```

---

## Troubleshooting

| What you see | What to do |
|---|---|
| `problem with the SSL CA cert (path? access rights?)` | `sd:/switch/saveman/cacert.pem` is missing. Copy it from the `sd_card` folder. |
| "Configurazione mancante" / "Prima configurazione" (setup missing) | Fill in `client_id` and `client_secret` in `sd:/switch/saveman/config.json`, then restart saveman. |
| Asks you to log in again after about 7 days | The Google app is still in *Testing*. Publish it (step 6 of section 2). |
| "Impossibile aprire il salvataggio (0x...)" (cannot open save) | The game is still running in the background. Close it and retry. |
| Crash or black screen on launch | Launch the Homebrew Menu by holding **R** while starting a game (title takeover): it has more RAM than the album. |
| The list is empty | Only user saves are listed. Launch the game at least once. |
| "Salvataggio non trovato" (save not found) when restoring | This Switch has no save for that game and user yet: launch the game once and retry. |
| "Backup non valido" / "CRC fallito" (invalid backup / CRC failed) | The zip is corrupt or incomplete (interrupted download?). The save was not touched: download it again. |
| The microSD is filling up | *Sulla microSD* tab → **Y** to clean up, or lower `local_keep_last` from the **−** menu. |
| "Accesso negato" / "Codice scaduto" (denied / code expired) | Try again: you have about 30 minutes to enter the code on your phone. |
| `dkp-pacman: command not found` (Windows) | Use `pacman` in the devkitPro **MSYS2** terminal. |
| `make: *** No targets specified` or `No rule to make target` | Wrong folder. Check with `ls` that the `Makefile` is there. |
| `cannot find -lzstd` | Remove ` -lzstd` from the `LIBS` line in the `Makefile` (already removed in recent versions). |
| `DEVKITPRO is not set` | You are not in the devkitPro MSYS2 terminal. |

---

## How it works (and privacy)

- For backups the app reads user saves (*Account* type) read-only, zips them and uploads them via the Google Drive API. Only restoring writes to a save.
- It uses the `drive.file` scope: it can see, download and delete **only** files and folders created by saveman.
- There is no server of mine or anyone else's: the Switch talks directly to Google.
- `config.json` holds your `client_secret` and `refresh_token`. **Never share it or commit it to GitHub** (`.gitignore` already excludes it).
- To disconnect, press **−** in the app, or revoke access at <https://myaccount.google.com/permissions>.

## Limitations

- User saves only, not system saves.
- Restoring needs the destination save to already exist on the console.
- Drive shows the latest 200 backups.
- Non-Latin game titles (e.g. Japanese) may show missing characters.

## Credits

- Project idea and testing by **<your-name>**.
- Code written in collaboration with **Claude** (Anthropic).
- [devkitPro](https://devkitpro.org) and [libnx](https://github.com/switchbrew/libnx)
- [SDL2](https://libsdl.org), SDL2_ttf, SDL2_image
- [libcurl](https://curl.se), [mbed TLS](https://github.com/Mbed-TLS/mbedtls), [zlib](https://zlib.net)
- Certificate list: Mozilla CA bundle (via [curl.se/docs/caextract](https://curl.se/docs/caextract.html))

## License

[MIT](LICENSE)
