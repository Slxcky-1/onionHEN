###################################################################################################
# Temporary beta trial gate (build-time seal + local anti-rollback state)
# (self-contained; remove with -DONION_ENABLE_BETA_TRIAL=OFF)
#
# Optional at configure/build time (auto-filled when omitted):
#   ONION_BETA_NOT_BEFORE      Unix seconds (default: configure time)
#   ONION_BETA_NOT_AFTER       Unix seconds (default: not_before + 30 days)
#   ONION_BETA_BUILD_ID        short id string (default: beta-YYYYMMDD)
#   ONION_BETA_STATE_KEY_HEX   64 hex chars for trial.state HMAC (default: random, cached)
#   ONION_BETA_SKEW_SEC        clock skew tolerance (default: 86400)
###################################################################################################

cmake_minimum_required(VERSION 3.20)

set(basename "onion_trial")
project(${basename} C)

onion_module_prologue(C_STANDARD 11)

set(D_CWD "${PROJECT_SOURCE_DIR}")
set(CHEAT_SUPPORT_DIR "${ONIONHEN_THIRD_PARTY_DIR}/cheat_support")

# --- resolve seal parameters (CLI/env > cache > auto) ---
macro(_onion_beta_from_env _var)
	if(NOT DEFINED ${_var} OR "${${_var}}" STREQUAL "")
		if(DEFINED ENV{${_var}} AND NOT "$ENV{${_var}}" STREQUAL "")
			set(${_var} "$ENV{${_var}}")
		endif()
	endif()
endmacro()

_onion_beta_from_env(ONION_BETA_NOT_BEFORE)
_onion_beta_from_env(ONION_BETA_NOT_AFTER)
_onion_beta_from_env(ONION_BETA_BUILD_ID)
_onion_beta_from_env(ONION_BETA_STATE_KEY_HEX)
_onion_beta_from_env(ONION_BETA_SKEW_SEC)

if(NOT DEFINED ONION_BETA_NOT_BEFORE OR ONION_BETA_NOT_BEFORE STREQUAL "")
	string(TIMESTAMP ONION_BETA_NOT_BEFORE "%s" UTC)
endif()

if(NOT DEFINED ONION_BETA_NOT_AFTER OR ONION_BETA_NOT_AFTER STREQUAL "")
	math(EXPR ONION_BETA_NOT_AFTER "${ONION_BETA_NOT_BEFORE} + 2592000") # +30d
endif()

if(NOT DEFINED ONION_BETA_BUILD_ID OR ONION_BETA_BUILD_ID STREQUAL "")
	string(TIMESTAMP _onion_beta_date "%Y%m%d" UTC)
	set(ONION_BETA_BUILD_ID "beta-${_onion_beta_date}")
	unset(_onion_beta_date)
endif()

if(NOT DEFINED ONION_BETA_STATE_KEY_HEX OR ONION_BETA_STATE_KEY_HEX STREQUAL "")
	if(DEFINED ONION_BETA_STATE_KEY_HEX_CACHED AND NOT ONION_BETA_STATE_KEY_HEX_CACHED STREQUAL "")
		set(ONION_BETA_STATE_KEY_HEX "${ONION_BETA_STATE_KEY_HEX_CACHED}")
	else()
		string(RANDOM LENGTH 64 ALPHABET "0123456789abcdef" ONION_BETA_STATE_KEY_HEX)
	endif()
endif()

if(NOT DEFINED ONION_BETA_SKEW_SEC OR ONION_BETA_SKEW_SEC STREQUAL "")
	set(ONION_BETA_SKEW_SEC 86400)
endif()

string(LENGTH "${ONION_BETA_STATE_KEY_HEX}" _onion_sk_len)
if(NOT _onion_sk_len EQUAL 64)
	message(FATAL_ERROR
		"ONION_BETA_STATE_KEY_HEX must be exactly 64 hex characters (got ${_onion_sk_len})")
endif()
if(NOT ONION_BETA_STATE_KEY_HEX MATCHES "^[0-9A-Fa-f]+$")
	message(FATAL_ERROR "ONION_BETA_STATE_KEY_HEX must be hexadecimal")
endif()
unset(_onion_sk_len)

string(LENGTH "${ONION_BETA_BUILD_ID}" _onion_bid_len)
if(_onion_bid_len EQUAL 0 OR _onion_bid_len GREATER 31)
	message(FATAL_ERROR
		"ONION_BETA_BUILD_ID must be 1..31 characters (got ${_onion_bid_len})")
endif()
unset(_onion_bid_len)

# Persist so rebuilds in the same build dir keep the same campaign/key.
set(ONION_BETA_NOT_BEFORE "${ONION_BETA_NOT_BEFORE}" CACHE STRING "Beta trial not_before (unix)" FORCE)
set(ONION_BETA_NOT_AFTER "${ONION_BETA_NOT_AFTER}" CACHE STRING "Beta trial not_after (unix)" FORCE)
set(ONION_BETA_BUILD_ID "${ONION_BETA_BUILD_ID}" CACHE STRING "Beta trial build id" FORCE)
set(ONION_BETA_STATE_KEY_HEX "${ONION_BETA_STATE_KEY_HEX}" CACHE STRING "Beta trial HMAC key (64 hex)" FORCE)
set(ONION_BETA_STATE_KEY_HEX_CACHED "${ONION_BETA_STATE_KEY_HEX}" CACHE INTERNAL "Cached beta state key")
set(ONION_BETA_SKEW_SEC "${ONION_BETA_SKEW_SEC}" CACHE STRING "Beta trial clock skew seconds" FORCE)

add_library(${PROJECT_NAME} STATIC
  source/gate.c
  source/eval.c
  source/device.c
  source/crypto_util.c
  "${CHEAT_SUPPORT_DIR}/sha256.c"
)
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")

set(CMAKE_C_FLAGS "${ONION_PS5_TARGET_FLAGS_MARCH} -fPIC -fstack-protector-all -O2 -Wall -Wextra -Werror")

target_compile_definitions(${PROJECT_NAME} PRIVATE
  ONION_BETA_NOT_BEFORE=${ONION_BETA_NOT_BEFORE}LL
  ONION_BETA_NOT_AFTER=${ONION_BETA_NOT_AFTER}LL
  ONION_BETA_BUILD_ID="${ONION_BETA_BUILD_ID}"
  ONION_BETA_STATE_KEY_HEX="${ONION_BETA_STATE_KEY_HEX}"
  ONION_BETA_SKEW_SEC=${ONION_BETA_SKEW_SEC}LL
)

target_include_directories(${PROJECT_NAME}
  PUBLIC
    "${D_CWD}/include"
  PRIVATE
    "${D_CWD}/source"
    "${CHEAT_SUPPORT_DIR}"
    "${PROJECT_ROOT}/libonion_platform/include"
    "${PROJECT_ROOT}/include"
)

target_link_libraries(${PROJECT_NAME} PUBLIC onion_platform)

message(STATUS "onion_trial: beta seal build_id=${ONION_BETA_BUILD_ID}")
message(STATUS "onion_trial: not_before=${ONION_BETA_NOT_BEFORE} not_after=${ONION_BETA_NOT_AFTER} skew=${ONION_BETA_SKEW_SEC}")
