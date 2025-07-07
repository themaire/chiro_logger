# 🦇 Projet de Datalogger Température & Humidité pour Cavités à Chiroptères

![Concept du Datalogger Chiro](assets/images/chiro_logger_concept.png)

> 📱 **Application mobile associée :** [Angular Chiro App](https://github.com/themaire/angular_chiro_app) - PWA pour la récupération des données via Bluetooth BLE
>
> 💡 **Qu'est-ce qu'une PWA ?**  
> Une Progressive Web App (PWA) est une application web qui fonctionne comme une app mobile native. Elle peut être installée sur smartphone, fonctionne hors-ligne, accède aux APIs natives (Bluetooth, géolocalisation...) et offre une expérience utilisateur fluide. Pas besoin de passer par les stores d'applications !

## 🎯 Objectif

Concevoir et déployer un **datalogger autonome et discret** permettant la mesure **long terme** de la **température**, de l’**humidité** et de la **pression atmosphérique** dans des **cavités naturelles** ou souterraines **occupées par des chauves-souris (chiroptères)**.

L’objectif est de récolter des données environnementales précises, sans perturber les conditions locales, afin de mieux comprendre les dynamiques microclimatiques des sites d’hivernage.

## 🧪 Contexte scientifique

Dans les études de suivi des chiroptères, la précision des mesures et la **non-perturbation du milieu** sont essentielles. De simples variations dues à l'ouverture d’un boîtier peuvent créer des artefacts dans les séries de données, voire impacter le comportement des individus. Il est donc nécessaire que :

- L’appareil soit **étanche et totalement autonome**
- Les mesures soient **horodatées** avec précision
- Les données soient **récupérables sans contact physique**

## ⚙️ Spécifications techniques du dispositif

### Matériel principal

| Composant | Rôle | Remarques |
|----------|------|-----------|
| **LOLIN/Wemos D32 Pro (ESP32)** | Microcontrôleur principal | Faible consommation, BLE intégré, slot microSD |
| **BME280** | Capteur de température, humidité, pression | Haute précision, I2C |
| **DS3231 RTC** | Horloge temps réel | Pour horodatage même en deep sleep |
| **Carte microSD** | Stockage local des mesures | Format `.csv` lisible par tableur |
| **Capteur capacitif (ex. TTP223)** | Déclencheur sans contact | Permet d’activer le mode "transfert de données" |
| **Batterie LiPo 3.7V (≥1000mAh)** | Alimentation autonome | Autonomie estimée à plusieurs semaines/mois |
| **Diviseur de tension ou MAX17048** | Suivi du niveau de batterie | Pour surveiller l'autonomie via le programme |

### Fonctionnement logiciel

#### 🔁 Mode normal (acquisition)

- Réveil toutes les X minutes (configurable)
- Lecture des capteurs BME280 + tension batterie
- Horodatage via RTC
- Enregistrement sur carte SD au format CSV
- Remise en sommeil profond (deep sleep)

#### 🔋 Deep Sleep - Optimisation énergétique

Le datalogger utilise le **mode deep sleep** de l'ESP32 pour maximiser l'autonomie :

**⚡ Consommation :**
- **Mode actif** (mesure + écriture SD) : ~100-200 mA pendant 2-3 secondes
- **Mode deep sleep** : ~10-20 µA (microampères)
- **Économie d'énergie** : >99% du temps en veille

**📊 Gestion intelligente des données avec tampon flash :**

Le datalogger utilise un système de **tampon flash interne** pour optimiser l'utilisation de la carte SD :

1. **Stockage temporaire** : Les mesures sont d'abord stockées dans la **flash interne de l'ESP32** (partition SPIFFS de 15MB)
2. **Économie d'énergie** : La carte SD n'est activée que lors du **flush périodique**
3. **Flush automatique** : Transfert des données vers la SD toutes les **1000 mesures** (configurable)

**🕒 Timing avec mesures toutes les 5 secondes :**

- **Mesures 1-999** : Stockées dans le tampon flash
- **Mesure 1000** : Déclenchement du flush → activation SD → transfert des 1000 mesures → extinction SD
- **Cycle suivant** : Reprend avec le tampon vide

**⚡ Économie d'énergie réalisée :**

- **Sans tampon** : SD activée à chaque mesure (5s) = 720 activations/heure
- **Avec tampon** : SD activée toutes les 1000 mesures = 1 activation toutes les **83 heures**
- **Réduction** : **99.9% d'activations SD en moins** = autonomie considérablement prolongée

**🔍 Monitoring dans les logs :**

```text
📊 Tampon: 999/1000 mesures
📊 Tampon: 1000/1000 mesures
🔄 Seuil atteint - flush vers la carte SD...
📊 Flush de 1000 mesures vers la SD
✅ 1000 lignes copiées vers la SD
🧹 Tampon flash vidé
✅ Flush réussi - tampon vidé
```

**💾 Fiabilité des données :**

- Données sécurisées dans la flash interne (persistante après coupure)
- Mode dégradé automatique : écriture directe sur SD si tampon indisponible
- Aucune perte de données même en cas de problème SD temporaire

**⚡ Économie d'énergie réalisée :**

- **Sans tampon** : SD activée à chaque mesure (5s) = 720 activations/heure
- **Avec tampon** : SD activée toutes les 1000 mesures = 1 activation toutes les **83 heures**
- **Réduction** : **99.9% d'activations SD en moins** = autonomie considérablement prolongée

- Mode actif : ~80 mA
- Deep sleep : ~10 µA (8000x moins !)

**🔄 Cycle de fonctionnement :**

```text
🚀 Démarrage initial du système
📊 Cycle de mesure #1
🌡️  Mesure: T=18.7°C, H=85.4%
💾 Données sauvegardées sur SD
 Entrée en deep sleep pour 5 secondes...
    [5 secondes plus tard - REDÉMARRAGE COMPLET]
⏰ Réveil du deep sleep (timer)
📊 Cycle de mesure #2
🌡️  Mesure: T=18.9°C, H=85.8%
💾 Données sauvegardées sur SD
💤 Entrée en deep sleep pour 5 secondes...
    [cycle se répète...]
```

**🎛️ Configuration :**

- Délai configurable via `DEEP_SLEEP_DURATION_SEC` (actuellement 5 secondes)
- Suggestions : 30s (monitoring), 300s (économie max), 60s (compromis)

**🛡️ Sécurité des données :**

- Chaque fichier CSV est fermé immédiatement après écriture
- Le deep sleep provoque un redémarrage complet qui nettoie automatiquement toutes les structures
- Récupération automatique de la carte SD à chaud
- Pas de corruption possible grâce au redémarrage propre de l'ESP32

#### 🔄 Gestion robuste de la carte SD (Hot-Plug)

Le système gère intelligemment les insertions/retraits de carte SD :

**🔍 Détection automatique :**

- Détection immédiate d'une déconnexion lors d'une écriture
- Passage automatique en mode "sans carte SD"
- Messages explicites dans les logs

**⚡ Récupération automatique :**

- Tentative de récupération toutes les 25 secondes (5 cycles)
- Test de fonctionnalité avant reprise d'écriture
- Gestion sécurisée du bus SPI (pas de crash)

**📊 Comportement observé :**

```text
💾 Données sauvegardées sur SD
[Retrait carte à chaud]
❌ Impossible d'ouvrir le fichier CSV
🔌 Carte SD déconnectée détectée - démontage...
⚠️  Carte SD non disponible - données non sauvegardées
[Réinsertion carte]
🔍 Tentative de récupération de la carte SD...
🎉 Carte SD récupérée avec succès!
✅ Test de récupération SD réussi
💾 Données sauvegardées sur SD
```

#### 📲 Mode consultation (sans contact)

- L’approche d’un doigt ou badge active un **capteur capacitif** à travers le boîtier étanche
- Le microcontrôleur **réveille le module Bluetooth Low Energy (BLE)**
- Un **smartphone** à proximité peut se connecter à l’appareil
- Le fichier `.csv` est transmis ligne par ligne via BLE

## 📱 Application de consultation (mobile)

Une application web Angular (PWA – progressive web app) permettra aux agents de :

- Se connecter à l’ESP32 via **Bluetooth BLE**
- Lire les fichiers de mesures enregistrés
- Les afficher dans une **table lisible**
- **Télécharger** les fichiers `.csv` pour traitement

➡️ Aucun câble, aucune ouverture, aucune manipulation du boîtier n’est nécessaire.

## ✅ Avantages du dispositif

- **Non-intrusif** : pas de contact physique avec le boîtier = pas de perturbation thermique
- **Économe** : deep sleep + composants basse consommation = autonomie longue
- **Modulaire** : peut évoluer vers d'autres capteurs ou usages (CO₂, mouvement, etc.)
- **Open Source** : basées sur des technologies libres, les solutions peuvent être adaptées et maintenues en interne
- **Valorisable** : les données produites sont exploitables directement pour la recherche, la communication ou la conservation

## 🧮 Estimation budgétaire

| Élément | Prix unitaire estimé (€) |
|--------|---------------------------|
| D32 Pro ESP32 | ~10 € |
| BME280 | ~4 € |
| DS3231 RTC | ~2,50 € |
| Carte microSD 8–16 Go | ~5 € |
| Capteur capacitif | ~1 € |
| Batterie LiPo 1000–2000 mAh | ~6–10 € |
| Boîtier étanche (IP65/67) | ~5–10 € |
| **Total estimé par unité** | **35 à 45 €** |

## 📦 Livrables proposés

- 📟 Firmware ESP32 prêt à flasher (Arduino ou ESP-IDF)
- 📄 Fichier `.csv` exportable avec : date, heure, température, humidité, pression, tension batterie
- 📲 Application web Angular compatible smartphones Android/iOS pour la récupération sans fil des données
- 📘 Documentation d’installation, mise en service, et maintenance

## 🚀 Installation et Configuration

### Prérequis

- macOS avec Python 3
- Git installé

### Setup initial (première fois)

```bash
# Cloner le repository
git clone https://github.com/ton-username/chiro_logger.git
cd chiro_logger

# Configurer l'environnement de développement
./setup_env.sh
```

### Utilisation quotidienne

```bash
# Compiler, flasher et monitorer
./pio.sh full

# Ou commandes individuelles
./pio.sh compile
./pio.sh flash
./pio.sh monitor
./pio.sh help
```

## 📋 Structure du projet

### 🦇 Hardware (ce repo)

```plaintext
chiro_logger/
├── src/           # Code source C/C++ ESP32
├── include/       # Headers
├── platformio.ini # Configuration PlatformIO
├── pio.sh         # Script d'automatisation
├── setup_env.sh   # Script d'installation
└── venv/          # Environnement virtuel (non versionné)
```

### 📱 Software (repos séparés)

- **[Angular Chiro App](https://github.com/themaire/angular_chiro_app)** - Application PWA mobile pour la récupération des données via Bluetooth BLE

## 🚀 Prochaine étape

➡️ Création d’un **prototype fonctionnel** pour validation de l'approche technique et tests de terrain.
