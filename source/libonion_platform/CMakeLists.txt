###################################################################################################
# OnionHEN platform leaves: fs / log / net / notify
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_platform")
project(${basename} C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(D_CWD "${PROJECT_SOURCE_DIR}")

include_directories(SYSTEM "${PS5_PAYLOAD_SDK}")
include_directories(SYSTEM "${PS5_PAYLOAD_SDK}/include")

add_library(${PROJECT_NAME} STATIC
  source/fs.c
  source/log.c
  source/net.c
  source/notify.c
  source/notify_i18n.c
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

message("========== build: ${PROJECT_NAME} ==========")

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(I18N_DIR "${D_CWD}/../i18n")
set(I18N_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(NOTIFY_I18N_CATALOG "${I18N_GENERATED_DIR}/notify_i18n_catalog.inc")
add_custom_command(
  OUTPUT "${NOTIFY_I18N_CATALOG}"
  COMMAND "${Python3_EXECUTABLE}" "${I18N_DIR}/generate_catalog.py"
    --zh-cn "${I18N_DIR}/zh-CN.json"
    --en-us "${I18N_DIR}/en-US.json"
    --catalog notifications
    --output "${NOTIFY_I18N_CATALOG}"
  DEPENDS
    "${I18N_DIR}/generate_catalog.py"
    "${I18N_DIR}/zh-CN.json"
    "${I18N_DIR}/en-US.json"
  COMMENT "Generating notification i18n catalog"
  VERBATIM
)
add_custom_target(onion_platform_i18n_catalog DEPENDS "${NOTIFY_I18N_CATALOG}")
add_dependencies(${PROJECT_NAME} onion_platform_i18n_catalog)

set(CMAKE_C_FLAGS "--target=x86_64-sie-ps5 -march=znver2 -DPPR -DPS5 -DPS5_FW_VERSION=${V_FW} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror")

target_include_directories(${PROJECT_NAME}
  PUBLIC  "${D_CWD}/include"
  PRIVATE "${D_CWD}/../include" "${I18N_GENERATED_DIR}"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
