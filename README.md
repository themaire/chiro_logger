# 🦇 Projet de Datalogger Température & Humidité pour Cavités à Chiroptères

![Concept du Datalogger Chiro](assets/images/chiro_logger_concept.png)

> A aller voir ..... [Angular Chiro App](https://github.com/themaire/angular_chiro_app) - PWA pour la récupération des données via Bluetooth BLE
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

**⚡ Consommation mesurée :**

- **Mode actif** (mesure + écriture tampon) : ~80-120 mA pendant 1-2 secondes
- **Mode deep sleep** : ~10-20 µA (microampères)
- **Économie d'énergie** : >99% du temps en veille

**🧪 Tests d'autonomie réels :**

**🔋 Test initial (mode debug) :**

- **Batterie testée** : LiPo 150mAh seulement
- **Configuration** : Mesures toutes les 5 secondes
- **Résultat** : **3700 mesures** jusqu'à épuisement total
- **Durée** : ~5 heures de fonctionnement continu

**🚀 Test optimisé (mode production ultra-silencieux) :**

- **Batterie testée** : LiPo 150mAh (même batterie)
- **Configuration** : Mesures toutes les 5 secondes
- **Résultat** : **6714 mesures** jusqu'à épuisement total ✨
- **Durée** : **~9h20** de fonctionnement continu (6714 × 5s)
- **Amélioration** : **+81% d'autonomie** grâce aux optimisations !

**📊 Projections d'autonomie :**

Basées sur les **résultats réels optimisés**, voici les estimations d'autonomie selon la batterie et l'intervalle de mesure :

| Batterie | Intervalle 5s | Intervalle 30min | Intervalle 1h |
|----------|---------------|------------------|---------------|
| **150mAh** | **9h20** (6714 mesures) | **~4 mois** | **~8 mois** |
| **500mAh** | **~31h** | **~13 mois** | **~26 mois** |
| **1000mAh** | **~62h** | **~26 mois** | **~52 mois** |
| **2000mAh** | **~124h** | **~52 mois** | **~104 mois** |

> 🦇 **Performance exceptionnelle :** Avec une batterie de 1000mAh et un intervalle de 30 minutes, le datalogger peut fonctionner **plus de 2 ans** dans une cavité !

**📊 Gestion intelligente des données avec tampon flash :**

Le datalogger utilise un système de **tampon flash interne** pour optimiser l'utilisation de la carte SD :

1. **Stockage temporaire** : Les mesures sont d'abord stockées dans la **flash interne de l'ESP32** (partition SPIFFS de 15MB)
2. **Économie d'énergie** : La carte SD n'est activée que lors du **flush périodique**
3. **Flush automatique** : Transfert des données vers la SD toutes les **500 mesures** (optimisé)

**🕒 Timing avec mesures toutes les 5 secondes :**

- **Mesures 1-499** : Stockées dans le tampon flash
- **Mesure 500** : Déclenchement du flush → activation SD → transfert des 500 mesures → extinction SD
- **Cycle suivant** : Reprend avec le tampon vide

**⚡ Économie d'énergie réalisée :**

- **Sans tampon** : SD activée à chaque mesure (5s) = 720 activations/heure
- **Avec tampon** : SD activée toutes les 500 mesures = 1 activation toutes les **42 heures**
- **Réduction** : **99.8% d'activations SD en moins** = autonomie considérablement prolongée

**🔍 Monitoring dans les logs :**

```text
📊 Tampon: 499/500 mesures
📊 Tampon: 500/500 mesures
🔄 Seuil atteint - flush vers la carte SD...
📊 Flush de 500 mesures vers la SD
✅ 500 lignes copiées vers la SD
🧹 Tampon flash vidé
✅ Flush réussi - tampon vidé
```

**💾 Fiabilité des données :**

- Données sécurisées dans la flash interne (persistante après coupure)
- Mode dégradé automatique : écriture directe sur SD si tampon indisponible
- Aucune perte de données même en cas de problème SD temporaire

**⚡ Économie d'énergie réalisée :**

- **Sans tampon** : SD activée à chaque mesure (5s) = 720 activations/heure
- **Avec tampon** : SD activée toutes les 500 mesures = 1 activation toutes les **42 heures**
- **Réduction** : **99.8% d'activations SD en moins** = autonomie considérablement prolongée

- Mode actif : ~80 mA
- Deep sleep : ~10 µA (8000x moins !)

**� Feedback visuel LED :**

Le datalogger intègre un **système de feedback LED** pour monitorer son fonctionnement :

- **1 clignotement** : Mesure ajoutée au tampon flash
- **10 clignotements rapides** : Flush des données vers la carte SD
- **LED éteinte** : Mode deep sleep (économie d'énergie maximale)

Ce système permet de vérifier visuellement que l'appareil fonctionne sans perturber son cycle de sommeil.

## 💡 Innovation RTC : Compteur persistant entre deep sleeps

🚀 **Pourquoi c'est techniquement stylé :**

La plupart des dataloggers "oublient" combien de mesures ils ont effectuées à chaque réveil. Ce datalogger utilise la **RTC Memory** de l'ESP32 pour maintenir un **compteur global persistant** !

**🔧 Implémentation technique :**

```c
// Variable stockée en RTC Memory - survit au deep sleep !
RTC_DATA_ATTR int cycle_counter = 0;

// À chaque réveil :
cycle_counter++;  // Le compteur continue de compter !
ESP_LOGI(TAG, "📊 Cycle de mesure #%d", cycle_counter);

// L'ID est utilisé comme première colonne du CSV
add_to_flash_buffer(cycle_counter, datetime_str, temp, humidity);
```

**📄 Format CSV enrichi :**

Le fichier CSV généré contient maintenant un **ID unique croissant** pour chaque mesure :

```csv
ID,DateTime,Temperature_C,Humidity_%
1,1672531200,18.5,85.0
2,1672531205,18.6,85.2
3,1672531210,18.7,85.4
...
1247,1672535435,19.2,86.1
```

**✨ Avantages uniques :**

- **Numérotation continue** : Cycles #1, #2, #3... même après des semaines
- **ID unique dans CSV** : Chaque mesure a un identifiant permanent et croissant
- **Diagnostic précis** : "Le datalogger a effectué exactement 1247 mesures"  
- **Détection de pertes** : Si l'ID saute de 100 à 110, on sait que 9 mesures manquent
- **Consommation nulle** : La RTC Memory ne consomme que quelques µA
- **Fiabilité totale** : Reset uniquement lors d'un redémarrage complet

**📊 Dans les logs :**

```text
⏰ Réveil du deep sleep (timer) - Cycle #1247
📊 Compteur RTC persistant: 1246 ✨ (survit depuis le début!)
📊 Cycle de mesure #1247
💾 CSV: ID=1247, T=19.2°C, H=86.1%
```

> 💡 **Magie technique :** Même après 1000 réveils de deep sleep, le système sait parfaitement qu'il en est à sa 1000ème mesure !

**�🔄 Cycle de fonctionnement avec compteur persistant :**

```text
🚀 Démarrage initial du système - Reset du compteur
📊 Compteur RTC persistant: 0
📊 Cycle de mesure #1
💡 LED: 1 clignotement (mesure ajoutée au tampon)
🌡️  Mesure: T=18.7°C, H=85.4%
� Mesure stockée dans le tampon flash
📊 Tampon: 1/500 mesures
💤 Entrée en deep sleep pour 5 secondes...
    [5 secondes plus tard - REDÉMARRAGE COMPLET]
⏰ Réveil du deep sleep (timer) - Cycle #2
📊 Compteur RTC persistant: 1
📊 Cycle de mesure #2
💡 LED: 1 clignotement (mesure ajoutée au tampon)
🌡️  Mesure: T=18.9°C, H=85.8%
� Tampon: 2/5 mesures
    [... cycles 3, 4...]
📊 Cycle de mesure #5
📊 Tampon: 5/5 mesures
🔄 Seuil atteint - flush vers la carte SD...
💡 LED: 10 clignotements rapides (flush SD)
✅ Flush réussi - tampon vidé
💤 Entrée en deep sleep pour 5 secondes...
    [cycle se répète avec compteur persistant...]
```

**🎛️ Configuration optimisée :**

- **Délai actuel** : `DEEP_SLEEP_DURATION_SEC = 5` (pour tests rapides)
- **Suggestions déploiement** :
  - **30 minutes** (1800s) : Monitoring climatique standard - **Autonomie 2+ mois**
  - **1 heure** (3600s) : Surveillance long terme - **Autonomie 4+ mois**
  - **6 heures** (21600s) : Études saisonnières - **Autonomie 2+ ans**
- **Tampon flash** : `BUFFER_FLUSH_THRESHOLD = 500` (optimisé pour autonomie)
- **Mode production** : `#define PRODUCTION_MODE` pour désactiver les logs de debug

**⚡ Optimisation énergétique avancée :**

**📟 Gestion intelligente des logs :**

Les logs de débogage consomment de l'énergie ! Le datalogger utilise un système de **logs conditionnels** :

```c
// Mode développement : Tous les logs actifs
LOG_ESSENTIAL(TAG, "📊 Cycle #%d");    // Toujours affiché
LOG_DEBUG(TAG, "🔋 Mesure ajoutée");   // Affiché en développement
LOG_VERBOSE(TAG, "🔧 Détail debug");   // Affiché en développement

// Mode production : Logs essentiels uniquement
#define PRODUCTION_MODE  // Désactive DEBUG et VERBOSE
```

**⚡ Économie réalisée en production :**

- **Mode debug** : ~100 logs par cycle = +200ms d'activité
- **Mode production** : ~10 logs essentiels = +50ms d'activité  
- **Gain énergétique** : **75% de réduction** du temps d'activité des logs

**🚀 Mode production ULTRA-SILENCIEUX :**

Configuration finale pour **autonomie maximale** - tous les logs système ESP-IDF désactivés :

**📁 Configuration `sdkconfig.defaults` :**

```ini
# 🔋 OPTIMISATION ÉNERGÉTIQUE - DÉSACTIVATION COMPLÈTE DES LOGS SYSTÈME
CONFIG_LOG_DEFAULT_LEVEL_NONE=y
CONFIG_LOG_DEFAULT_LEVEL=0
CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y  
CONFIG_BOOTLOADER_LOG_LEVEL=0
CONFIG_LOG_MAXIMUM_LEVEL=0
CONFIG_ESP_CONSOLE_UART_NONE=y
```

**🔧 Résultat de l'optimisation :**

**AVANT** (mode debug) :

```text
I (31) boot: ESP-IDF 5.3.1 2nd stage bootloader
I (31) boot: compile time Jul  7 2025 22:57:51
I (392) CHIRO_LOGGER: 🦇 Chiro Logger - Datalogger pour chiroptères
I (392) CHIRO_LOGGER: ⏰ Réveil du deep sleep (timer) - Cycle #4
I (1552) CHIRO_LOGGER: 📊 Cycle de mesure #4
```

**APRÈS** (mode production) :

```text
ets Jun  8 2016 00:22:57
rst:0x5 (DEEPSLEEP_RESET),boot:0x17 (SPI_FAST_FLASH_BOOT)
mode:DIO, clock div:2
entry 0x40080580
```

**🔍 Explication des messages restants :**

- **`ets Jun 8 2016`** : **ETS** = **Espressif Test Suite** (ROM bootloader)
- **Timestamp figé** : Date de compilation du firmware ROM d'Espressif (normal)
- **Messages ROM** : Viennent du **silicium ESP32**, **impossible à supprimer**
- **Impact énergétique** : **Négligeable** (~10ms d'affichage au réveil)

**⚡ Impact des optimisations énergétiques :**

L'amélioration spectaculaire de **+81% d'autonomie** démontre l'efficacité des optimisations :

**🔧 Optimisations appliquées :**

1. **Logs système ESP-IDF désactivés** : Plus de logs bootloader/système
2. **Logs applicatifs conditionnels** : Seuls les logs essentiels en production
3. **Tampon flash optimisé** : SD activée seulement 13 fois (6714÷500) vs 6714 fois
4. **Mode production silencieux** : Pas de gaspillage énergétique UART
5. **Deep sleep parfait** : Consommation < 10µA en veille

**📈 Comparaison des résultats :**

| Mode | Mesures | Durée | Amélioration |
|------|---------|-------|-------------|
| **Debug initial** | 3700 | ~5h | Baseline |
| **Production optimisé** | **6714** | **~9h20** | **+81%** |

**💡 Conclusion :** Les optimisations de logs et de gestion énergétique permettent de **quasi-doubler l'autonomie** !

## 📡 Mode transfert Bluetooth BLE

### 🔄 Récupération des données sans contact

Le datalogger intègre un **mode transfert BLE** permettant de récupérer les données stockées **sans ouvrir le boîtier** :

**🎛️ Activation du mode transfert :**

- **Déclencheur** : Appui sur bouton GPIO0 (BOOT) pendant le deep sleep
- **Réveil automatique** : L'ESP32 détecte l'appui et active le BLE
- **Service GATT** : Service personnalisé avec UUIDs 128 bits
  - **Service :** `12345678-1234-1234-1234-123456789ABC`
  - **Caractéristique données :** `87654321-4321-4321-4321-CBA987654321`
- **Publicité active** : Device visible comme "ChiroLogger"
- **Connexion PWA** : L'application web se connecte automatiquement

**⚡ Fonctionnement optimisé :**

- **Timeout intelligent** : Mode BLE actif pendant 5 minutes maximum
- **Retour automatique** : Retour en deep sleep après transfert ou timeout
- **Économie d'énergie** : BLE activé uniquement à la demande
- **Feedback LED** : Indication visuelle du mode actif

**📱 Compatibilité PWA :**

Le système fonctionne avec l'[Angular Chiro App](https://github.com/themaire/angular_chiro_app), une PWA qui :

- Se connecte automatiquement au datalogger via Web Bluetooth API
- Récupère et affiche les données CSV en temps réel  
- Fonctionne sur smartphone/tablette sans installation
- Permet l'export et l'analyse des données sur le terrain

**🛠️ Implémentation technique :**

```c
// Structure modulaire du composant BLE
components/ble_transfer/
├── ble_manager.h        // API publique + UUIDs
├── ble_manager.c        // Service GATT + callbacks + conversion UUID
└── CMakeLists.txt       // Dépendances BT (bt, nvs_flash)

// Conversion UUID correcte (little-endian pour ESP32)
static void uuid_string_to_bin(const char *uuid_str, uint8_t *uuid_bin);

// Séquence de connexion BLE
1. ESP_GATTS_REG_EVT -> Création service GATT
2. ESP_GATTS_CREATE_EVT -> Ajout caractéristique données  
3. ESP_GATTS_ADD_CHAR_EVT -> Service démarré
4. ESP_GATTS_CONNECT_EVT -> Client connecté, publicité arrêtée
5. ESP_GATTS_READ_EVT -> Envoi des données CSV

// Intégration dans main.c
esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    handle_transfer_mode();  // Mode BLE actif
} else {
    perform_measurement();   // Mode normal
}
```

**🔒 Sécurité et fiabilité :**

- **Données en lecture seule** : Aucune modification possible via BLE
- **Mode temporaire** : BLE désactivé en fonctionnement normal
- **Impact nul sur l'autonomie** : Mode transfert purement optionnel

Cette innovation permet une **récupération des données totalement non-intrusive**, essentielle pour les études sur terrain sensible.
