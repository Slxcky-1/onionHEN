###################################################################################################
# Temporary beta Ed25519 certificate activation
# (self-contained; remove with -DONION_ENABLE_BETA_ACTIVATION=OFF)
#
# Required at configure/build time:
#   export ONION_ACTIVATION_PUBLIC_KEY_HEX=<64 lowercase/upper hex chars>
# or:
#   cmake -DONION_ACTIVATION_PUBLIC_KEY_HEX=...
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_activation")
project(${basename} C)

onion_module_prologue(C_STANDARD 11)

set(D_CWD "${PROJECT_SOURCE_DIR}")
set(CHEAT_SUPPORT_DIR "${ONIONHEN_THIRD_PARTY_DIR}/cheat_support")

# Public key: prefer cache/CLI -D, else environment variable. No baked-in default.
if(NOT DEFINED ONION_ACTIVATION_PUBLIC_KEY_HEX OR ONION_ACTIVATION_PUBLIC_KEY_HEX STREQUAL "")
	if(DEFINED ENV{ONION_ACTIVATION_PUBLIC_KEY_HEX}
		AND NOT "$ENV{ONION_ACTIVATION_PUBLIC_KEY_HEX}" STREQUAL "")
		set(ONION_ACTIVATION_PUBLIC_KEY_HEX "$ENV{ONION_ACTIVATION_PUBLIC_KEY_HEX}")
	endif()
endif()

if(NOT DEFINED ONION_ACTIVATION_PUBLIC_KEY_HEX OR ONION_ACTIVATION_PUBLIC_KEY_HEX STREQUAL "")
	message(FATAL_ERROR
		"ONION_ACTIVATION_PUBLIC_KEY_HEX is required when beta activation is enabled.\n"
		"  export ONION_ACTIVATION_PUBLIC_KEY_HEX=<64 hex chars>\n"
		"  # or: cmake -DONION_ACTIVATION_PUBLIC_KEY_HEX=...")
endif()

string(LENGTH "${ONION_ACTIVATION_PUBLIC_KEY_HEX}" _onion_pk_len)
if(NOT _onion_pk_len EQUAL 64)
	message(FATAL_ERROR
		"ONION_ACTIVATION_PUBLIC_KEY_HEX must be exactly 64 hex characters "
		"(got length ${_onion_pk_len})")
endif()
if(NOT ONION_ACTIVATION_PUBLIC_KEY_HEX MATCHES "^[0-9A-Fa-f]+$")
	message(FATAL_ERROR
		"ONION_ACTIVATION_PUBLIC_KEY_HEX must be hexadecimal")
endif()
unset(_onion_pk_len)

# Keep in cache so rebuilds without re-export still work in the same build dir.
set(ONION_ACTIVATION_PUBLIC_KEY_HEX "${ONION_ACTIVATION_PUBLIC_KEY_HEX}"
	CACHE STRING "Ed25519 public key (64 hex) for beta certificate activation" FORCE)

# Verify-only ed25519 (no seed / sign / keygen on device).
set(ED25519_SRCS
  vendor/ed25519/verify.c
  vendor/ed25519/ge.c
  vendor/ed25519/fe.c
  vendor/ed25519/sc.c
  vendor/ed25519/sha512.c
)

add_library(${PROJECT_NAME} STATIC
  source/activation.c
  source/license.c
  source/device.c
  source/crypto_util.c
  vendor/cjson/cJSON.c
  ${ED25519_SRCS}
  "${CHEAT_SUPPORT_DIR}/sha256.c"
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror")

target_compile_definitions(${PROJECT_NAME} PRIVATE
  ONION_ACTIVATION_PUBLIC_KEY_HEX="${ONION_ACTIVATION_PUBLIC_KEY_HEX}"
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
    "${D_CWD}/include"
  PRIVATE
    "${D_CWD}/source"
    "${D_CWD}/vendor/cjson"
    "${D_CWD}/vendor/ed25519"
    "${CHEAT_SUPPORT_DIR}"
    "${PROJECT_ROOT}/libonion_platform/include"
    "${PROJECT_ROOT}/include"
)

target_link_libraries(${PROJECT_NAME} PUBLIC onion_platform)

message(STATUS "onion_activation: Ed25519 public key from env/CLI (length 64)")
