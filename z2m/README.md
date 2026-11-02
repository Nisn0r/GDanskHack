# Intégration dans Zigbee2MQTT

Ce dossier contient ce qu'il faut pour intégrer une surface lumineuse
gdansk équipée du firmware **gdansk-light** dans
[Zigbee2MQTT](https://www.zigbee2mqtt.io/), et à travers lui dans Home
Assistant ou toute autre domotique compatible MQTT.

| Fichier | Rôle |
|---|---|
| [`gdansk-light.mjs`](gdansk-light.mjs) | convertisseur externe : déclare la lampe et ses fonctions |
| [`gdansk_ota_index.json`](gdansk_ota_index.json) | exemple d'index OTA local, pour les mises à jour du firmware |
| [`gdansk-light.png`](gdansk-light.png) | icône de l'appareil pour l'interface (512×512, fond transparent) |
| [`make_icon.py`](make_icon.py) | script qui génère cette icône (Python + Pillow) |

> **État** : convertisseur chargé sur une installation Zigbee2MQTT réelle
> (lampe déjà appairée passée en « External » sans ré-appairage) et vérifié
> avec `zigbee-herdsman-converters` 26.125.0. L'OTA via Zigbee2MQTT reste à
> valider.

## Prérequis

- Le luminaire doit déjà contenir le module ZSLC5 reprogrammé avec ce
  firmware : voir le [README principal](../README.md), en particulier
  l'**avertissement de sécurité** (électronique non isolée du secteur).
- Zigbee2MQTT 2.x avec un coordinateur Zigbee 3.0.

## 1. Installer le convertisseur

1. Copier `gdansk-light.mjs` dans le dossier `external_converters/`, situé
   à côté du fichier `configuration.yaml` de Zigbee2MQTT (le créer s'il
   n'existe pas).
2. Autoriser les convertisseurs externes, désactivés par défaut sur les
   installations récentes :

   ```yaml
   advanced:
     enable_external_js: true
   ```

3. Redémarrer Zigbee2MQTT (le dossier n'est lu qu'au démarrage). Le
   journal doit signaler le chargement de `gdansk-light.mjs`.

**Lampe déjà présente dans Zigbee2MQTT** (appairée avant l'installation
du convertisseur, affichée « Auto-generated » ou « NOT supported ») :
**inutile de la ré-appairer**. Au redémarrage, Zigbee2MQTT lui associe le
convertisseur, et son support passe à **« External »** : la lampe apparaît
alors comme **Nisnor GDANSK-CCT**. L'appairage (section 2) est alors sans
objet, hormis l'icône, optionnelle.

## 2. Appairer la lampe

Seulement pour une lampe qui n'est pas encore dans Zigbee2MQTT.

1. Ouvrir l'appairage dans Zigbee2MQTT (« Permit join »).
2. Mettre la lampe en mode appairage :
   - **lampe neuve** (jamais appairée) : il suffit de la mettre sous
     tension, elle cherche un réseau ouvert au démarrage puis toutes les
     60 s ;
   - **lampe déjà appairée** (autre coordinateur, autre réseau) : faire
     une **remise à zéro par 6 coupures** : 6 mises sous tension
     d'affilée, chaque phase allumée durant **moins de 3 s**, la phase
     éteinte assez longue pour que la lampe s'éteigne vraiment. La lampe
     clignote 3 fois, oublie son réseau et redémarre en recherche.
3. Le journal doit afficher « Successfully interviewed », et la lampe
   apparaître comme **Nisnor GDANSK-CCT**, support « External ».

Si la lampe apparaît comme « Auto-generated » ou « NOT supported », le
convertisseur n'est pas chargé (voir Dépannage) : le corriger puis
redémarrer Zigbee2MQTT, sans ré-appairer.

### Icône (optionnel)

Zigbee2MQTT n'a pas d'image pour ce modèle et affiche une icône générique.
Pour utiliser `gdansk-light.png` :

1. la copier dans le dossier `device_icons/` du dossier de données de
   Zigbee2MQTT (à côté de `configuration.yaml`) ;
2. déclarer l'icône, **de préférence depuis l'interface** : page de la
   lampe, onglet **Paramètres** (« Settings » en anglais), saisir
   `device_icons/gdansk-light.png` dans le champ **Icône**, puis
   valider. Le réglage s'applique tout de suite, sans redémarrage, et
   Zigbee2MQTT l'enregistre lui-même dans `configuration.yaml` : pas de
   risque d'erreur d'indentation ou de section en double.

   Équivalent à la main dans `configuration.yaml` (redémarrage
   nécessaire) :

   ```yaml
   devices:
     '0x<adresse IEEE de la lampe>':
       icon: device_icons/gdansk-light.png
   ```

   Le chemin s'écrit tel quel, sans `/` au début ni chemin absolu.

## 3. Fonctions disponibles

| Fonction | Propriété MQTT | Valeurs |
|---|---|---|
| Allumage / extinction | `state` | `ON`, `OFF`, `TOGGLE` |
| Luminosité | `brightness` | 0-254 (transitions avec `transition`, en secondes) |
| Température de couleur | `color_temp` | 153-370 mireds (6500 K froid → 2700 K chaud) |
| Température au démarrage | `color_temp_startup` | 153-370, ou reprise de la valeur précédente |
| Comportement à la mise sous tension | `power_on_behavior` | `off`, `on`, `toggle`, `previous` |
| Identification | `identify` | la lampe clignote |
| Mise à jour du firmware | onglet OTA | voir plus bas |

La lampe envoie d'elle-même ses changements d'état (marche/arrêt,
luminosité, température) : Zigbee2MQTT les configure à l'appairage.

Exemple de commande :

```sh
mosquitto_pub -t 'zigbee2mqtt/<nom de la lampe>/set' \
  -m '{"state": "ON", "brightness": 200, "color_temp": 250, "transition": 2}'
```

Dans **Home Assistant** (découverte MQTT), la lampe apparaît comme une
lumière avec luminosité et température de couleur, plus une entité pour
le comportement à la mise sous tension.

## 4. Mettre à jour le firmware par OTA (optionnel)

Le firmware se met à jour sans ouvrir le luminaire. Zigbee2MQTT sert les
images déclarées dans un **index local**.

1. Produire l'image : `tools/set_version.sh N`, `REGEN=1 ./build.sh app`,
   puis `./make_ota.sh` (voir le [README principal](../README.md)) →
   `out/gdansk_light-0x0000000N.ota`. La version doit être **strictement
   supérieure** à celle installée (v3 actuellement).
2. Copier le `.ota` dans le dossier de données de Zigbee2MQTT, par exemple
   dans `ota/`.
3. Copier `gdansk_ota_index.json` à côté de `configuration.yaml` et y
   mettre le chemin du fichier (relatif au dossier de données) :

   ```json
   [
       {
           "url": "ota/gdansk_light-0x00000004.ota",
           "modelId": "Surface lumineuse CCT",
           "manufacturerName": ["Nisnor"]
       }
   ]
   ```

   Zigbee2MQTT lit lui-même la version, le type d'image et le code
   fabricant dans l'en-tête du fichier. `modelId` et `manufacturerName`
   réservent l'image à cette lampe : le code fabricant (`0x1002`) est
   celui par défaut de Silicon Labs, partagé par d'autres appareils.
   Ne garder dans l'index **que la dernière version**.

4. Déclarer l'index dans `configuration.yaml` :

   ```yaml
   ota:
     zigbee_ota_override_index_location: gdansk_ota_index.json
   ```

5. Redémarrer Zigbee2MQTT, puis dans l'onglet **OTA** de l'interface :
   « Check for updates », puis « Update ».

Durée : environ 15 minutes avec les réglages par défaut (blocs de
50 octets, 250 ms entre deux blocs) ; la lampe reste utilisable pendant
le téléchargement. `ota: image_block_response_delay` (minimum 50 ms)
accélère le transfert.

> ⚠️ Une mise à jour ratée ne se rattrape pas sans ouvrir le luminaire :
> le bootloader ne revient pas à l'ancienne version si la nouvelle ne
> démarre pas. Ne flasher que des images construites depuis ce dépôt et
> validées.

## Dépannage

| Symptôme | Cause probable |
|---|---|
| Support « Auto-generated » ou « NOT supported » au lieu de « External » | convertisseur non chargé : fichier absent de `external_converters/` (à côté de `configuration.yaml`), `advanced: enable_external_js` non activé, ou Zigbee2MQTT pas redémarré depuis. Corriger et redémarrer, pas besoin de ré-appairer |
| Lampe reconnue sous un autre nom | `ModelIdentifier` différent de `Surface lumineuse CCT` (firmware modifié) : adapter `zigbeeModel` dans le convertisseur |
| La lampe ne rejoint pas le réseau | appairage non ouvert, ou lampe encore liée à un ancien réseau : refaire les 6 coupures |
| Les 6 coupures ne font rien | phases allumées trop longues (> 3 s), ou alimentation qui maintient le module trop longtemps après la coupure (voir le condensateur de lissage dans le [README principal](../README.md)) |
| Aucune mise à jour proposée | version du `.ota` pas supérieure à celle installée, chemin `url` incorrect, ou index non déclaré dans `configuration.yaml` |
