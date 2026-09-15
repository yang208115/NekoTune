set(NEKOTUNE_BACKEND_SOURCES
    core/player_engine.cpp
    core/player_queue.cpp
    ipc/ipc_server.cpp
    storage/song_store.cpp
)

set(NEKOTUNE_BACKEND_HEADERS
    core/player_engine.h
    core/player_queue.h
    core/player_state.h
    ipc/ipc_server.h
    storage/song_metadata.h
    storage/song_store.h
)

add_library(nekotune_core STATIC
    ${NEKOTUNE_BACKEND_SOURCES}
    ${NEKOTUNE_BACKEND_HEADERS}
)

target_include_directories(nekotune_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(nekotune_core
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Sql
        Qt6::Multimedia
)

add_executable(nekotune-backend
    main.cpp
)

target_link_libraries(nekotune-backend
    PRIVATE
        nekotune_core
)
