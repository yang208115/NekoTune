set(NEKOTUNE_BACKEND_SOURCES
    core/player_engine.cpp
    core/player_queue.cpp
    ipc/ipc_server.cpp
    ipc/ipc_router.cpp
    storage/asr_settings.cpp
    lyrics/network_diagnostics.cpp
    lyrics/aliyun_asr.cpp
    storage/song_store.cpp
    storage/playlist_store.cpp
    lyrics/lyrics_types.cpp
    lyrics/asr_parser.cpp
    lyrics/lrc_parser.cpp
    lyrics/lyrics_cache.cpp
    lyrics/lrclib_provider.cpp
    lyrics/lyrics_service.cpp
)

set(NEKOTUNE_BACKEND_HEADERS
    core/player_engine.h
    core/player_queue.h
    core/player_state.h
    ipc/ipc_server.h
    ipc/ipc_router.h
    storage/song_metadata.h
    storage/asr_settings.h
    lyrics/network_diagnostics.h
    lyrics/aliyun_asr.h
    storage/song_store.h
    lyrics/lyrics_types.h
    lyrics/asr_parser.h
    lyrics/lrc_parser.h
    lyrics/lyrics_cache.h
    lyrics/lyrics_provider.h
    lyrics/lrclib_provider.h
    lyrics/lyrics_service.h
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
