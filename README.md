# Noita Quick Save

A native DLL hijack for Noita that adds simple quicksave and quickload hotkeys.

This project builds a replacement `lua51.dll`. When Noita loads it, the DLL starts a
small background key poller and forwards Noita's normal Lua 5.1 exports to the
original Lua DLL renamed as `lua51_orig.dll`.

## Features

- `F5` creates a quicksave by copying Noita's `save00` directory.
- `F9` restores the quicksave, restarts Noita, and terminates the old process so
  the restored save is not overwritten during normal shutdown.
- Backups are stored in a visible `NoitaQuicksave` directory beside `noita.exe`.
- Logs are written beside the game as `noita_quicksave.log`.
- Built as a 32-bit native C++ DLL to match Noita's 32-bit process.

## Warning

This directly copies and replaces Noita save data. Back up your real save before
testing it.

Quickload deletes the current `save00` directory and replaces it with the last
quicksave. If Steam Cloud is enabled, test carefully and keep an external backup.

## Requirements

- Windows Noita installation.
- Visual Studio or Build Tools with MSVC C++ tools installed.
- MSBuild from the Visual Studio installation.

## Build

From this repository:

```bat
"C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" NoitaQuicksaveDll\NoitaQuicksaveDll.vcxproj /p:Configuration=Release /p:Platform=Win32
```

The built DLL is written to:

```text
NoitaQuicksaveDll\bin\Win32\Release\lua51.dll
```

## Install

1. Open the folder that contains `noita.exe`.
2. Rename Noita's original `lua51.dll` to `lua51_orig.dll`.
3. Copy the published `lua51.dll` from this project into the same folder.
4. Start Noita normally.

The folder should contain at least:

```text
noita.exe
lua51.dll       # this project's DLL
lua51_orig.dll  # Noita's original Lua DLL
```

## Usage

| Key | Action |
| --- | --- |
| `F5` | Create or overwrite the quicksave backup. |
| `F9` | Restore the quicksave and restart Noita. |

Backups are stored at:

```text
<Noita game folder>\NoitaQuicksave\
```

Logs are stored at:

```text
<Noita game folder>\noita_quicksave.log
```

## Save Locations

The mod reads Noita's active save from:

```text
%APPDATA%\..\LocalLow\Nolla_Games_Noita\save00
```

For Proton/Linux development paths, the code also supports:

```text
~/.steam/steam/steamapps/compatdata/881100/pfx/drive_c/users/steamuser/AppData/LocalLow/Nolla_Games_Noita/save00
```

## Troubleshooting

### Noita fails to start

Make sure the original Noita DLL was renamed to `lua51_orig.dll` and is in the
same directory as the replacement `lua51.dll`.

### F9 says no quicksave exists

Press `F5` first and confirm that `<Noita game folder>\NoitaQuicksave\` was
created.

### Build fails with Lua unresolved externals

The project creates a linker `.exp` file from `lua51_exports.def` before linking.
Build with MSBuild and the MSVC C++ toolchain so `lib.exe`, `cl.exe`, and
`link.exe` are available.

### Noita shows 0xc000007b on launch

That usually means a 64-bit DLL was copied into Noita's 32-bit process. Build the
`Release|Win32` target and install `NoitaQuicksaveDll\bin\Win32\Release\lua51.dll`.

## Development Notes

- `lua51_exports.def` lists the Lua 5.1 exports forwarded to `lua51_orig.dll`.
- `NoitaQuicksave.cpp` handles the DLL entry point, `F5` and `F9`, save copying,
  logging, and Noita restart.
