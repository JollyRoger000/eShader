cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(eShader)

#include($ENV{IDF_PATH}/tools/cmake/project.cmake)
#set(PROJECT_VER "1.0.0")
#project(eShader_${PROJECT_VER})

#target_add_binary_data(eShader_${PROJECT_VER}.elf "main/wqtt.pem" TEXT)
#target_add_binary_data(eShader_${PROJECT_VER}.elf "main/owmap_org.pem" TEXT)
#target_add_binary_data(eShader_${PROJECT_VER}.elf "main/api_telegram_org.pem" TEXT)
#target_add_binary_data(eShader_${PROJECT_VER}.elf "main/gts_root_r4.pem" TEXT)
#target_add_binary_data(eShader_${PROJECT_VER}.elf "main/sunrise_sunset_org.pem" TEXT)

target_add_binary_data(eShader.elf "main/wqtt.pem" TEXT)
target_add_binary_data(eShader.elf "main/owmap_org.pem" TEXT)
target_add_binary_data(eShader.elf "main/api_telegram_org.pem" TEXT)
target_add_binary_data(eShader.elf "main/gts_root_r4.pem" TEXT)
target_add_binary_data(eShader.elf "main/sunrise_sunset_org.pem" TEXT)
