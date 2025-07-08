cmake_minimum_required(VERSION 3.16.0)

# Configuration pour PlatformIO + ESP-IDF
if(DEFINED ENV{IDF_PATH})
    # Utiliser IDF_PATH si défini (build manuel ESP-IDF)
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
else()
    # Utiliser le chemin PlatformIO par défaut
    set(IDF_PATH $ENV{HOME}/.platformio/packages/framework-espidf)
    include(${IDF_PATH}/tools/cmake/project.cmake)
endif()

project(chiro_logger)