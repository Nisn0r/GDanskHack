# BOOTLOADER.md

Notes sur le Gecko Bootloader pour l'EFR32MG21A020F768IM32 (carte définitive,
module Tuya). Sources : code du SDK 2026.6.2 (`bootloader/platform/bootloader/`,
`bootloader_sample_app/`) et doc officielle
<https://docs.silabs.com/mcu-bootloader/latest/> (v3.3.2, PDF complet :
`https://docs.silabs.com/mcu-bootloader/3.3.2/mcu-bootloader.pdf`).

Note : le SDK 2026.6.2 embarque le bootloader **3.2.x** (`btl_config.h`), la
doc en ligne décrit la 3.3.2 (et cite des essais en 3.4.0) — une option
documentée peut ne pas encore exister dans notre SDK, vérifier dans les
`.slcc` avant de compter dessus.

## État sécurité de la puce (2026-10-20)

`commander security status` : debug lock off, secure boot off, aucune clé en
OTP, SE firmware 1.2.6 — puce vierge, cf. COMMANDER.md.

## Architecture sur xG21

- Pas de « first stage » logiciel sur Series 2 : c'est le **Secure Element**
  qui en tient le rôle. Le bootloader principal est à `0x0`, **16 Ko**
  (`bootloader_main_size = 0x4000`), application à `0x4000`.
- Flash xG21 : pages de **8 Ko** (`0x2000`) — toute adresse de slot/région
  doit y être alignée.
- Mise à jour du bootloader lui-même : le GBL contenant le nouveau
  bootloader est d'abord décompressé à `BTL_UPGRADE_LOCATION`
  (défaut **`0x8000` sur xG21 → dans la zone applicative**), puis le SE le
  recopie en `0x0`. **L'application est écrasée** : un GBL de mise à jour
  bootloader doit embarquer aussi l'application (sinon régler
  `BTL_APP_SPACE_SIZE` / déplacer `BTL_UPGRADE_LOCATION_BASE`).
- Mise à jour du firmware SE via GBL : prévoir **48 Ko** dans le slot sur xG21.
- Bootloader TrustZone-aware : passe de 16 à **24 Ko** sur xG21 (casse la
  carte mémoire par défaut avec app à `0x4000`).

## Valeurs par défaut réelles (vérifié 2026-10-20)

`btl_config.h` définit `BOOTLOADER_ENFORCE_SECURE_BOOT` à `true` **seulement
en repli** : le `btl_core_cfg.h` généré pour un projet le met à **0**, et
idem `BOOTLOADER_ENFORCE_SIGNED_UPGRADE`/`_ENCRYPTED_UPGRADE`. Un bootloader
généré depuis l'exemple n'exige donc ni signature ni secure boot — à
activer explicitement dans le `.slcp`.

Exemple concret : `bootloader/` (10,8 Ko sur 16).

## Choix pour le routeur Zigbee OTA

Exemple de base : `bootloader-storage-internal-single-768k` — slot unique de
352 Ko à `0x5C000` sur xG21 (sdid 200).

- EmberZNet (`zigbee_ota_storage_simple*`) ne gère **qu'un seul slot**.
- **L'offset de début du slot côté bootloader doit être égal à l'« OTA
  Storage Start Offset » du composant `zigbee_ota_storage_simple_eeprom`
  côté application** — rien ne vérifie la cohérence, c'est au développeur.
  Sauf avec `GECKO_BOOTLOADER_STORAGE_SUPPORT = USE_FIRST_SLOT` et le
  composant `slot_manager` : l'appli lit alors la table des slots du
  bootloader (choix retenu ici).
- Le composant EEPROM doit être configuré selon le support read-modify-write
  du stockage (flash interne : non) — sinon assert au démarrage.

## Sécurité : clés et options

- Signature ECDSA-P256-SHA256 (images GBL et secure boot de l'app), chiffrement
  AES-CTR-128.
- **Spécifique xG21** : la clé publique de signature et la clé AES de
  déchiffrement OTA peuvent être soit en **token de fabrication** (dernière
  page de flash, « Lock Bits page », effaçable au débogueur → réversible),
  soit dans l'**OTP du Secure Element** (définitif). Pour du secure boot
  sérieux, la doc demande de désactiver « Allow use of public key from
  manufacturing token storage » sur xG21.
- Anti-rollback applicatif : la version d'app vue est stockée à la fin de la
  zone bootloader ; **seulement 6 incréments par défaut**, puis il faut
  mettre à jour le bootloader (ou agrandir
  `SL_GBL_APPLICATION_VERSION_STORAGE_CAPACITY` + linker). Inopérant si le
  SE verrouille les pages du bootloader.
- Secure boot par certificat : une version de certificat GBL ≥ celle du
  bootloader est exigée ; sur xG21, le certificat peut être signé par la clé
  en OTP du SE.

## Décision pour la lampe déployée (2026-10-22)

Pas de signature, pas de chiffrement, pas de secure boot, pas de verrouillage
du débogage : réseau domestique, choix de l'utilisateur. Le module est monté
dans le luminaire, **l'activer plus tard demande de le ressortir** (nouveau
bootloader + clé en token, au débogueur).

- Installer une GBL depuis le slot sans serveur OTA :
  `commander flash x.gbl --binary --address 0x5C000 …`, puis dans la
  console de l'appli `plugin slot-manager boot-slot 0`. Validé avec le
  watchdog applicatif actif : la copie par le bootloader n'est pas
  interrompue.
- Option `btl_core_cfg.h` : le bootloader peut lui-même armer le WDOG avant
  d'appliquer une mise à jour (désactivée ici).
- Pas de retour arrière automatique : une application qui ne démarre pas
  ou ne rejoint pas le réseau reste installée.

## Compression / Delta DFU sur xG21 (limite de taille)

Le bootloader de 16 Ko est serré : doc Silicon Labs (BRD4181B) —
**LZMA + Delta DFU dépasse de 2 792 octets**, **LZ4 + Delta DFU passe** ; en
3.4.0, même LZMA seul ne passait plus. → Viser LZ4 si on veut compresser.

## Outils

- `commander util genkey` / `commander gbl create` (signature, chiffrement,
  compression), `commander ota create --type zigbee` (fichier `.ota`).
- `zigbee_app/tool/image-builder/image-builder-linux` dans le SDK (outil
  historique pour les `.ota`).
