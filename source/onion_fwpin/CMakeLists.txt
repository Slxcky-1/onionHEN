cmake_minimum_required(VERSION 3.20)

set(basename "onion_fwpin")
project(${basename} C)

set(D_CWD "${CMAKE_CURRENT_SOURCE_DIR}")
onion_output_dirs()

onion_module_prologue(C_STANDARD 17)

add_executable(${PROJECT_NAME} source/main.c)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "onion_fwpin.elf")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS} ")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-keyword-macro -fPIC -fPIE -march=znver2 -Wall -Werror -fstack-protector-all -O2")

target_link_directories(${PROJECT_NAME} PUBLIC "${ONIONHEN_PS5_STUB_DIR}")
target_link_libraries(${PROJECT_NAME} PUBLIC kernel_sys)

onion_strip_debug(${PROJECT_NAME})
