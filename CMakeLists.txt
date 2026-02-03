cmake_minimum_required(VERSION 3.16.0)

if(DEFINED ENV{IDF_PATH})
    include($ENV{IDF_PATH}/tools/cmake/project.cmake)
else()
    set(IDF_PATH $ENV{HOME}/.platformio/packages/framework-espidf)
    include(${IDF_PATH}/tools/cmake/project.cmake)
endif()

project(chiro_logger)