# ProBreeze — référence pour assistants IA

Ce document décrit **comment piloter ce déshumidificateur depuis Home Assistant**.
Il est écrit pour être lu par un agent LLM : chaque règle est explicite, chaque
piège est documenté. Aucune connaissance du protocole Tuya n'est nécessaire pour
s'en servir ; le détail matériel est dans [`README.md`](README.md).

---

## Identité de l'appareil

| | |
|---|---|
| Nom | `ProBreeze` |
| Intégration | ESPHome |
| Adresse | `probreeze.local` / `192.168.1.60` |
| Fabricant | DIY (ESP32-C3 dans un ProBreeze 12 L) |
| Entités | 11 |

---

## Entités exposées

Les `entity_id` ci-dessous sont ceux générés par défaut. Si Home Assistant a dû
renommer une entité (collision), le suffixe `_2` peut apparaître — se fier au nom
affiché plutôt qu'à l'`entity_id` en dur.

### Contrôle

| `entity_id` | Type | Valeurs | Effet |
|---|---|---|---|
| `switch.probreeze_marche` | switch | `on` / `off` | Marche / arrêt de l'appareil |
| `select.probreeze_mode` | select | `CO (continu)`, `AU (auto)`, `Manuel` | Mode de fonctionnement |
| `select.probreeze_consigne` | select | `30 %` → `90 %` par pas de 5 | Humidité cible |
| `select.probreeze_ventilateur` | select | `Haut`, `Bas` | Vitesse du ventilateur |
| `select.probreeze_timer` | select | `0 h` → `24 h` | Arrêt automatique différé |
| `button.probreeze_redemarrer` | button | — | Redémarre l'ESP (pas l'appareil) |

### Mesures et diagnostic

| `entity_id` | Type | Unité | Notes |
|---|---|---|---|
| `sensor.probreeze_humidite` | sensor | % | Humidité ambiante, rafraîchie toutes les 5 s |
| `sensor.probreeze_signal_wifi` | sensor | dBm | ~ −70 dBm, liaison faible mais stable |
| `sensor.probreeze_uptime` | sensor | s | Retombe à 0 si l'ESP redémarre |
| `text_sensor.probreeze_cause_du_dernier_reset` | text | — | Cause du dernier reboot |
| `text_sensor.probreeze_version_esphome` | text | — | Version du firmware |

---

## Modèle mental : comment les commandes interagissent

Trois entités forment un ensemble lié. **Les traiter indépendamment produit des
comportements inattendus.**

```
Marche (on/off)
  └── Mode (CO / AU / Manuel)
        ├── CO      → marche en continu, la Consigne est IGNORÉE
        ├── AU      → régulation automatique, la Consigne est IGNORÉE
        └── Manuel  → la Consigne s'applique
              └── Consigne (30 → 90 %)
```

### Règles à respecter

1. **`Mode` n'est modifiable que si l'appareil est en marche.**
   Si `switch.probreeze_marche` est `off`, toute tentative de changement de mode
   est **refusée** et l'entité revient à sa valeur précédente. C'est volontaire :
   en veille, le contrôleur de l'appareil traite mal ces commandes.

2. **Passer en `Manuel` remet la consigne à 30 %.**
   Toute séquence « repasser en manuel à X % » doit donc envoyer **deux**
   commandes : `Mode = Manuel`, puis `Consigne = X %`.

3. **En `CO` ou `AU`, `select.probreeze_consigne` n'a pas de sens.**
   Sa valeur affichée reste la dernière consigne manuelle et ne reflète pas
   l'état réel. Ne pas s'en servir pour déduire le fonctionnement.

4. **Espacer les commandes d'au moins 5 secondes.**
   Le contrôleur de l'appareil ignore les écritures trop rapprochées. Envoyer
   `Mode` puis `Consigne` dans la même seconde aboutit à une commande perdue.

5. **Ne pas redémarrer l'ESP sans raison.** `button.probreeze_redemarrer`
   redémarre l'ESP32, pas le déshumidificateur. Sans effet sur le compresseur.

6. **Le panneau physique peut être inactif.** Après une commande venue de Home
   Assistant, les boutons de l'appareil sont ignorés jusqu'à la prochaine
   coupure secteur. Ce n'est pas une panne.

---

## Recettes

### « Mets-le à 45 % »

```
1. switch.probreeze_marche   → on        (si nécessaire)
2. select.probreeze_mode     → Manuel
3. attendre 5 s
4. select.probreeze_consigne → 45 %
```

### « Mode continu » / « fais-le tourner sans arrêt »

```
1. switch.probreeze_marche → on        (obligatoire, sinon refusé)
2. select.probreeze_mode   → CO (continu)
```

### « Mode automatique »

```
1. switch.probreeze_marche → on
2. select.probreeze_mode   → AU (auto)
```

### « Arrête-le »

```
switch.probreeze_marche → off
```

Le `Timer` est remis à `0 h` automatiquement à l'extinction.

### « Arrête-le dans 3 heures »

```
1. switch.probreeze_marche → on
2. select.probreeze_timer  → 3 h
```

### « Ventilation maximale »

```
1. switch.probreeze_marche → on
2. select.probreeze_ventilateur → Haut
```

---

## Ce qu'il ne faut pas faire

| Ne pas | Pourquoi |
|---|---|
| Changer `Mode` appareil éteint | Refusé, la commande est perdue |
| Envoyer 3 commandes en 1 s | Le contrôleur en ignore une partie |
| Lire `Consigne` pour connaître le mode | Fausse en CO/AU |
| Supposer que `Consigne` vaut 50 après « Manuel » | Elle vaut **30**, valeur minimale |
| Déduire une panne de `sensor.probreeze_humidite` stable | Il bouge lentement, c'est normal |
| Utiliser `uptime` comme horloge | Ce sont des secondes depuis le boot de l'ESP |

---

## Diagnostic

| Symptôme | Cause probable | Action |
|---|---|---|
| `Mode` revient tout seul à sa valeur | Appareil en veille | Allumer d'abord |
| `Consigne` affiche 30 après un passage en Manuel | Comportement normal | Renvoyer la consigne voulue |
| `sensor.probreeze_uptime` retombe à 0 | L'ESP a redémarré | Lire `cause_du_dernier_reset` |
| Cause = `brownout` | Chute du 5 V : la carte ne fournit pas assez | Vérifier l'alimentation — et ne **jamais** alimenter par USB en même temps que par le `+5V` du connecteur |
| Cause = `exception/panic` ou `task watchdog` | Firmware instable | Reflasher en USB |
| Appareil injoignable | WiFi 2.4 GHz uniquement | Vérifier que le SSID est diffusé en 2,4 GHz |
| Aucune entité ne répond | ESP planté | `button.probreeze_redemarrer` |

---

## Limites matérielles connues

- **WiFi 2,4 GHz uniquement.** L'ESP32-C3 ne voit pas la bande 5 GHz.
- **La consigne va de 30 à 90 %**, par pas de 5. Les valeurs intermédiaires
  (42 %, 67 %) sont impossibles.
- **Le timer va de 0 à 24 h**, en heures entières.
- **`Ventilateur` n'a que deux vitesses.** `Haut` correspond à la valeur 0 et
  `Bas` à la valeur 1 dans l'enum du constructeur — c'est inversé par rapport à
  l'intuition, mais les libellés exposés à Home Assistant sont corrects.
- **La consommation est réduite en veille** (économie d'énergie WiFi) : compter
  ~50 à 100 ms de latence sur les commandes quand l'appareil est à l'arrêt, ~2 ms
  quand il tourne.
