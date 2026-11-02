# gdansk-light

Firmware Zigbee 3.0 pour rendre pilotable par une domotique la surface
lumineuse **gdansk** de la gamme **INSPIRE** (Leroy Merlin), version
blanche à température variable (**référence 84845491**), à l'aide d'un
module Tuya **ZSLC5** récupéré dans une ampoule connectée Lidl et
reprogrammé.

## ⚠️ Sécurité : électronique non isolée du secteur

Dans le bloc d'alimentation du luminaire, le module sans fil est alimenté
par un abaisseur DC-DC **non isolé du secteur**. Le module, son 3,3 V, sa
masse et ses points de programmation sont donc **au potentiel du secteur**
dès que le luminaire est branché.

- Ne **jamais** connecter une sonde de programmation, un câble USB ou un
  appareil de mesure relié à la terre ou à un PC pendant que le luminaire
  est branché au secteur.
- Programmer le module **hors tension secteur**, alimenté par la sonde, ou
  sorti du luminaire.
- Une fois le firmware en place, les mises à jour se font **par OTA**
  (Zigbee), sans contact physique.

Toute intervention sur ce bloc d'alimentation se fait à vos risques.

## Le problème

Les surfaces lumineuses gdansk proposent plusieurs options, pilotées
essentiellement par une télécommande à piles. Le protocole sans fil est
propriétaire et les références des circuits intégrés sont effacées : il
n'existe aucune solution pour intégrer ce luminaire à une domotique.

En revanche, dans la version blanche à température variable, le bloc
d'alimentation est simple à comprendre :

- une **alimentation à découpage** dont la **puissance** (luminosité) est
  pilotée par le module sans fil ;
- la **température de couleur** est pilotée à travers un **optocoupleur**
  qui agit sur la partie basse tension ;
- les deux commandes sont des signaux **PWM à environ 2 kHz** ;
- le module est alimenté en **3,3 V** par un abaisseur DC-DC (non isolé,
  voir plus haut).

Il suffit donc de remplacer le module d'origine par un module Zigbee qui
produit ces deux PWM.

## Pourquoi reprogrammer un module Tuya

L'idée de départ : réutiliser le module Zigbee d'une ampoule Lidl. Ces
ampoules utilisent des modules Tuya, dont le **ZSLC5** dans la version
blanche à température réglable. Mais elles ne commandent pas la lumière
de la même façon :

| | Ampoule Lidl (firmware Tuya) | Alimentation gdansk |
|---|---|---|
| PWM 1 | quantité de lumière **froide** | **intensité** globale |
| PWM 2 | quantité de lumière **chaude** | **répartition** chaud/froid |

Le firmware d'origine du module ne convient donc pas : il a fallu
reprogrammer le ZSLC5 (un EFR32MG21 de Silicon Labs) pour produire les
signaux attendus par l'électronique gdansk. C'est le but de ce projet.

## Ce que fait le firmware

- Appareil Zigbee 3.0 « Color Temperature Light » (0x010C), **routeur**
  (alimenté sur secteur) : allumage/extinction, luminosité, température de
  couleur 2700-6500 K, comportement à la mise sous tension.
- Compatible avec les coordinateurs standard (testé avec deCONZ ;
  Zigbee2MQTT et ZHA lisent les mêmes attributs). Intégration
  Zigbee2MQTT : voir [z2m/](z2m/README.md).
- **Remise à zéro par 6 coupures de courant** (le module n'a ni bouton ni
  accès filaire une fois monté) : 6 mises sous tension d'affilée, chacune
  de moins de 3 s ; détail dans [app/](app/README.md#remise-à-zéro-sans-bouton-ni-accès-filaire).
- **Mises à jour OTA** via le Gecko Bootloader ([bootloader/](bootloader/README.md)).
- Watchdog, build de production sans console ni debug.

Version actuelle : **v3**, voir [CHANGELOG.md](CHANGELOG.md).

## Matériel

| Broche ZSLC5 | Rôle | Périphérique |
|---|---|---|
| PA00 | PWM puissance (luminosité) | TIMER0 CC0, 2 kHz |
| PA03 | PWM température (vers l'optocoupleur) | TIMER0 CC1, 2 kHz |

Détail des signaux (rapport cyclique, sens de la température, fréquence,
partage de PA03 avec la trace de débogage) : [app/](app/README.md).

Module ZSLC5 ([fiche Tuya](https://developer.tuya.com/en/docs/iot/zslc5?id=K97rhd3028uu5)) :
broches ANT, GND, PA00, PA03, PA04 (3 PWM), 3V3 ; points de test SWDIO
(PA02), SWCLK (PA01), RST, UART0 TX = PA05 / RX = PA06. **Pas d'antenne
intégrée** : antenne monopole externe sur ANT (ici un fil de 35 mm),
chemin RF 1 (`SL_RAIL_ANTENNA_1`). +20 dBm max, **200-210 mA en
émission**. Pas de calibration usine (page USERDATA vierge).

**Modification du bloc d'alimentation** : sur la carte d'alimentation, le
condensateur de lissage d'origine de 470 µF maintient le module en vie
plusieurs secondes après une coupure, ce qui empêche la remise à zéro par
coupures de fonctionner. Il a été remplacé par un **100 µF**.

## Construire

Prérequis : SDK Simplicity 2026.6.2, `slc`, `zap`, toolchain ARM, cmake,
ninja et Simplicity Commander, installés via `slt` (voir
[ZIGBEE.md](ZIGBEE.md)). `slt` et `slc` doivent être dans le `PATH`, ou
désignés par les variables `SLT` / `SLC`.

```sh
./build.sh                    # bootloader + app (génère avec slc si besoin)
REGEN=1 ./build.sh app        # après modification du .slcp ou du .zap
```

Le `.zap` de l'application est produit par `tools/make_zap.py` : voir
[app/](app/README.md#régénérer-le-zap).

## Premier flash du module

Avec une sonde SWD (J-Link) sur les points de test du ZSLC5 (SWDIO,
SWCLK, 3V3, GND), **luminaire débranché du secteur** :

```sh
commander flash bootloader/cmake_gcc/build/base/gdansk_light_bootloader.s37 --device EFR32MG21A020F768IM32
commander flash app/cmake_gcc/build/base/gdansk_light.s37 --device EFR32MG21A020F768IM32
```

Repartir d'un état réseau vierge sans toucher au reste :
`commander device pageerase --range 0xB6000:0xBE000 --device EFR32MG21A020F768IM32`.

## Mises à jour OTA

1. `tools/set_version.sh N` : met la version N partout (version OTA,
   `SL_APPLICATION_VERSION`, Basic `ApplicationVersion` / `SWBuildID`
   « 0.N.0 »). La lampe n'accepte qu'une version **strictement
   supérieure** à la sienne.
2. `REGEN=1 ./build.sh app && ./make_ota.sh` →
   `out/gdansk_light-0x0000000N.ota`. Ne pas flasher cette build au
   débogueur : c'est l'OTA qui doit l'installer.
3. Serveur OTA :
   - **deCONZ** (module « STD OTAU ») : copier le `.ota` dans `~/otau`
     sur la machine deCONZ, puis **redémarrer deCONZ** (le dossier est lu
     au démarrage). Sélectionner le fichier dans le module, puis
     « Update ». Compter environ 7 minutes.
   - **Zigbee2MQTT** : index OTA local, voir [z2m/](z2m/README.md).

   Correspondance obligatoire : fabricant `0x1002`, type d'image `0x0001`,
   version supérieure à la version installée.

- **Une OTA ratée ne se rattrape pas sans ouvrir le luminaire** : le
  bootloader ne revient pas à l'ancienne version si la nouvelle ne démarre
  pas ou ne rejoint pas le réseau. Valider chaque version sur une carte de
  banc avant.
- Après un retour à une version inférieure par flash direct, retirer du
  serveur OTA tout fichier de version supérieure, sinon la lampe se remet
  à jour toute seule.
- Images **non signées, non chiffrées** (réseau domestique) : voir
  [bootloader/](bootloader/README.md) pour les activer.
- Tester une installation sans serveur OTA, au débogueur : voir
  [app/](app/README.md#build-de-banc-et-vérification-sans-console).

## Contenu du dépôt

| Chemin | Contenu |
|---|---|
| [app/](app/README.md) | application Zigbee : sorties PWM, clusters et capacités annoncées, remise à zéro par coupures, watchdog, build de banc et vérification au J-Link |
| [bootloader/](bootloader/README.md) | Gecko Bootloader : carte mémoire flash, slot OTA, options de sécurité (signature, chiffrement) |
| [z2m/](z2m/README.md) | intégration Zigbee2MQTT : convertisseur externe, index OTA, icône |
| `tools/` | `make_zap.py` (.zap), `set_version.sh`, `rtt_poll.py`/`rtt_send.py` (console de banc) |
| `build.sh` | génération slc + compilation cmake/ninja |
| `make_ota.sh` | image GBL + fichier `.ota` |

Autre documentation : [CHANGELOG.md](CHANGELOG.md), [LECONS.md](LECONS.md) (pièges rencontrés),
[ZIGBEE.md](ZIGBEE.md) (outillage SLT/slc, deCONZ),
[COMMANDER.md](COMMANDER.md), [SEGGER.md](SEGGER.md) (J-Link, RTT).

## Licence

MIT, voir [LICENSE](LICENSE). Projet indépendant, sans lien avec Leroy
Merlin, INSPIRE, Lidl ou Tuya. Les fichiers générés et les sources du SDK
Silicon Labs restent sous leurs licences respectives.
