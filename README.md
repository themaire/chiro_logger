# 🦇 Projet de Datalogger Température & Humidité pour Cavités à Chiroptères

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
