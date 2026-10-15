if(NOT DEFINED REPO_ROOT OR NOT DEFINED OUTPUT_FILE)
  message(FATAL_ERROR "REPO_ROOT and OUTPUT_FILE are required")
endif()

set(_version "v0.0.14-r7 Unofficial By </ElonTusk>")

string(CONCAT _contents
    "#pragma once\n\n"
    "/* Generated at build time. Do not edit. */\n"
    "#define ONIONHEN_VERSION \"${_version}\"\n"
    "#define ONIONHEN_AUTHOR \"麒麟/0xp0co\"\n")

get_filename_component(_output_dir "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${_output_dir}")

set(_write_header TRUE)
if(EXISTS "${OUTPUT_FILE}")
  file(READ "${OUTPUT_FILE}" _existing_contents)
  if(_existing_contents STREQUAL _contents)
    set(_write_header FALSE)
  endif()
endif()

if(_write_header)
  file(WRITE "${OUTPUT_FILE}" "${_contents}")
endif()

message(STATUS "OnionHEN version: ${_version}")
