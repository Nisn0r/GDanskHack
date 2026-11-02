# Zigbee : outillage et protocole

Notes sur la chaîne Silicon Labs sans IDE et sur le comportement Zigbee
observé. Le projet lui-même est décrit dans [README.md](README.md), les
pièges résumés dans [LECONS.md](LECONS.md).

## Documentation officielle

<https://docs.silabs.com/zigbee/9.1.1/zigbee-start/>

Alliance Zigbee (spécifications, indépendant Silicon Labs) :
<https://csa-iot.org/> · <https://csa-iot.org/resources/developer-resources/>

## Comment le SDK fournit la pile Zigbee

- La pile elle-même est livrée en **bibliothèques précompilées**
  (`zigbee-pro-stack`, etc., sous `zigbee/build/gcc/cortex-m33/…`) : on lie
  contre, on ne recompile pas.
- La couche ZCL (`zigbee/app/framework/`) est en source, mais attend du
  code généré par **ZAP** à partir d'un fichier `.zap` (endpoints,
  clusters, attributs).
- `main()` délègue l'init et le tick de chaque composant à
  `autogen/sl_event_handler.c`, généré par **`slc`** à partir du `.slcp`.
  ZAP seul ne suffit donc pas : c'est `slc` qui génère le projet complet
  (et appelle ZAP s'il est installé via SLT).
- Exemple de référence : `zigbee_app/z3/zigbee_z3_light/` (son `.zap` sert
  de base à `tools/make_zap.py`).
- Aucun composant des exemples ne génère de vrai PWM : la sortie est écrite
  à la main (TIMER0, `app/app.c`). Sur Series 2, n'importe quelle broche
  peut être routée vers un canal TIMER (`GPIO->TIMERROUTE[n]`).
- Layout du SDK 2026.6.x : sous-paquets à la racine (`platform_core/`,
  `zigbee/`, `zigbee_app/`, `bootloader/`…).

## Installer l'outillage via SLT (sans IDE)

**SLT** (gestionnaire de paquets headless de Silicon Labs) :

```sh
mkdir -p <dossier d'outils>/slt-cli && cd <dossier d'outils>/slt-cli
curl -sL -o slt-cli.zip \
  "https://www.silabs.com/documents/public/software/slt-cli-1.1.0-linux-x64.zip"
unzip -q slt-cli.zip && rm slt-cli.zip && chmod +x slt
./slt update --self --non-interactive
./slt install simplicity-sdk slc-cli zap commander gcc-arm-none-eabi cmake ninja --non-interactive
```

- **Installer `zap` via SLT** : c'est ce qui permet à `slc` de le trouver.
  Sans lui, `slc generate` produit `autogen/` mais **pas** les fichiers
  `zap-*` (sans erreur).
- Le script `slc` résout son Java via `$(dirname "$0")` : **ne pas le
  symlinker**, mettre dans le `PATH` un wrapper `slc` qui résout la
  version courante :

  ```sh
  #!/usr/bin/env bash
  exec "$(slt where slc-cli)/slc" "$@"
  ```

- Faire confiance au SDK une fois :
  `slc signature trust --sdk "$(slt where simplicity-sdk)"`.
- `slt update …` désenregistre l'ancienne version (seule la nouvelle est
  vue par `slt where`) ; rollback : `slt install "simplicity-sdk/<version>"`.

## Générer et compiler (ce que fait `build.sh`)

```sh
slc generate -p app/gdansk_light.slcp -d app -s "$SDK" \
  --with EFR32MG21A020F768IM32 -o cmake -cpproj
cmake --workflow --preset project      # dans app/cmake_gcc
```

Pièges rencontrés :
- **Cibler la puce par son part number exact** (`--with
  EFR32MG21A020F768IM32`), **jamais un composant de carte** (`brd4181a`) :
  la carte impose son propre device et crée un conflit d'exclusivité.
- `slc` **n'écrase pas** les `config/*.h` existants à la régénération :
  modifier le `.slcp` **et** l'en-tête. Quand un composant est retiré, son
  en-tête modifié est déplacé dans `trashed_modified_files/` (à supprimer).
- Les dossiers `cmake_gcc/` générés contiennent des chemins absolus : les
  supprimer après un déplacement du projet (`build.sh` les régénère).
- Avec l'ancien SDK et la sortie Makefile, LTO cassait au link (bytecode
  LTO 12 vs 14) et il fallait `-nocp` ; avec le SDK 2026.6 et la sortie
  cmake, `toolchain_gcc_lto` fonctionne.
- `slc example create --name <nom court>` est plus fiable que `--id`.

## Network steering : « Beacons heard: 0 »

Avec `SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_OPTIMIZE_SCANS = 1` (défaut),
c'est `network-steering-v2.c` qui est compilé. Un routeur y lance un
`SL_ZIGBEE_ACTIVE_SCAN_ROUTER`, et la pile ne garde que les beacons de
réseaux **ouverts à l'appairage et acceptant un routeur**. « Beacons
heard » = `sl_zigbee_get_num_stored_beacons()`. **0 = aucun réseau
joignable**, pas « radio muette ». (Lire `network-steering.c` à la place a
coûté deux sessions de faux débogage radio.)

## deCONZ

- **Cause réelle des échecs** (2026-10-22) : le coordinateur deCONZ ne
  répondait à **aucune** requête de beacon, sur aucun canal. Après
  reformation du réseau dans deCONZ (plusieurs canaux activés, leave/join),
  il est réapparu, et la lampe l'a rejoint avec la
  **sécurité Zigbee 3.0 active** (clé de lien Trust Center unique
  authentifiée). L'ancienne impression « deCONZ ne marche que sans
  sécurité » venait très probablement de ce réseau incohérent.
- Méthode de diagnostic : un scan actif simple (`SL_ZIGBEE_ACTIVE_SCAN`)
  liste tous les réseaux, ouverts ou non ; un réseau fermé répond quand
  même aux requêtes de beacon. Si le coordinateur n'apparaît pas, le
  problème est de son côté, avant toute question de clés.
- Pistes de sécurité notées à l'époque, non nécessaires au final : install
  code (token de fabrication, à déclarer aussi côté coordinateur),
  `SL_ZIGBEE_AF_PLUGIN_NETWORK_STEERING_TRY_ALL_KEYS`, et le départ du
  réseau si la mise à jour de la clé TC échoue
  (`sl_zigbee_af_update_tc_link_key_status_cb`).
- deCONZ envoie parfois un niveau **0xFF** (réservé) : bornes du plugin
  Level Control à 1/254.
- OTA : voir [README.md](README.md), section OTA.

## Autres coordinateurs

- Autre coordinateur testé : jonction OK. Éviter `zigbee_trust_center_keepalive` : ce coordinateur ne gère pas
  le cluster Keep-Alive (0x0025), d'où des erreurs puis une recherche de
  nouveau réseau.
- Zigbee2MQTT : convertisseur externe et index OTA dans [z2m/](z2m/README.md).
