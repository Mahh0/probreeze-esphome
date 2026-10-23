# ProBreeze × ESPHome — déshumidificateur connecté, sans soudure
Ajout d'un contrôle wifi Tuya sur le déshumidificateur Probreeze 12L avec compresseur, avec un **ESP32-C3 Super Mini**. La carte mère dispose d'un connecteur wifi `WIFI CN1` sur lequel l'ESP est branché. Aucune soudure nécessaire (voir matériel).

Le protocole série **Tuya MCU** a été entièrement décodé à l'écoute.

> Pour piloter l'appareil depuis Home Assistant (ou depuis un agent qui s'en
> occupe), voir **[`HOME_ASSISTANT.md`](HOME_ASSISTANT.md)** : entités exposées,
> interactions entre `Mode` et `Consigne`, commandes à ne pas envoyer.

---

## Tableau 
| Entité | Type | Source |
|---|---|---|
| `Marche` | switch | `dp1` |
| `Consigne` | select | `dp2` (30→90 % par pas de 5) |
| `Ventilateur` | select | `dp4` (2 vitesses) |
| `Timer` | select | `dp101` (0→24 h) |
| `Humidité` | sensor | `dp6` |
| `Mode` | select | `dp5` (CO / AU / Manuel) — **pilotable** |
| `Signal WiFi`, `Uptime`, `Cause du dernier reset` | sensors | diagnostic |

---

## Matériel
- Déshumidificateur ProBreeze dont la carte expose un connecteur `WIFI CN1` (testé sur le modèle 12l, mais doit sûrement fonctionner avec d'autres)
- ESP32-C3 Super Mini (module type `HW466-AB`), 4 Mo de flash
- Dupont TO PH2.0 4P
- De quoi isoler mécaniquement l'ESP de la carte ou le positionner facilement.

L'ESP est alimenté par le `+5V` du connecteur donc par le déshumidificateur lui-même.

---
## Câblage
| Carte (`WIFI CN1`) | ESP32-C3 Super Mini |
|---|---|
| `TXD` | `GPIO20` (RX) |
| `RXD` | **`GPIO7`** (TX) |
| `GND` | `GND` |
| `+5V` | `5V` |

### Pourquoi `GPIO7` et pas `GPIO21`
`GPIO21` est le `U0TXD` par défaut du C3. À **chaque reset**, le bootloader ROM y
imprime ses logs à 115200 bauds — donc directement dans le RX du MCU, qui attend
du 9600. Le MCU y voit une rafale de charabia, conclut que son module est
défaillant, verrouille son UART et affiche `U0` jusqu'à la prochaine coupure
secteur.

L'UART de l'ESP32-C3 passe par la matrice GPIO : n'importe quelle broche libre
convient. Avec le TX sur `GPIO7`, plus aucun reboot de l'ESP ne perturbe le MCU.

> ⚠️ Ne jamais laisser le connecteur USB branché en même temps que le `+5V` de la carte

---
## Protocole

Tuya MCU, **9600 8N1**, trames binaires :

```
55 AA | VER | CMD | LEN_HI LEN_LO | DATA… | CHK
```

- `CHK` = somme de tous les octets depuis `55` (header inclus) & `0xFF`
- `CMD 0x00` heartbeat · `0x01` product query · `0x02` conf query ·
  `0x03` wifi state · `0x07` report · `0x08` datapoint query

Identité produit renvoyée par le MCU :
```json
{"p":"bnrwkmfkce6hmxbm","v":"1.0.0","m":0}
```

### Datapoints
| DP | Type | R/W | Valeurs | Rôle |
|----|------|-----|---------|------|
| 1 | bool | RW | 0 / 1 | Marche / Arrêt |
| 2 | int 32 | RW | 30→90 par 5, **170 = AU**, **187 = CO** | Consigne |
| 4 | enum | RW | 0 = haut, 1 = bas | Ventilateur |
| 5 | enum | **RW** | 0 = CO, 1 = AU, 2 = manuel | Mode — lecture **et** pilotage |
| 6 | int 32 | RO | %RH | Humidité ambiante |
| 25 | enum | RO | = 1 | inconnu, constant |
| 101 | enum | RW | 0→24 | Timer en heures |

Les sentinelles `0xAA` (AU) et `0xBB` (CO) sont à priori un choix délibéré du firmware du
MCU, ce ne sont pas des valeurs corrompues.

---

## Pièges constatés

1. **Le MCU n'écoute rien s'il n'a pas démarré avec un module présent.**
   Le redémarrer, lui, est inutile : c'est le **déshumidificateur** qu'il faut
   mettre hors tension puis rebrancher.

2. **Le charabia du bootloader verrouille l'UART du MCU** → `U0` à l'écran.
   D'où le TX sur `GPIO7` (voir plus haut).

3. **Les trames doivent être espacées** (≥ 300 ms). Un burst collé est vu comme
   une trame unique invalide → bip d'erreur.

4. **AU et CO ne se pilotent pas par `dp2`.** Le MCU valide `30..90` sur ce
   datapoint et **ignore silencieusement** le reste — il ne ramène pas à 30, il
   conserve la valeur précédente. Les modes passent par **`dp5`**, déclaré RO
   côté spec Tuya mais bel et bien inscriptible :

   | Écriture `dp5` | Réponse du MCU | Mode |
   |---|---|---|
   | `0` | `dp2=187`, `dp5=0` | CO (continu) |
   | `1` | `dp2=170`, `dp5=1` | AU (auto) |
   | `2` | `dp2=30`, `dp5=2` | Manuel |

   ⚠️ Cela ne fonctionne **que si l'appareil est en marche** (`dp1=1`). En
   veille, le MCU répond de façon incohérente et revient à 30 — c'est ce qui
   nous a fait conclure à tort, un moment, que ces modes n'étaient pas
   pilotables. Passer en Manuel remet `dp2` à 30 : il faut enchaîner avec la
   consigne voulue.

5. **Après avoir accepté une écriture, le MCU ignore son panneau physique**
   jusqu'au prochain power-cycle. C'est le comportement normal du mode « module
   présent ».

---

## Utilisation

```bash
# 1. secrets
cp secrets.yaml.example secrets.yaml
$EDITOR secrets.yaml

# 2. compilation
esphome compile tuya.yaml

# 3. première fois en USB
esphome run tuya.yaml --device /dev/ttyACM0

# 4. ensuite, sans fil
esphome run tuya.yaml --device probreeze.local
```

### Consommation

La consommation est pilotée dynamiquement selon l'état de l'appareil :

- **en veille** → `WIFI_PS_MIN_MODEM`, ~20-30 mA, comme le module Tuya d'origine.  Le rail 5 V retrouve son régime d'origine et le buzzer cesse de gueuler (il était excité par l'ondulation 100 Hz, amplifiée par la charge continue).
- **en marche** → `WIFI_PS_NONE`, ~2 ms de latence. Le bruit est de toute façon
  couvert par le ventilateur et le compresseur.
Le CPU est fixé à 80 MHz, largement suffisant.

---

## Fichiers

| Fichier | Rôle |
|---|---|
| `tuya.yaml` | firmware final |
| `HOME_ASSISTANT.md` | référence des entités pour assistants IA |
| `NOTES.md` | notes de reverse-engineering complètes |
| `secrets.yaml.example` | modèle de configuration |
| `esp_wifi_ps.h` | en-tête requis pour `esp_wifi_set_ps()` en lambda |
| `sniff.yaml` | étape 1 : écoute passive, aucun octet émis |
| `listen.yaml` | écoute seule, TX en haute impédance |
| `probe.yaml` | handshake Tuya manuel + datapoint query |
| `probe-dp2.yaml`, `probe-dp2b.yaml` | tests d'écriture sur la consigne |
| `probe-fuzz.yaml` | énumération des valeurs de `dp4`, `dp5`, `dp101` |
| `tuya-sniff.yaml` | composant `tuya` + logs VERBOSE, pour comparaison |

---

## Licence
MIT.
