# dotfiles – gofile-downloader Setup

## Dateien

| Datei im Repo | Ziel auf dem System |
|---|---|
| `gofile-downloader.env` | `~/gofile-downloader/.env` |
| `gofile.bash` | `~/dotfiles/gofile.bash` (dann in `.bashrc` sourcen) |

## Einrichtung

```bash
# Repo klonen
git clone git@github.com:necrevistonnezr/dotfiles.git ~/dotfiles

# .env an den richtigen Ort kopieren
cp ~/dotfiles/gofile-downloader.env ~/gofile-downloader/.env

# In ~/.bashrc eintragen:
echo 'source ~/dotfiles/gofile.bash' >> ~/.bashrc
source ~/.bashrc
```

## Hinweis zu uv + sudo

`uv` wird typischerweise per User installiert (`~/.local/bin/uv`).  
Die Funktion ermittelt den Pfad vor dem `sudo`-Aufruf via `command -v uv`,  
damit vopono den Binary auch als root findet.

Falls `uv` noch nicht installiert ist:
```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
```

## Benutzung

```bash
gofile "https://gofile.io/d/xxxxxxxx"
```
