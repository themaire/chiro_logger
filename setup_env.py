#!/usr/bin/env python3
"""
Script de configuration de l'environnement de développement ESP32
pour le projet Chiro Logger
"""

import os
import sys
import subprocess
import venv
from pathlib import Path

def run_command(cmd, cwd=None):
    """Execute une commande et affiche le résultat"""
    print(f"🔧 Exécution: {cmd}")
    try:
        result = subprocess.run(cmd, shell=True, cwd=cwd, check=True, 
                              capture_output=True, text=True)
        if result.stdout:
            print(f"✅ {result.stdout}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"❌ Erreur: {e}")
        if e.stderr:
            print(f"❌ Détails: {e.stderr}")
        return False

def main():
    print("🦇 Configuration de l'environnement Chiro Logger")
    print("=" * 50)
    
    # Créer l'environnement virtuel
    venv_path = Path("./venv")
    if not venv_path.exists():
        print("📦 Création de l'environnement virtuel...")
        venv.create(venv_path, with_pip=True)
    else:
        print("✅ Environnement virtuel déjà présent")
    
    # Chemins pour l'activation
    if sys.platform == "darwin":  # macOS
        activate_script = venv_path / "bin" / "activate"
        pip_path = venv_path / "bin" / "pip"
    else:
        activate_script = venv_path / "Scripts" / "activate"
        pip_path = venv_path / "Scripts" / "pip"
    
    print(f"📍 Environnement virtuel créé dans: {venv_path.absolute()}")
    print(f"📍 Script d'activation: {activate_script}")
    
    # Installation des dépendances
    print("\n🔧 Installation des dépendances...")
    
    # Mise à jour pip
    run_command(f"{pip_path} install --upgrade pip")
    
    # Installation de PlatformIO
    run_command(f"{pip_path} install platformio")
    
    print("\n✅ Configuration terminée!")
    print("\n📋 Prochaines étapes:")
    print("1. Activer l'environnement virtuel:")
    print(f"   source {activate_script}")
    print("2. Compiler le projet:")
    print("   pio run")
    print("3. Uploader sur la carte:")
    print("   pio run --target upload")
    print("4. Monitorer le port série:")
    print("   pio device monitor")

if __name__ == "__main__":
    main()
