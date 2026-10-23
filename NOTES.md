# ProBreeze - notes de protocole

## Bus
- UART 9600 8N1, ESP32-C3 GPIO20=RX (carte TXD), GPIO21=TX (carte RXD, non branché phase 1)
- Protocole **Tuya MCU**, checksum valide sur 100% des trames observees.

## Format de trame
```
55 AA | VER | CMD | LEN_HI LEN_LO | DATA[LEN] | CHK
CHK = somme de tous les octets depuis 0x55 (header inclus) & 0xFF
```
Piege : quand le checksum vaut 0x55/0x57 il peut etre confondu avec un header.

## Tramas observees (phase 1, ESP passif, TX non branche)
Une seule trame, toutes les ~5 s, 15 octets, CMD=0x07 (DATAPOINT_REPORT_ASYNC) :

```
55 AA 03 07 00 08 | 06 02 00 04 00 00 00 38 | 55
                     ^  ^  ^^^^^  ^^^^^^^^^
                     |  |  len=4  valeur=0x38=56
                     |  type=2 (VALUE)
                     dp id = 6
```

Valeurs observees en 2 min : 55, 56, 57, 58 (oscillation ±1)
=> candidat : humidite ambiante %RH (a confirmer) ou temperature sonde.

## Mapping DP (phase 1, passif)

| DP | Type | Valeurs vues | Interpretation | Confiance |
|----|------|--------------|----------------|-----------|
| 1  | bool (t1) | 1 / 0 | Marche / Arret | certain |
| 2  | value 4o (t2) | 187 -> 30 | Humidite cible (min 30) | a confirmer |
| 4  | enum (t4) | 0 -> 1 | Vitesse ventilateur | probable |
| 5  | enum (t4) | 0 -> 2 | Mode | probable |
| 6  | value 4o (t2) | 55..58 | Humidite ambiante %RH | certain |
| 25 | enum (t4) | 1 (constant) | inconnu | ? |
| 101| enum (t4) | 0 -> 1..5 -> 0 | **Timer en heures** (confirme : 2 appuis = 2 h) | certain |

Le timer (dp101) est remis a 0 automatiquement a l'extinction.
dp2 : plage 30..90 par pas de 5, 170 = AU, 187 = CO (valeurs emises par le
selecteur physique, non inscriptibles directement).

### dp5 : mode, RW malgre la spec
**Confirme le 2026-10-23.** dp5 est declare RO cote Tuya mais le MCU accepte
les ecritures. C'est le **seul** moyen de selectionner AU et CO :

| ecriture dp5 | reponse MCU | mode |
|---|---|---|
| 0 | dp2=187, dp5=0 | CO (continu) |
| 1 | dp2=170, dp5=1 | AU (auto) |
| 2 | dp2=30,  dp5=2 | Manuel (consigne remise a 30) |

Ecrire dp2=170 ou 187 est **rejete silencieusement** : le MCU valide 30..90 en
ecriture sur ce DP et ignore le reste (pas d'ecretage a 30, la valeur
precedente est conservee).

**Condition importante : cela ne fonctionne que si l'appareil est en marche
(dp1=1).** En veille, le MCU traite dp5 de facon incoherente : il peut accepter
la trame, rapporter une autre valeur, ou revenir a 30 sans raison. Tous les
essais faits en veille ont produit des resultats contradictoires.

Passer en Manuel remet dp2 a 30 : il faut enchainer avec la consigne voulue.

Le MCU envoie un **dump complet** (dp1,2,6,5,4,101,25) a la mise sous tension
et a chaque changement d'etat, puis des reports unitaires toutes les 5 s.

Anomalie : dp2 = 187 au demarrage puis 30 apres appui. 187 = 0xBB ressemble
a une valeur par defaut non initialisee (le module WiFi absent ne l'a jamais
confirmee).

## Handshake (phase 4) - RESOLU

Le MCU ignore le module tant qu'il n'a pas demarre avec lui. **Un power-cycle
du deshumidificateur est indispensable** apres avoir commence a emettre.

Sequence d'init valide (300 ms entre chaque trame, sinon checksum KO cote MCU) :
```
>>> 55 AA 00 00 00 00 FF            heartbeat module
<<< 55 AA 03 00 00 01 01 04         heartbeat MCU
>>> 55 AA 00 01 00 00 00            product query
<<< 55 AA 03 01 00 2A {...} 6B      identite produit
>>> 55 AA 00 02 00 00 01            conf query
<<< 55 AA 03 02 00 00 04
>>> 55 AA 00 03 00 01 04 07         wifi state = 4 (connecte)
<<< 55 AA 03 03 00 00 05            echo
```

Identite produit renvoyee (ASCII dans le payload du CMD=0x01) :
```
{"p":"bnrwkmfkce6hmxbm","v":"1.0.0","m":0}
```
- product id : `bnrwkmfkce6hmxbm`
- firmware MCU : 1.0.0

## Mapping DP DEFINITIF

| DP | Type | R/W | Valeurs | Role |
|----|------|-----|---------|------|
| 1  | bool  | RW | 0 / 1 | Marche / Arret |
| 2  | int 4o| RW | 30..90 pas de 5, **170=AU**, **187=CO** | Consigne |
| 4  | enum  | RW | 0, 1 (le reste refuse) | Ventilateur 2 vitesses |
| 5  | enum  | **RW** | 0=CO, 1=AU, 2=manuel | Mode : lecture ET pilotage |
| 6  | int 4o| RO | %RH | Humidite ambiante |
| 25 | enum  | RO | = 1 constant | inconnu |
| 101| enum  | RW | 0..24 | Timer heures, remis a 0 a l'extinction |

Sentinelles **0xAA = AU** et **0xBB = CO** : choix delibere du firmware MCU.
Au demarrage sans module, dp2 = 187 (CO) et dp5 = 0.

## Cablage final (valide le 2026-10-23)

| Carte (WIFI CN1) | ESP32-C3 Super Mini |
|---|---|
| TXD | GPIO20 (RX) |
| RXD | **GPIO7** (TX) |
| GND | GND |
| +5V | non connecte (ESP alimente en USB) |

**TX sur GPIO7 et non GPIO21** : a chaque reset, le bootloader ROM du C3
imprime ses logs a 115200 sur GPIO21 (U0TXD), donc directement dans le RX du
MCU => rafale de charabia a un baud different => le MCU verrouille son UART et
affiche `U0` jusqu'au power-cycle. L'UART0 passe par la matrice GPIO, donc
n'importe quelle broche libre convient. Avec TX sur GPIO7, plus aucun reboot
de l'ESP ne peut perturber le MCU.

## Pieges identifies
- Le MCU n'ecoute rien tant qu'il n'a pas boote avec un module present
  => **power-cycle obligatoire** apres avoir commence a emettre.
- Les trames doivent etre espacees (>=300 ms), sinon le MCU les voit comme
  une seule trame et repond par un defaut E1.
- Laisser ~5 s entre deux ecritures de consigne (buffer MCU).
- Une fois qu'on a ecrit des DP, le panneau physique est ignore tant que
  l'appareil n'est pas remis sur secteur.

## Etat
- Aucun heartbeat CMD=0x00, aucune reponse aux queries : normal, la carte
  n'a pas de module WiFi. Le MCU se contente des reports async.
- Prochaine etape : mapping des DP par appui boutons (passif), puis
  emulation du module via composant `tuya` d'ESPHome.
