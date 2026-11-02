# Outils SEGGER (JLinkExe / JLinkGDBServer)

Notes cumulées sur les outils SEGGER utilisés pour le débogage bas niveau sur
ce projet. Voir [COMMANDER.md](COMMANDER.md) pour le flash/l'identification
de puce (`commander` est préférable pour ça) ; ce fichier concerne le
débogage interactif, pour lequel les outils SEGGER restent nécessaires.

## Outils utilisés

`JLinkExe` (commander-line J-Link), `JLinkGDBServer` /
`JLinkGDBServerCLExe`. Pas de `arm-none-eabi-gdb` installé, mais
`gdb-multiarch` (GDB générique Debian) fonctionne très bien avec
`JLinkGDBServer`.

## JLinkExe en scripté

Fonctionne bien via un fichier de commandes passé à `-CommandFile`. Piège
important : **`g` (go) ne bloque pas** — il faut un `Sleep <ms>` avant tout
`h`/`regs`/`mem32` qui doit refléter un état après exécution, sinon on lit
l'état d'avant que le programme ait eu le temps de tourner (ou de planter).

Commandes utiles rencontrées :
- `r` — reset (halt-after-reset par défaut avec ce device).
- `h` — halt.
- `g` — go (non bloquant, voir ci-dessus).
- `regs` — dump complet des registres (dont les registres Secure/Non-secure
  séparés sur ce Cortex-M33 TrustZone : `MSP_S`/`MSP_NS`, etc.).
- `SetBP <addr>` / `ClrBP <handle>` — breakpoints matériels (FPB — seulement
  8 emplacements sur ce cœur, `WARNING: No more breakpoint resources left`
  si on en pose trop sans nettoyer).
- `step` — un seul pas d'instruction (très utile pour bisector un crash très
  précoce, ex. le tout premier `push` de `Reset_Handler`).
- `mem32 <addr>, <count>` / `w4 <addr>, <valeur>` — lecture/écriture mémoire
  directe **sans exécuter aucune instruction** (accès via l'AHB-AP du
  débogueur). Très utile pour sonder la RAM/les registres périphériques
  indépendamment de tout code — c'est ce qui a permis de mesurer la RAM
  physique réelle (64 Ko, alors que la référence supposée en annonçait 96).
- `loadfile <fichier.hex>` — flash + reset implicite.

Exemple de session (bisection d'un crash) :
```
r
SetBP 0x2e0
g
Sleep 200
h
regs
```

## Registres de fault utiles (Cortex-M33 / ARMv8-M)

Adresses PPB à lire en direct via `mem32` pour diagnostiquer un HardFault :

| Registre | Adresse      | Usage |
|----------|--------------|-------|
| CFSR     | `0xE000ED28` | Combine MMFSR (octet 0) / BFSR (octet 1) / UFSR (octets 2-3) |
| HFSR     | `0xE000ED2C` | bit30 `FORCED` = fault escaladé en HardFault (sous-handler désactivé) |
| BFAR     | `0xE000ED38` | Adresse ayant causé un BusFault (valide si BFSR bit7=`BFARVALID`) |
| SFSR     | `0xE000EDE4` | Statut SecureFault (spécifique ARMv8-M) |
| SFAR     | `0xE000EDE8` | Adresse ayant causé un SecureFault |
| SHCSR    | `0xE000ED24` | Etat d'activation des handlers système |

Lecture de BFSR=`0x92` (bit1 PRECISERR + bit4 STKERR + bit7 BFARVALID) avec
BFAR pointant exactement sur l'adresse de pile visée = signature d'un vrai
bus fault matériel (adresse non mappée), pas d'un souci de sécurité
TrustZone/SAU — ça a permis de trancher rapidement entre les deux
hypothèses lors du debug d'un bug de pile au début du projet.

## JLinkGDBServer + gdb-multiarch

Fonctionne (`JLinkGDBServer -device <part> -if SWD -speed 4000 -port 2331`
puis `gdb-multiarch -x <script> app/cmake_gcc/build/base/gdansk_light.out`), mais **piège rencontré** :
poser des breakpoints logiciels dans la flash pendant une session où le FPB
est déjà à court d'emplacements produit des erreurs trompeuses ("CPU is not
halted", "Cannot access memory") qui n'ont rien à voir avec un vrai bug —
préférer JLinkExe en scripté avec des breakpoints matériels (`SetBP`) pour
ce genre de bisection fine, c'est plus prévisible.

## Lire les périphériques : alias non sécurisé (0x5xxx_xxxx) sur xG21

Le firmware Simplicity SDK tourne en non sécurisé et accède aux périphériques
via l'alias `*_NS_BASE` (ex. TIMER0 = `0x50048000`, GPIO = `0x5003C000`).
Lu via l'alias sécurisé (`0x40048000`), TIMER0 renvoie **tout à zéro** même
quand il tourne — ne pas en conclure que le périphérique est éteint.

## Console RTT sans arrêter le CPU (2026-10-20)

- `JLinkRTTLogger -RTTAddress <adr>` a fonctionné une fois, puis a
  systématiquement répondu « RTT Control Block not found » alors que le
  bloc était valide en RAM (signature `SEGGER RTT` lue avec `mem32`).
  Cause non identifiée.
- Remplacé par `tools/rtt_poll.py` (lecture du tampon montant 0
  via `mem32`/`savebin`, puis mise à jour de `RdOff` avec `w4`, le cœur
  continue de tourner) et `tools/rtt_send.py` (écriture dans le tampon
  descendant 0, donc envoi de commandes CLI). Deux sessions JLinkExe
  simultanées se gênent : `rtt_send.py` réessaie jusqu'à ce que l'écriture
  soit confirmée.
- `sleep 100000` dans un script JLinkExe ne revient pas : enchaîner des
  `sleep 10000`.
- Piège shell : `pkill -f "motif"` tue aussi le shell qui l'exécute si le
  motif apparaît ailleurs dans la même ligne de commande (code de sortie
  144). Tuer par PID après avoir filtré `/proc/<pid>/cmdline`.

## Simuler un blocage (test du watchdog, 2026-10-22)

```
halt
setPC 0x4280      # adresse d'une instruction « b . » (e7fe), trouvée avec
go                # objdump -d … | grep -P "^\s+([0-9a-f]+):\s+e7fe\s+b\.n\s+\1"
exit
```

`exit` laisse la cible tourner. Le watchdog applicatif (2 s) doit
redémarrer la puce : `Reset info: 0x05 (WDG)` dans la console. Pendant un
`halt`, le watchdog est gelé (`DEBUGRUN = 0`).

## Alimentation

`power off` dans JLinkExe ne coupe que la sortie 5 V de la sonde
(broche 19). La carte prend son 3,3 V autrement : impossible de
simuler une coupure secteur à la sonde.

Adresse du bloc RTT : elle **change à chaque build**, la relire avec
`arm-none-eabi-nm <app>.out | grep _SEGGER_RTT`.
