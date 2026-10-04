set(NEKOTUNE_NODE_VERSION "24.21.0")
if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(FATAL_ERROR "Bundled extension runtime requires x86_64")
endif()
if(WIN32)
    set(NEKOTUNE_NODE_PLATFORM "win-x64")
    set(NEKOTUNE_NODE_EXTENSION "zip")
    set(NEKOTUNE_NODE_EXECUTABLE "node.exe")
    set(NEKOTUNE_NODE_RELATIVE_BINARY "node.exe")
    set(NEKOTUNE_NODE_SHA256 "158f7685b44de51f6c0df1d153526cbcd3e1bc739a8dfc607721cef75de9e541")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NEKOTUNE_NODE_PLATFORM "linux-x64")
    set(NEKOTUNE_NODE_EXTENSION "tar.xz")
    set(NEKOTUNE_NODE_EXECUTABLE "node")
    set(NEKOTUNE_NODE_RELATIVE_BINARY "bin/node")
    set(NEKOTUNE_NODE_SHA256 "fd8e59d5a511510f6a298afb548f18c7d2b1be404d8b4a27d94fbe49f56cb2d6")
else()
    message(FATAL_ERROR "Bundled extension runtime supports Linux and Windows x86_64")
endif()
set(NEKOTUNE_NODE_ARCHIVE "" CACHE FILEPATH "Offline Node ${NEKOTUNE_NODE_PLATFORM} archive")
set(NEKOTUNE_NODE_DIRECTORY "node-v${NEKOTUNE_NODE_VERSION}-${NEKOTUNE_NODE_PLATFORM}")
set(NEKOTUNE_RUNTIME_DIR "${CMAKE_BINARY_DIR}/extensions-runtime")
file(MAKE_DIRECTORY "${NEKOTUNE_RUNTIME_DIR}")
if(NOT NEKOTUNE_NODE_ARCHIVE)
    set(NEKOTUNE_NODE_ARCHIVE "${NEKOTUNE_RUNTIME_DIR}/${NEKOTUNE_NODE_DIRECTORY}.${NEKOTUNE_NODE_EXTENSION}")
    if(NOT EXISTS "${NEKOTUNE_NODE_ARCHIVE}")
        file(DOWNLOAD "https://nodejs.org/dist/v${NEKOTUNE_NODE_VERSION}/${NEKOTUNE_NODE_DIRECTORY}.${NEKOTUNE_NODE_EXTENSION}"
             "${NEKOTUNE_NODE_ARCHIVE}" EXPECTED_HASH "SHA256=${NEKOTUNE_NODE_SHA256}" TLS_VERIFY ON STATUS NODE_DOWNLOAD TIMEOUT 180)
        list(GET NODE_DOWNLOAD 0 NODE_DOWNLOAD_CODE)
        if(NOT NODE_DOWNLOAD_CODE EQUAL 0)
            file(REMOVE "${NEKOTUNE_NODE_ARCHIVE}")
            message(FATAL_ERROR "Node runtime download failed: ${NODE_DOWNLOAD}. Supply NEKOTUNE_NODE_ARCHIVE for offline builds.")
        endif()
    endif()
endif()
file(SHA256 "${NEKOTUNE_NODE_ARCHIVE}" NODE_ARCHIVE_HASH)
if(NOT NODE_ARCHIVE_HASH STREQUAL NEKOTUNE_NODE_SHA256)
    message(FATAL_ERROR "Bundled Node archive SHA256 mismatch")
endif()
if(NOT EXISTS "${NEKOTUNE_RUNTIME_DIR}/${NEKOTUNE_NODE_DIRECTORY}/${NEKOTUNE_NODE_RELATIVE_BINARY}")
    file(ARCHIVE_EXTRACT INPUT "${NEKOTUNE_NODE_ARCHIVE}" DESTINATION "${NEKOTUNE_RUNTIME_DIR}")
endif()
set(NEKOTUNE_NODE_BINARY "${NEKOTUNE_RUNTIME_DIR}/${NEKOTUNE_NODE_DIRECTORY}/${NEKOTUNE_NODE_RELATIVE_BINARY}")
if(WIN32)
    find_program(PNPM_EXECUTABLE NAMES pnpm.cmd pnpm.exe pnpm REQUIRED)
    set(NEKOTUNE_PNPM_COMMAND cmd /c "${PNPM_EXECUTABLE}")
else()
    find_program(PNPM_EXECUTABLE pnpm REQUIRED)
    set(NEKOTUNE_PNPM_COMMAND "${PNPM_EXECUTABLE}")
endif()
file(GLOB_RECURSE EXTENSION_RUNTIME_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/extensions/runtime/*.mjs" "${CMAKE_SOURCE_DIR}/extensions/sdk/*.ts" "${CMAKE_SOURCE_DIR}/extensions/examples/*.ts")
add_custom_command(OUTPUT "${CMAKE_SOURCE_DIR}/extensions/dist/supervisor.cjs" "${CMAKE_SOURCE_DIR}/extensions/dist/worker.cjs"
    COMMAND "${CMAKE_COMMAND}" -E env CI=true ${NEKOTUNE_PNPM_COMMAND} install --frozen-lockfile --store-dir "${CMAKE_BINARY_DIR}/pnpm-store"
    COMMAND ${NEKOTUNE_PNPM_COMMAND} build
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/extensions"
    DEPENDS ${EXTENSION_RUNTIME_SOURCES} "${CMAKE_SOURCE_DIR}/extensions/tools/build.mjs" "${CMAKE_SOURCE_DIR}/extensions/tools/cli.mjs" "${CMAKE_SOURCE_DIR}/extensions/package.json" "${CMAKE_SOURCE_DIR}/extensions/pnpm-lock.yaml" "${CMAKE_SOURCE_DIR}/extensions/pnpm-workspace.yaml"
    VERBATIM)
add_custom_target(nekotune_extension_runtime ALL DEPENDS "${CMAKE_SOURCE_DIR}/extensions/dist/supervisor.cjs" "${CMAKE_SOURCE_DIR}/extensions/dist/worker.cjs")
include(GNUInstallDirs)
install(PROGRAMS "${NEKOTUNE_NODE_BINARY}" DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/nekotune" RENAME "${NEKOTUNE_NODE_EXECUTABLE}")
install(FILES "${NEKOTUNE_RUNTIME_DIR}/${NEKOTUNE_NODE_DIRECTORY}/LICENSE" DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/nekotune" RENAME NODE-LICENSE)
# Only ship the host runtime; stale archives from previous builds must not be distributed.
install(FILES
    "${CMAKE_SOURCE_DIR}/extensions/dist/supervisor.cjs"
    "${CMAKE_SOURCE_DIR}/extensions/dist/supervisor.cjs.map"
    "${CMAKE_SOURCE_DIR}/extensions/dist/worker.cjs"
    "${CMAKE_SOURCE_DIR}/extensions/dist/worker.cjs.map"
    "${CMAKE_SOURCE_DIR}/extensions/dist/index.d.ts"
    DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/nekotune")
