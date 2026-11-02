# Application

Application Zigbee de gdansk-light (EFR32MG21A020F768IM32, module Tuya
ZSLC5). Le projet général (but, sécurité, construction, OTA) est décrit
dans le [README principal](../README.md), le bootloader et la carte
mémoire dans [bootloader/](../bootloader/README.md).

| Fichier | Rôle |
|---|---|
| `gdansk_light.slcp` | projet slc : composants, configuration, identité OTA |
| `main.c` | `main()` standard de l'exemple du SDK |
| `app.c` | PWM, mise à jour de la lumière, identification, watchdog, recherche de réseau |
| `power_cycle_reset.c/.h` | remise à zéro par 6 coupures |
| `printf.h` | stub vide, requis par `slot_manager` sans composant printf |
| `config/` | configuration des composants, `zcl/zcl_config.zap` |
| `autogen/`, `cmake_gcc/` | générés par slc (non versionnés) |

## Sorties PWM

| Broche ZSLC5 | Rôle | Périphérique |
|---|---|---|
| PA00 | PWM puissance (luminosité) | TIMER0 CC0, 2 kHz |
| PA03 | PWM température (vers l'optocoupleur) | TIMER0 CC1, 2 kHz |

- **Luminosité** : rapport cyclique = `OnOff ? CurrentLevel / 254 : 0`
  (linéaire, pas de correction gamma).
- **Température** : 100 % = le plus froid (`ColorTempPhysicalMin` =
  153 mireds, 6500 K), 0 % = le plus chaud (`ColorTempPhysicalMax` =
  370 mireds, 2700 K). Sens réglable par `CCT_PWM_INVERT` dans `app.c`,
  plage dans `tools/make_zap.py`.
- **PWM à 2 kHz**, comme le contrôleur d'origine : l'alimentation émet un
  léger sifflement, déjà présent d'origine. `PWM_FREQ_HZ` dans `app.c`
  (vérifier la fréquence maximale acceptée par le bloc avant de la
  monter).
- **PA03 est aussi la broche de trace SWV et JTAG TDO** : `pwm_init()`
  libère ces routes, sans quoi la trace prend le pas sur le TIMER (voir
  [CHANGELOG.md](../CHANGELOG.md), v2). SWCLK/SWDIO restent actifs.

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

### Régénérer le `.zap`

`config/zcl/zcl_config.zap` est produit par `tools/make_zap.py` à partir
de celui de l'exemple `zigbee_z3_light` du SDK (endpoint réduit à la
température de couleur). Depuis la racine du dépôt :

```sh
python3 tools/make_zap.py "$(slt where simplicity-sdk)/zigbee_app/z3/zigbee_z3_light/config/zcl/zcl_config.zap" app/config/zcl/zcl_config.zap
REGEN=1 ./build.sh app
```

## Remise à zéro (sans bouton ni accès filaire)

`power_cycle_reset.c` : **6 mises sous tension courtes d'affilée**,
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
  la modification du condensateur dans le [README principal](../README.md)).
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

## Build de banc et vérification sans console

Le build de production n'a ni console, ni CLI, ni sortie de debug. Pour un
build de banc, remettre les composants listés en commentaire dans
`gdansk_light.slcp`, puis `REGEN=1 ./build.sh app`. La console passe
par RTT, lue sans arrêter le CPU :

```sh
CB=$(arm-none-eabi-nm app/cmake_gcc/build/base/gdansk_light.out | awk '/ _SEGGER_RTT$/{print "0x"$1}')
python3 tools/rtt_poll.py $CB session.txt 1              # lecture continue
python3 tools/rtt_send.py $CB "plugin counters print"    # envoi d'une commande
```

`keys print` affiche les clés réseau en clair : ne pas conserver ces
journaux tels quels.

Tester une installation OTA sans serveur, au débogueur : écrire la GBL
dans le slot (`commander flash out/x.gbl --binary --address 0x5C000 …`)
et lancer `plugin slot-manager boot-slot 0` depuis la console d'un build
de banc.

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
