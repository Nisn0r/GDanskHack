# Leçons apprises (EFR32MG21 / Zigbee, 2026-08 → 2026-10)

Résumé transverse des pièges rencontrés, du plus coûteux au plus anecdotique.
Le détail est dans les fichiers cités.

## Méthode

- **Vérifier quel fichier du SDK est réellement compilé** avant d'en tirer
  une conclusion (`#if`, liste des sources générée). Deux sessions perdues à
  lire `network-steering.c` alors que la build compile
  `network-steering-v2.c` (→ fausse piste « problème radio »). Voir
  [CHANGELOG.md](CHANGELOG.md).
- **Tester l'intuition de l'utilisateur avant de la contredire** : il
  soupçonnait l'appairage/la sécurité, c'était bien côté réseau.
- **Identifier la puce par DEVINFO** (`commander device info`), pas par la
  référence annoncée : la puce était une F768/64 Ko, pas une F1024.
- Un compteur n'est pas forcément ce que son nom dit : « Beacons heard » =
  beacons *stockés* (réseaux joignables pour un routeur), pas entendus.

## Zigbee

- **« Beacons heard: 0 » ≠ radio muette** : aucun réseau ouvert acceptant
  un routeur. Pour voir ce que la radio entend vraiment : un scan actif
  simple (`SL_ZIGBEE_ACTIVE_SCAN`), qui liste tous les réseaux, ouverts ou
  non.
- **deCONZ peut avoir un réseau cassé** qui ne répond à aucune requête de
  beacon, sur aucun canal. Remède : reformer le réseau (leave/join dans
  deCONZ). Ce n'était ni la sécurité Zigbee 3.0 ni la lampe
  ([ZIGBEE.md](ZIGBEE.md)).
- deCONZ envoie parfois un niveau **0xFF** (réservé) : borner le plugin
  Level Control à 1/254.
- `zigbee_trust_center_keepalive` : à éviter avec un coordinateur qui ne
  gère pas le cluster 0x0025 (erreurs puis recherche d'un nouveau réseau).
- OTA via deCONZ : `.ota` dans `~/otau` **puis redémarrage de deCONZ** ;
  ~11 min pour 284 Ko. Après un retour à une version inférieure, retirer
  le `.ota` supérieur du serveur, sinon la lampe se remet à jour.
- `keys print` affiche les clés réseau en clair : masquer dans les journaux.

## SDK / génération

- `slc` **n'écrase pas** les `config/*.h` existants à la régénération :
  modifier le `.slcp` **et** l'en-tête.
- **Watchdog désactivé par défaut** dans les projets Zigbee
  (`SL_LEGACY_HAL_DISABLE_WATCHDOG 1`, contrairement à ce que dit le
  commentaire). Une fois activé, le framework dort (EM1) jusqu'au prochain
  événement **sans limite liée au watchdog** → reset toutes les ~8 s si la
  lampe est inactive. Remède : un événement périodique (1 s) qui réveille
  la boucle principale ([app/README.md](app/README.md#watchdog)).
- Le watchdog Series 2 est déjà gelé en EM2/EM3 (`CFG.EM2RUN/EM3RUN = 0`) ;
  pas d'option en EM1.
- `slc` cassé après mise à jour : résoudre le binaire via `slt where
  slc-cli` (wrapper `slc` dans le `PATH`) ; SDK non approuvé :
  `slc signature trust --sdk <sdk>`.
- Horloge LF sur LFXO qui ne démarre pas → blocage dans
  `sl_sleeptimer_init()` (`RTCC_SyncWait`). Utiliser LFRCO, comme la
  config de référence (août, premier essai Zigbee abandonné). Le blocage
  HFXO suivant était logiciel (disparu avec le SDK 2026.6 + cmake/ninja),
  pas un défaut de la carte.

## Débogage

- **Vérifier la broche, pas seulement le registre du périphérique** : un
  PWM correct dans le TIMER peut ne jamais sortir. Lire `GPIO_Px_DIN`
  plusieurs fois au J-Link.
- Le composant `debug_swo` (amené par le debug Zigbee) **s'approprie PA03**
  (sortie à 1 + trace SWV) sur xG21 : `SL_DEBUG_SWO_ENABLE 0` si PA03 sert
  à autre chose. PA03/PA04 sont aussi TDO/TDI (routes de débogage
  actives au reset).

- Périphériques à lire via l'**alias non sécurisé** (`0x5xxx_xxxx`) :
  l'alias sécurisé renvoie 0 ([SEGGER.md](SEGGER.md)).
- `JLinkRTTLogger` peu fiable ici : `tools/rtt_poll.py` /
  `rtt_send.py`. L'adresse du bloc RTT **change à chaque build**
  (`arm-none-eabi-nm … | grep _SEGGER_RTT`).
- Un seul accès J-Link à la fois ; `pkill -f motif` peut tuer le shell
  appelant.
- Simuler un blocage pour tester le watchdog : `halt`, `setPC <adresse
  d'un « b . »>`, `go` dans JLinkExe.
- La J-Link ne peut pas couper l'alimentation de la carte (`power off`
  ne coupe que sa sortie 5 V) : tester la remise à zéro par de vraies
  coupures.
- `| grep -m1` sur une build peut la tuer (SIGPIPE) : rediriger le
  journal dans un fichier.

## Production / montage

- **« Prêt pour la production » = retirer tout le debug** (CLI, console,
  debug print, SWO) quand le produit n'a plus d'accès console : c'est
  inutile et ça peut casser du matériel (PA03 pris par `debug_swo`).
  `slot_manager` inclut alors `printf.h` sans dépendance déclarée : stub
  local.
- Sans console, valider au J-Link : état des broches (`GPIO_Px_DIN`),
  registres, et marqueur en bas de pile pour détecter un reset.

- Remise à zéro par coupures : le temps de maintien de l'alimentation
  après coupure s'ajoute à la phase allumée. 470 µF de lissage → reset
  impossible ; 100 µF → OK. À mesurer sur chaque nouveau driver.

- Ce qui demande le débogueur (bootloader, clés de signature, install
  code, verrouillage) se décide **avant** de fermer le boîtier.
- Le bootloader ne revient pas en arrière après une OTA ratée : valider
  chaque version sur une carte de banc.
- Archiver les binaires livrés : les sorties de
  build sont écrasées.
