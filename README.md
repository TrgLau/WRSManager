# WRSManager

Interface wxWidgets pour installer et piloter un serveur Windrose Dedicated, éditer les configurations JSON, et gérer des sauvegardes automatiques.

## Fonctionnalités

- Installation de SteamCMD (`1. Install SteamCMD`)
- Installation du serveur (`2. Install Server`) avec bootstrap automatique
- Démarrage serveur (`3. StartServer`) et arrêt (`4. StopServer`)
- Édition des réglages `WorldDescription` (JSON monde)
- Édition des réglages `ServerDescription_Persistent` (ancien JSON serveur)
- Validation/sauvegarde via bouton `Valider`
- Auto-backup périodique du dossier `R5\Saved` en `.zip`
- Rétention des backups avec suppression des plus anciens (`MaxBackups`)

## Prérequis

- Windows recommandé (les actions serveur utilisent des API/commandes Windows)
- `wxWidgets` installé
- `wx-config` disponible dans le PATH (MSYS2 MinGW64 conseillé)
- Compilateur C++17

## Build

Depuis la racine du projet :

```bash
make
```

Pour lancer :

```bash
make run
```

Nettoyage :

```bash
make clean
```

## Configuration locale (optionnelle)

Tu peux créer `makefile.config` pour surcharger des variables, par exemple :

- `WX_CONFIG`
- `CXX`
- `APP`
- `BINDIR`
- `BUILDDIR`

## Workflow recommandé

1. Créer un onglet serveur (`+`)
2. Cliquer `1. Install SteamCMD`
3. Cliquer `2. Install Server`
4. Vérifier/éditer les réglages JSON
5. Cliquer `Valider`
6. Lancer avec `3. StartServer`
7. Arrêter avec `4. StopServer`

Quand le serveur est lancé, l’UI est verrouillée : seul `4. StopServer` reste actif.

## Réglages JSON

La zone réglages est en 2 colonnes :

- **Colonne gauche** : paramètres `WorldDescription` (monde)
- **Colonne droite** : paramètres `ServerDescription_Persistent` (serveur)

`CreationTime` est conservé automatiquement à la sauvegarde (non exposé dans l’UI).

`islandId` n’est pas exposé dans l’UI (non modifiable par l’utilisateur).

### Fichiers ciblés

- Monde :  
  `steamcmd\steamapps\common\Windrose Dedicated Server\R5\Saved\SaveProfiles\Default\RocksDB\0.10.0\Worlds\<dossier_id>\*.json`
- Serveur :  
  `steamcmd\steamapps\common\Windrose Dedicated Server\R5\ServerDescription.json`

## AutoBackup

AutoBackup crée des archives `.zip` horodatées du dossier :

`steamcmd\steamapps\common\Windrose Dedicated Server\R5\Saved`

Destination :

`<dossier_serveur>\BackupServer`

Nom de fichier :

`Saved_YYYYMMDD_HHMMSS.zip`

Rétention :

- `MaxBackups` limite le nombre d’archives conservées
- les plus anciennes sont supprimées en premier

## Notes

- Le parsing JSON est volontairement simple (recherche texte/regex) pour rester léger.
- En cas d’évolution forte de structure JSON, ajuster `LoadServerDescriptionToControls()` et `SaveServerDescriptionFromControls()`.
