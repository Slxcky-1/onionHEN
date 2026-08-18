# Optional libgit2 for util cheat-repo sync.
#
# When third_party/libgit2 is present (submodule), build a slim static lib
# without SSH or the built-in HTTPS stack. The console supplies HTTPS through
# IHttpTransport. Host tests do not require this directory.

include_guard(GLOBAL)

set(ONION_LIBGIT2_DIR "${ONIONHEN_THIRD_PARTY_DIR}/libgit2")
set(ONION_HAVE_LIBGIT2 OFF)

if(EXISTS "${ONION_LIBGIT2_DIR}/CMakeLists.txt")
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
	set(BUILD_CLI OFF CACHE BOOL "" FORCE)
	set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(BUILD_FUZZERS OFF CACHE BOOL "" FORCE)
	set(USE_SSH OFF CACHE BOOL "" FORCE)
	set(USE_HTTPS OFF CACHE BOOL "" FORCE)
	set(USE_NTLMCLIENT OFF CACHE BOOL "" FORCE)
	set(USE_BUNDLED_ZLIB ON CACHE BOOL "" FORCE)
	set(USE_THREADS ON CACHE BOOL "" FORCE)
	set(REGEX_BACKEND "builtin" CACHE STRING "" FORCE)
	set(SONAME OFF CACHE BOOL "" FORCE)
	set(LIBGIT2_NO_FEATURES_H ON CACHE BOOL "" FORCE)

	# add_subdirectory pulls libgit2's OBJECT target named "util".
	# OnionHEN's daemon ELF target is therefore onion_util (OUTPUT_NAME util.elf).
	add_subdirectory("${ONION_LIBGIT2_DIR}" "${CMAKE_BINARY_DIR}/libgit2"
		EXCLUDE_FROM_ALL)
	set(ONION_HAVE_LIBGIT2 ON)
	message(STATUS "OnionHEN: libgit2 enabled from ${ONION_LIBGIT2_DIR}")
else()
	message(STATUS "OnionHEN: libgit2 not vendored; cheat git clone is a stub")
endif()
