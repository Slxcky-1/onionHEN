###################################################################################################
# Optional ELF self-integrity (Ed25519 over executable PT_LOAD).
# Enable: -DONION_ENABLE_ELF_PROTECTION=ON -DONION_ELF_SIGNING_PUBLIC_KEY_HEX=<64hex>
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_integrity")
project(${basename} C)

onion_module_prologue(C_STANDARD 11)

set(D_CWD "${PROJECT_SOURCE_DIR}")

# Prefer CLI, then env.
if(NOT DEFINED ONION_ELF_SIGNING_PUBLIC_KEY_HEX OR ONION_ELF_SIGNING_PUBLIC_KEY_HEX STREQUAL "")
	if(DEFINED ENV{ONION_ELF_SIGNING_PUBLIC_KEY_HEX}
		AND NOT "$ENV{ONION_ELF_SIGNING_PUBLIC_KEY_HEX}" STREQUAL "")
		set(ONION_ELF_SIGNING_PUBLIC_KEY_HEX "$ENV{ONION_ELF_SIGNING_PUBLIC_KEY_HEX}")
	endif()
endif()

if(ONION_ENABLE_ELF_PROTECTION)
	if(NOT DEFINED ONION_ELF_SIGNING_PUBLIC_KEY_HEX OR ONION_ELF_SIGNING_PUBLIC_KEY_HEX STREQUAL "")
		message(FATAL_ERROR
			"ONION_ELF_SIGNING_PUBLIC_KEY_HEX (64 hex) is required when "
			"ONION_ENABLE_ELF_PROTECTION=ON")
	endif()
	string(LENGTH "${ONION_ELF_SIGNING_PUBLIC_KEY_HEX}" _onion_pk_len)
	if(NOT _onion_pk_len EQUAL 64)
		message(FATAL_ERROR
			"ONION_ELF_SIGNING_PUBLIC_KEY_HEX must be exactly 64 hex characters "
			"(got ${_onion_pk_len})")
	endif()
	if(NOT ONION_ELF_SIGNING_PUBLIC_KEY_HEX MATCHES "^[0-9A-Fa-f]+$")
		message(FATAL_ERROR "ONION_ELF_SIGNING_PUBLIC_KEY_HEX must be hexadecimal")
	endif()
	unset(_onion_pk_len)
endif()

set(ED25519_VERIFY_SRCS
	vendor/ed25519/verify.c
	vendor/ed25519/ge.c
	vendor/ed25519/fe.c
	vendor/ed25519/sc.c
	vendor/ed25519/sha512.c
)

add_library(${PROJECT_NAME} STATIC
	source/self_integrity.c
	source/image_verify.c
	${ED25519_VERIFY_SRCS}
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror")

if(ONION_ENABLE_ELF_PROTECTION)
	target_compile_definitions(${PROJECT_NAME} PRIVATE
		ONION_ENABLE_ELF_PROTECTION=1
		ONION_ELF_SIGNING_PUBLIC_KEY_HEX="${ONION_ELF_SIGNING_PUBLIC_KEY_HEX}"
	)
	message(STATUS "onion_integrity: ELF protection ON (pubkey length 64)")
else()
	message(STATUS "onion_integrity: ELF protection OFF (verify is no-op)")
endif()

target_include_directories(${PROJECT_NAME}
	PUBLIC
		"${D_CWD}/include"
	PRIVATE
		"${D_CWD}/vendor/ed25519"
)

# Always available to link; gate is compile-time inside the .c file.
