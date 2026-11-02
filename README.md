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
  Zigbee2MQTT et ZHA lisent les mêmes attributs).
- **Remise à zéro par 6 coupures de courant** (le module n'a ni bouton ni
  accès filaire une fois monté).
- **Mises à jour OTA** via le Gecko Bootloader.
- Watchdog, build de production sans console ni debug.

Version actuelle : **v3**, voir [CHANGELOG.md](CHANGELOG.md).

## Matériel

| Broche ZSLC5 | Rôle | Périphérique |
|---|---|---|
| PA00 | PWM puissance (luminosité) | TIMER0 CC0, 2 kHz |
| PA03 | PWM température (vers l'optocoupleur) | TIMER0 CC1, 2 kHz |

- **Luminosité** : rapport cyclique = `OnOff ? CurrentLevel / 254 : 0`
  (linéaire, pas de correction gamma).
- **Température** : 100 % = le plus froid (`ColorTempPhysicalMin` =
  153 mireds, 6500 K), 0 % = le plus chaud (`ColorTempPhysicalMax` =
  370 mireds, 2700 K). Sens réglable par `CCT_PWM_INVERT` dans
  `app/app.c`, plage dans `tools/make_zap.py`.
- **PWM à 2 kHz**, comme le contrôleur d'origine : l'alimentation émet un
  léger sifflement, déjà présent d'origine. `PWM_FREQ_HZ` dans `app/app.c`
  (vérifier la fréquence maximale acceptée par le bloc avant de la
  monter).
- **PA03 est aussi la broche de trace SWV et JTAG TDO** : `pwm_init()`
  libère ces routes, sans quoi la trace prend le pas sur le TIMER (voir
  [CHANGELOG.md](CHANGELOG.md), v2). SWCLK/SWDIO restent actifs.

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

## Zigbee

Endpoint 1, profil HA (0x0104), device type 0x010C :

| Cluster | Côté | Notes |
|---|---|---|
| Basic (0x0000) | serveur | `PowerSource = 0x01` (secteur monophasé), `GenericDeviceClass` = 0x00 (Lighting), `GenericDeviceType` = 0x09 (luminaire intérieur), `ManufacturerName` = `Nisnor`, `ModelIdentifier` = `Surface lumineuse CCT` (réglés dans `tools/make_zap.py`) |
| Identify (0x0003) | serveur | la lampe clignote pendant l'identification |
| Groups (0x0004), Scenes (0x0005) | serveur | obligatoires pour ce type de device |
| On/Off (0x0006) | serveur | `StartUpOnOff` (NVM) |
| Level Control (0x0008) | serveur | bornes 1/254, `StartUpCurrentLevel` (NVM) |
| Color Control (0x0300) | serveur | température seule, `StartUpColorTemperatureMireds` (NVM) |
| OTA Upgrade (0x0019) | client | |

`ManufacturerName`/`ModelIdentifier` servent à Zigbee2MQTT pour
reconnaître le modèle : les garder stables. Un coordinateur les lit à la
jonction ; pour un appareil déjà joint, relire le cluster Basic ou
ré-appairer.

### Capacités annoncées aux coordinateurs

| Information | Où | Valeur |
|---|---|---|
| Type de device | endpoint 1, device ID | 0x010C Color Temperature Light |
| Température seule | `ColorCapabilities` (0x0300/0x400A) | 0x0010 (ni teinte, ni saturation, ni XY, ni boucle) |
| Mode de couleur | `ColorMode` (0x0008) / `EnhancedColorMode` (0x4001) | 2 = température |
| Plage de température | `ColorTempPhysicalMinMireds` (0x400B) / `…MaxMireds` (0x400C) | 153 (6500 K) / 370 (2700 K), valeurs hors plage ramenées aux bornes |
| Plage de luminosité | `MinLevel` (0x0008/0x0002) / `MaxLevel` (0x0003) | 1 / 254 |
| Commandes Color Control | « Discover Commands Received » | 0x0A, 0x47, 0x4B, 0x4C (Move to / Stop / Move / Step Color Temperature) |
| Commande non gérée (ex. Move to Hue) | Default Response | 0x81 UNSUP_COMMAND |

**Power-on behavior** : les trois attributs `StartUp*`, gérés par les
plugins du SDK. Par défaut (`0xFF`/`0xFFFF`), la lampe reprend son état
précédent. Au tout premier démarrage : allumée, 100 %, 4000 K.

**Réseau** : la lampe **rejoint** un réseau, elle n'en crée jamais.
Recherche au démarrage, puis toutes les 60 s tant qu'elle n'est pas sur
un réseau, et 5 s après avoir quitté un réseau. Pas de proxy Green Power
ni de Touchlink.

## Remise à zéro (sans bouton ni accès filaire)

`app/power_cycle_reset.c` : **6 mises sous tension courtes d'affilée**,
chaque phase allumée durant moins de 3 s → la lampe clignote 3 fois,
quitte le réseau si elle y est (le coordinateur est prévenu), efface toute
sa mémoire non volatile (réseau, clés, attributs), puis redémarre en état
usine et cherche un réseau à rejoindre.

- Seules les mises sous tension et les baisses de tension (brown-out)
  comptent ; tout autre redémarrage (logiciel, OTA, watchdog, débogueur)
  remet le compteur à 0, tout comme rester allumé 3 s.
- Si le courant est coupé pendant la séquence, elle reprend à la mise sous
  tension suivante.
- Réglages : `POWER_CYCLE_RESET_COUNT` / `POWER_CYCLE_WINDOW_MS` dans
  `power_cycle_reset.h`. Le temps de maintien de l'alimentation après une
  coupure s'ajoute à la phase allumée : la phase éteinte doit être plus
  longue que ce maintien, et la fenêtre de 3 s doit couvrir les deux (voir
  la modification du condensateur ci-dessus).
- Test sur banc sans coupure : définir
  `POWER_CYCLE_RESET_TEST_COUNT_ANY_RESET` en tête de
  `power_cycle_reset.c` (les resets débogueur comptent alors). **Ne pas
  livrer ce mode.**

## Watchdog

WDOG0, délai 2 s (`SL_LEGACY_HAL_DISABLE_WATCHDOG 0` dans le `.slcp` et
`config/sl_legacy_hal_wdog_config.h` ; le SDK le laisse désactivé par
défaut).

- Rafraîchi par les ticks de la boucle principale.
- Le framework dort en EM1 jusqu'au prochain événement, sans tenir compte
  du watchdog : `watchdog_event` (`app.c`) réveille la boucle chaque
  seconde. Le rafraîchissement reste dans la boucle principale, donc un
  vrai blocage provoque bien le redémarrage.
- L'installation d'une image par le bootloader n'est pas interrompue.

## Carte mémoire flash

```
0x00000  bootloader      16 Ko   (bootloader/)
0x04000  application    ~190 Ko  (app/, max 352 Ko)
0x5C000  slot OTA 0      352 Ko  (images téléchargées)
0xB6000  NVM3             32 Ko  (réseau, clés, attributs NVM)
0xBE000  dernière page            (tokens fabrication — ne pas effacer)
```

L'application trouve le slot via `slot_manager` (`USE_FIRST_SLOT`).

## Construire

Prérequis : SDK Simplicity 2026.6.2, `slc`, `zap`, toolchain ARM, cmake,
ninja et Simplicity Commander, installés via `slt` (voir
[ZIGBEE.md](ZIGBEE.md)). `slt` et `slc` doivent être dans le `PATH`, ou
désignés par les variables `SLT` / `SLC`.

```sh
./build.sh                    # bootloader + app (génère avec slc si besoin)
REGEN=1 ./build.sh app        # après modification du .slcp ou du .zap
```

Le `.zap` est produit par `tools/make_zap.py` à partir de celui de
l'exemple `zigbee_z3_light` du SDK (endpoint réduit à la température de
couleur) :

```sh
python3 tools/make_zap.py "$(slt where simplicity-sdk)/zigbee_app/z3/zigbee_z3_light/config/zcl/zcl_config.zap" app/config/zcl/zcl_config.zap
```

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
   - Zigbee2MQTT / ZHA : index OTA local (à documenter).

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
  [BOOTLOADER.md](BOOTLOADER.md) pour les activer.
- Tester une installation sans serveur OTA, au débogueur : écrire la GBL
  dans le slot (`commander flash out/x.gbl --binary --address 0x5C000 …`)
  et lancer `plugin slot-manager boot-slot 0` depuis la console d'un build
  de banc.

## Build de banc et vérification sans console

Le build de production n'a ni console, ni CLI, ni sortie de debug. Pour un
build de banc, remettre les composants listés en commentaire dans
`app/gdansk_light.slcp`, puis `REGEN=1 ./build.sh app`. La console passe
par RTT, lue sans arrêter le CPU :

```sh
CB=$(arm-none-eabi-nm app/cmake_gcc/build/base/gdansk_light.out | awk '/ _SEGGER_RTT$/{print "0x"$1}')
python3 tools/rtt_poll.py $CB session.txt 1              # lecture continue
python3 tools/rtt_send.py $CB "plugin counters print"    # envoi d'une commande
```

`keys print` affiche les clés réseau en clair : ne pas conserver ces
journaux tels quels.

Vérifier un build de production au J-Link :
- broches : forcer CC0/CC1 à 50 % (`w4 0x50048068 0x4E20`,
  `w4 0x50048088 0x4E20`), puis lire plusieurs fois `GPIO_PA_DIN`
  (`0x5003C014`, bit 0 = PA00, bit 3 = PA03) ;
- registres : `WDOG0 EN` (`0x5A018004`) = 1, `TRACEROUTEPEN`
  (`0x5003C444`) = 0, `DBGROUTEPEN` (`0x5003C440`) = 0xB ;
- redémarrage : écrire un marqueur en bas de pile
  (`w4 0x20000010 0xCAFEF00D`) ; `halInit()` remplit la pile de
  `0xCDCDCDCD` au démarrage, donc si le marqueur survit, il n'y a pas eu
  de reset.

## Contenu du dépôt

```
app/            application Zigbee (.slcp, app.c, config/, autogen/ générés par slc)
bootloader/     Gecko Bootloader (slot OTA interne)
tools/          make_zap.py (.zap), set_version.sh, rtt_poll/rtt_send (console de banc)
build.sh        génération slc + compilation cmake/ninja
make_ota.sh     image GBL + fichier .ota
```

Documentation : [CHANGELOG.md](CHANGELOG.md), [LECONS.md](LECONS.md)
(pièges rencontrés), [ZIGBEE.md](ZIGBEE.md) (outillage SLT/slc, deCONZ),
[BOOTLOADER.md](BOOTLOADER.md), [COMMANDER.md](COMMANDER.md),
[SEGGER.md](SEGGER.md) (J-Link, RTT).

## Licence

MIT, voir [LICENSE](LICENSE). Projet indépendant, sans lien avec Leroy
Merlin, INSPIRE, Lidl ou Tuya. Les fichiers générés et les sources du SDK
Silicon Labs restent sous leurs licences respectives.
