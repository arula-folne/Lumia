# Lumia

**Live × Music × View**

OBS Studio source plugin **LumiaMusicView** for Windows (x64).  
Local music folders become an on-stream overlay — jacket, album, title, and artist — without a separate Node server.

## Download

See [Releases](https://github.com/arula-folne/Lumia/releases) for Windows packages (`LumiaMusicView-*-windows-x64.zip`).

Design / Custom CSS editor: [folne.net/apps/lumia](https://folne.net/apps/lumia)

## Install (end users)

1. Quit OBS Studio.
2. Extract the release zip into your OBS folder (e.g. `C:\Program Files\obs-studio\`).
3. Start OBS → **Add Source → LumiaMusicView**.
4. Add music folders to the playlist and optionally paste Custom CSS from the web editor.

Details: `INSTALL.txt` in the zip.

## Build from source (Windows)

Requirements:

- Visual Studio 2022 Build Tools (MSVC + CMake)
- OBS Studio installed at `C:\Program Files\obs-studio` (used to generate import libs)

```bat
cd obs-plugin
build.bat
```

Optional local install (Administrator):

```bat
install-to-obs.bat
```

## License

[GNU General Public License v3.0](LICENSE)

## Notes

- Shuffle mixes tracks across albums (not album-by-album).
- Loop restarts the full playlist when finished.
- Artist metadata uses Windows file properties when available; album name follows the folder name; title is the file stem.
