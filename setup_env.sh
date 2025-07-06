#!/bin/bash
# Script de configuration pour macOS

echo "🦇 Configuration de l'environnement Chiro Logger pour macOS"
echo "========================================================="

# Vérifier si Python est installé
if ! command -v python3 &> /dev/null; then
    echo "❌ Python3 n'est pas installé. Installez-le avec:"
    echo "   brew install python3"
    exit 1
fi

# Créer et activer l'environnement virtuel
echo "📦 Création de l'environnement virtuel..."
python3 -m venv venv

echo "🔄 Activation de l'environnement virtuel..."
source venv/bin/activate

# Mise à jour pip
echo "⬆️ Mise à jour de pip..."
pip install --upgrade pip

# Installation de PlatformIO
echo "🔧 Installation de PlatformIO..."
pip install platformio

# Installation des outils ESP-IDF
echo "🔧 Installation des outils ESP-IDF..."
pio pkg install --global --platform espressif32@6.9.0

echo ""
echo "✅ Configuration terminée!"
echo ""
echo "📋 Pour utiliser l'environnement:"
echo "   source venv/bin/activate"
echo ""
echo "📋 Commandes PlatformIO disponibles:"
echo "   pio run                    # Compiler le projet"
echo "   pio run --target upload    # Uploader sur la carte"
echo "   pio device monitor         # Monitorer le port série"
echo "   pio device list            # Lister les ports série"
echo ""
echo "🔗 Documentation:"
echo "   https://docs.platformio.org/en/latest/platforms/espressif32.html"
