#!/bin/bash

# Script utilitaire pour PlatformIO - Projet Chiro Logger
# Usage: ./pio.sh [commande]

# Couleurs pour l'affichage
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Fonction d'aide
show_help() {
    echo -e "${BLUE}🦇 Chiro Logger - Script PlatformIO${NC}"
    echo -e "${BLUE}======================================${NC}"
    echo
    echo -e "${GREEN}Usage:${NC} ./pio.sh [commande]"
    echo
    echo -e "${GREEN}Commandes disponibles:${NC}"
    echo -e "  ${YELLOW}compile${NC}     - Compiler le projet"
    echo -e "  ${YELLOW}flash${NC}       - Flasher le firmware sur la carte"
    echo -e "  ${YELLOW}monitor${NC}     - Monitorer la sortie série"
    echo -e "  ${YELLOW}build-flash${NC} - Compiler et flasher en une seule commande"
    echo -e "  ${YELLOW}full${NC}        - Compiler, flasher et monitorer"
    echo -e "  ${YELLOW}clean${NC}       - Nettoyer le projet"
    echo -e "  ${YELLOW}clean-all${NC}   - Nettoyage complet (supprime .pio)"
    echo -e "  ${YELLOW}list${NC}        - Lister les ports série disponibles"
    echo -e "  ${YELLOW}reset${NC}       - Redémarrer la carte ESP32"
    echo -e "  ${YELLOW}help${NC}        - Afficher cette aide"
    echo
    echo -e "${GREEN}Exemples:${NC}"
    echo -e "  ./pio.sh compile"
    echo -e "  ./pio.sh flash"
    echo -e "  ./pio.sh full"
    echo
}

# Vérifier que l'environnement virtuel existe
check_venv() {
    if [ ! -d "venv" ]; then
        echo -e "${RED}❌ Erreur: Environnement virtuel 'venv' non trouvé${NC}"
        echo -e "${YELLOW}💡 Exécutez d'abord: python3 -m venv venv && source venv/bin/activate && pip install platformio${NC}"
        exit 1
    fi
}

# Détecter le port ESP32 automatiquement
detect_port() {
    # Chercher les ports USB/UART ESP32
    if ls /dev/cu.usbmodem* 2>/dev/null | grep -q .; then
        PORT=$(ls /dev/cu.usbmodem* | head -1)
    elif ls /dev/cu.usbserial* 2>/dev/null | grep -q .; then
        PORT=$(ls /dev/cu.usbserial* | head -1)
    elif ls /dev/ttyUSB* 2>/dev/null | grep -q .; then
        PORT=$(ls /dev/ttyUSB* | head -1)
    else
        PORT=""
    fi
    echo "$PORT"
}

# Fonction principale
main() {
    # Vérifier les prérequis
    check_venv
    
    # Gestion des paramètres (conversion en minuscules compatible macOS)
    param=$(echo "${1}" | tr '[:upper:]' '[:lower:]')
    case "${param}" in
        "compile"|"build")
            echo -e "${BLUE}🔧 Compilation du projet...${NC}"
            venv/bin/pio run -t clean && venv/bin/pio run
            ;;
        "flash"|"upload")
            echo -e "${BLUE}⚡ Flashage du firmware...${NC}"
            PORT=$(detect_port)
            if [ -z "$PORT" ]; then
                echo -e "${RED}❌ Erreur: Aucun port ESP32 détecté${NC}"
                echo -e "${YELLOW}💡 Utilisez: ./pio.sh list${NC}"
                exit 1
            fi
            echo -e "${GREEN}✓ Port détecté: $PORT${NC}"
            venv/bin/pio run --target upload -e lolin_c3_mini --upload-port "$PORT"
            ;;
        "monitor"|"serial")
            echo -e "${BLUE}📺 Monitoring série...${NC}"
            echo -e "${YELLOW}💡 Sortie avec Ctrl+C${NC}"
            PORT=$(detect_port)
            if [ -z "$PORT" ]; then
                echo -e "${RED}❌ Erreur: Aucun port ESP32 détecté${NC}"
                echo -e "${YELLOW}💡 Utilisez: ./pio.sh list${NC}"
                exit 1
            fi
            echo -e "${GREEN}✓ Port détecté: $PORT${NC}"
            venv/bin/pio device monitor --port "$PORT"
            ;;
        "build-flash"|"bf")
            echo -e "${BLUE}🔧⚡ Compilation et flashage...${NC}"
            PORT=$(detect_port)
            if [ -z "$PORT" ]; then
                echo -e "${RED}❌ Erreur: Aucun port ESP32 détecté${NC}"
                echo -e "${YELLOW}💡 Utilisez: ./pio.sh list${NC}"
                exit 1
            fi
            echo -e "${GREEN}✓ Port détecté: $PORT${NC}"
            venv/bin/pio run --target upload -e lolin_c3_mini --upload-port "$PORT"
            ;;
        "full"|"all")
            echo -e "${BLUE}🔧⚡📺 Compilation, flashage et monitoring...${NC}"
            PORT=$(detect_port)
            if [ -z "$PORT" ]; then
                echo -e "${RED}❌ Erreur: Aucun port ESP32 détecté${NC}"
                echo -e "${YELLOW}💡 Utilisez: ./pio.sh list${NC}"
                exit 1
            fi
            echo -e "${GREEN}✓ Port détecté: $PORT${NC}"
            venv/bin/pio run --target upload -e lolin_c3_mini --upload-port "$PORT" && venv/bin/pio device monitor --port "$PORT"
            ;;
        "clean")
            echo -e "${BLUE}🧹 Nettoyage du projet...${NC}"
            venv/bin/pio run --target clean
            ;;
        "clean-all"|"cleanall")
            echo -e "${BLUE}🧹💥 Nettoyage complet du projet...${NC}"
            echo -e "${YELLOW}💡 Suppression du dossier .pio pour un rebuild complet${NC}"
            rm -rf .pio
            echo -e "${GREEN}✅ Nettoyage complet terminé${NC}"
            ;;
        "list"|"ports")
            echo -e "${BLUE}📋 Ports série disponibles:${NC}"
            venv/bin/pio device list
            ;;
        "reset"|"restart")
            echo -e "${BLUE}🔄 Redémarrage de la carte ESP32...${NC}"
            # Utiliser le moniteur PlatformIO pour reset
            PORT=$(venv/bin/pio device list | grep -o '/dev/cu\.usbserial-[0-9]*' | head -1)
            if [ -n "$PORT" ]; then
                echo -e "${YELLOW}💡 Port détecté: $PORT${NC}"
                echo -e "${YELLOW}💡 Appuyez sur le bouton RESET de la carte ou débranchez/rebranchez le câble USB${NC}"
                echo -e "${YELLOW}💡 Puis utilisez: ./pio.sh monitor${NC}"
            else
                echo -e "${RED}❌ Erreur: Aucun port ESP32 détecté${NC}"
                exit 1
            fi
            ;;
        "help"|"-h"|"--help")
            show_help
            ;;
        "")
            echo -e "${RED}❌ Erreur: Aucune commande spécifiée${NC}"
            echo
            show_help
            exit 1
            ;;
        *)
            echo -e "${RED}❌ Erreur: Commande '${param}' non reconnue${NC}"
            echo
            show_help
            exit 1
            ;;
    esac
}

# Exécution du script
main "$@"
