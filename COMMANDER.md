# Simplicity Commander

Notes cumulées sur `commander` (Silicon Labs Simplicity Commander — CLI et
GUI), tels qu'utilisés sur ce projet.

## Documentation officielle

<https://docs.silabs.com/simplicity-commander/latest/simplicity-commander-start/>

## Installation

Installé via `slt` (Commander GUI + CLI, le binaire `commander` fait les
deux). `make_ota.sh`
l'utilise pour créer les images GBL/OTA.

```sh
commander --version
# Simplicity Commander 1v24p3b1989
# JLink DLL version: 8.94
```

`commander` embarque sa propre DLL JLink — pas besoin d'installer JLink
séparément pour que `commander` fonctionne. Les outils SEGGER (JLinkExe)
restent utiles pour le débogage interactif.

## Ce pour quoi `commander` est le bon outil

- **Identifier une puce sans connaître sa référence exacte** — lit la page
  DEVINFO gravée en usine (part number, die revision, taille flash/RAM,
  unique ID) :
  ```sh
  commander device info --device EFR32MG21   # famille générique suffit
  ```
  A permis de corriger l'hypothèse erronée F1024 → F768 sur ce projet en une
  commande.
- **Flasher / vérifier** :
  ```sh
  commander flash app/cmake_gcc/build/base/gdansk_light.s37 --device EFR32MG21A020F768IM32
  commander verify app/cmake_gcc/build/base/gdansk_light.s37 --device EFR32MG21A020F768IM32
  ```
- Domaines couverts (`commander --help`) au-delà du flash simple : NVM3,
  tokens de sécurité (Secure Engine), OTA/GBL, LittleFS, RTT, SWO, AEM
  (Advanced Energy Monitor), génération RPS, provisioning manufacturing
  (mfg917). Pas encore explorés en détail sur ce projet — à documenter ici
  au fur et à mesure d'un usage réel.

## Ce pour quoi `commander` n'est PAS le bon outil

Pas de débogage interactif : pas de breakpoints, pas de lecture de
registres CPU/PC/SP, pas de dump des registres de fault (CFSR/BFAR/SFAR).
Pour ça, voir [SEGGER.md](SEGGER.md) (JLinkExe/JLinkGDBServer).

## Commandes utiles rencontrées

- `commander adapter list` — liste les sondes J-Link connectées (numéro de
  série, firmware).
- `commander device info --device <famille|part exact>` — identité puce.
- `commander flash <fichier> --device <part>` — écrit en flash (hex/bin/s37
  supportés).
- `commander verify <fichier> --device <part>` — compare le contenu flash
  au fichier sans le réécrire.

## État de sécurité du Secure Element (lecture seule)

`commander security status --device EFR32MG21A020F768IM32` — ne modifie
rien. Résultat sur la carte définitive (puce d'un module Tuya, 2026-10-20) :
SE firmware 1.2.6, Debug lock **Disabled**, Device erase Enabled, Secure
boot **Disabled**, Command key / Sign key **non installées** → OTP vierge,
puce entièrement ouverte. Attention : installer une clé (`security
writekey`) ou activer le secure boot / debug lock est **irréversible** (OTP).
