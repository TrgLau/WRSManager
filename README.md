# WRSManager

wxWidgets desktop app to install and manage a Windrose Dedicated Server, edit JSON settings, and run automatic backups.

## Features

- Install SteamCMD (`1. Install SteamCMD`)
- Install server (`2. Install Server`) with automatic bootstrap
- Start (`3. StartServer`) and stop (`4. StopServer`) server
- Edit `WorldDescription` settings (world JSON)
- Edit `ServerDescription_Persistent` settings (legacy server JSON)
- Save/validate settings with `Valider`
- Periodic auto-backup of `R5\Saved` as `.zip`
- Backup retention with automatic oldest-first cleanup (`MaxBackups`)

## Requirements

- Windows recommended (server actions use Windows APIs/commands)
- `wxWidgets` installed
- `wx-config` available in PATH (MSYS2 MinGW64 recommended)
- C++17 compiler

## Build

From project root:

```bash
make
```

Run:

```bash
make run
```

Clean:

```bash
make clean
```

## Optional local config

Create `makefile.config` to override variables, for example:

- `WX_CONFIG`
- `CXX`
- `APP`
- `BINDIR`
- `BUILDDIR`

## Recommended workflow

1. Create a server tab (`+`)
2. Click `1. Install SteamCMD`
3. Click `2. Install Server`
4. Review/edit JSON settings
5. Click `Valider`
6. Start with `3. StartServer`
7. Stop with `4. StopServer`

When the server is running, UI is locked and only `4. StopServer` stays enabled.

## JSON settings

The settings area is split in 2 columns:

- **Left column**: `WorldDescription` parameters (world)
- **Right column**: `ServerDescription_Persistent` parameters (server)

`CreationTime` is preserved automatically on save (not exposed in UI).

`islandId` is not exposed in UI (not user-editable).

### Target files

- World:  
  `steamcmd\steamapps\common\Windrose Dedicated Server\R5\Saved\SaveProfiles\Default\RocksDB\0.10.0\Worlds\<world_id_folder>\*.json`
- Server:  
  `steamcmd\steamapps\common\Windrose Dedicated Server\R5\ServerDescription.json`

## AutoBackup

AutoBackup creates timestamped `.zip` archives of:

`steamcmd\steamapps\common\Windrose Dedicated Server\R5\Saved`

Destination:

`<server_folder>\BackupServer`

Filename format:

`Saved_YYYYMMDD_HHMMSS.zip`

Retention:

- `MaxBackups` limits how many archives are kept
- oldest backups are deleted first

## Notes

- JSON parsing is intentionally lightweight (text search/regex).
- If JSON structure changes significantly, adjust `LoadServerDescriptionToControls()` and `SaveServerDescriptionFromControls()`.
