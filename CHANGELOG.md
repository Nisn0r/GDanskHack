# Changelog

Versions du firmware applicatif (`ApplicationVersion` du cluster Basic,
version OTA). Le bootloader n'a pas changé depuis la v1.

## v4 — 2026-11-02 (version déployée)

- **Corrigé** : après extinction puis rallumage (commande « On »), la
  lampe repartait à 100 % au lieu de reprendre sa luminosité précédente.
  L'attribut `OnLevel` (Level Control 0x0011) valait 0xFE, repris de
  l'exemple `zigbee_z3_light` : le plugin Level Control remonte alors
  toujours à ce niveau. Il vaut maintenant 0xFF (« non défini » =
  reprendre le niveau précédent), réglé dans `tools/make_zap.py`.
  Diagnostic confirmé sur la lampe en v3 : écrire `onLevel` = 255 depuis
  Zigbee2MQTT suffit à corriger le rallumage (jusqu'à la coupure
  suivante, l'attribut étant en RAM).
- Installée par OTA dans le luminaire et validée.
- Binaires archivés localement dans `release/v4/`.

## v3 — 2026-10-23

- **Corrigé** : sens de la température de couleur. L'alimentation gdansk
  attend 100 % de rapport cyclique = le plus froid ; la v2 était inversée
  (« coolest » donnait le plus chaud). `CCT_PWM_INVERT` passé à 1.
- Installée par OTA dans le luminaire : On/Off, luminosité et température
  fonctionnent.

## v2 — 2026-10-23

- **Corrigé** : la sortie température (PA03) restait collée à 3,3 V. Le
  composant `debug_swo`, amené par les composants de debug Zigbee, met
  PA03 en sortie à 1 et y route la trace SWV (`GPIO->TRACEROUTEPEN`),
  prioritaire sur le TIMER. `SL_DEBUG_SWO_ENABLE 0`, et `pwm_init()`
  libère les routes SWV et TDO (libérer TDO seul ne suffisait pas). Le
  défaut était passé inaperçu en v1 car seul le registre CC1 avait été
  vérifié, pas l'état de la broche.
- **Build de production** : plus de console, de CLI ni de sortie de debug
  (le module est enfermé dans le luminaire, sans accès débogueur).
  Composants `cli`, `iostream_rtt`, `zigbee_debug_basic`,
  `zigbee_debug_print`, `zigbee_zcl_cli`, `zigbee_core_cli`,
  `zigbee_counters` et la commande `beacon-scan` retirés. Code : 280 →
  186 Ko, OTA ~7 min au lieu de ~11. `app/printf.h` neutralise le
  `printf.h` que `slot_manager` inclut sans déclarer la dépendance.
- Validée sur carte de banc, uniquement au J-Link : mise à jour v1 → v2
  par le bootloader depuis le slot, PA03 suit le PWM, watchdog actif, pas
  de redémarrage intempestif, blocage forcé → redémarrage.

## v1 — 2026-10-22

Première version installée dans le luminaire.

- Lampe Zigbee 3.0 « Color Temperature Light » (routeur) : On/Off, Level
  Control, Color Control (température seule), power-on behavior, client
  OTA, Gecko Bootloader avec slot interne.
- Remise à zéro par 6 coupures de courant courtes.
- Bornes de niveau 1/254 : un coordinateur deCONZ envoyait parfois le
  niveau réservé 0xFF, que la pile acceptait (PWM à 100,4 %).
- Capacités exposées aux coordinateurs (`ColorCapabilities`, plage en
  mireds, `MinLevel`/`MaxLevel`, Discover Commands), fabricant « Nisnor »,
  modèle « Surface lumineuse CCT », classe Lighting / type luminaire
  intérieur.
- Watchdog (WDOG0, 2 s), désactivé par défaut dans le SDK. Un événement
  d'une seconde réveille la boucle principale : sans lui, le framework dort
  plus de 2 s et le watchdog redémarre la lampe toutes les ~8 s.
- Images OTA non signées, par choix (réseau domestique).

## Mise au point (2026-10-20 → 2026-10-22)

- **Fausse piste radio** : « Beacons heard: 0 » au network steering a été
  pris pour une radio muette, en lisant `network-steering.c` alors que la
  build compile `network-steering-v2.c`. En v2, un routeur ne compte que
  les réseaux ouverts acceptant un routeur : 0 signifie « aucun réseau
  joignable ». Vérifié au passage : antenne ZSLC5 = chemin RF 1, HFXO à
  +10 ppm, CTUNE correct.
- **deCONZ** : le coordinateur ne répondait à aucune requête de beacon,
  sur aucun canal. Une fois son réseau reformé, la lampe l'a rejoint avec
  la sécurité Zigbee 3.0 (clé de lien Trust Center unique authentifiée).
  Le problème venait du réseau, pas des clés.
- **Composant à éviter** : `zigbee_trust_center_keepalive`, avec un
  coordinateur qui ne gère pas le cluster Keep-Alive (0x0025) : erreurs
  toutes les ~60 s, puis recherche d'un nouveau réseau.
- **OTA de bout en bout** via le module STD OTAU de deCONZ : blocs de
  50 octets, ~420 o/s, ~11 min pour 284 Ko, lampe utilisable pendant le
  téléchargement ; réglages et réseau conservés après installation.
- **Remise à zéro dans le luminaire** : avec le condensateur de lissage
  d'origine (470 µF) de la carte d'alimentation, la puce restait
  alimentée plus de 3 s après la coupure et les cycles n'étaient jamais
  comptés. Remplacé par 100 µF : remise à zéro fonctionnelle, sans
  changement de firmware.
- **Watchdog** : couper le watchdog avant chaque mise en veille a été
  envisagé puis écarté. Le matériel le gèle déjà en EM2/EM3, la lampe
  reste en EM1, le gain d'énergie est négligeable sur un routeur dont la
  radio reste en réception, et on perdrait la protection contre un système
  qui s'endort sans jamais se réveiller.
