add_library(nekotune_domain STATIC
 domain/player_queue.cpp domain/playback_backend.h domain/file_inspector.h domain/kugou_backend.h
 domain/lyrics_types.cpp domain/lrc_parser.cpp domain/krc_parser.cpp domain/lyrics_provider.h
)
target_include_directories(nekotune_domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nekotune_domain PUBLIC Qt6::Core ZLIB::ZLIB)

add_library(nekotune_storage STATIC
 storage/database_session.cpp storage/song_repository.cpp storage/queue_repository.cpp
 storage/playlist_repository.cpp storage/tag_repository.cpp
)
target_link_libraries(nekotune_storage PUBLIC nekotune_domain Qt6::Sql)

add_library(nekotune_providers STATIC lyrics/network_diagnostics.cpp lyrics/lrclib_provider.cpp lyrics/kugou_provider.cpp)
target_link_libraries(nekotune_providers PUBLIC nekotune_domain Qt6::Network)

add_library(nekotune_infrastructure STATIC
 infrastructure/qt_playback_backend.cpp infrastructure/import_executor.cpp
 infrastructure/lyrics_cache.cpp infrastructure/lyrics_storage.cpp
 kugou/kugou_music_service.cpp kugou/kugou_account_session.cpp kugou/kugou_api_client.cpp kugou/kugou_download_job.cpp
)
target_link_libraries(nekotune_infrastructure PUBLIC nekotune_domain Qt6::Network Qt6::Gui Qt6::Multimedia)

add_library(nekotune_application STATIC
 core/player_engine.cpp application/library_service.cpp application/queue_service.cpp
 application/playlist_service.cpp application/tag_service.cpp application/collection_service.cpp
 application/lyrics_controller.cpp application/download_service.cpp application/command_scheduler.cpp application/kugou_service.h
 application/lyrics_service.cpp
)
target_link_libraries(nekotune_application PUBLIC nekotune_domain)

add_library(nekotune_ipc STATIC
 ipc/serialization.cpp ipc/lyrics_serialization.cpp ipc/kugou_serialization.cpp ipc/ipc_router.cpp ipc/ipc_server.cpp
 ipc/api_context.cpp ipc/player_api.cpp ipc/library_api.cpp ipc/lyrics_api.cpp ipc/kugou_api.cpp
)
target_link_libraries(nekotune_ipc PUBLIC nekotune_application Qt6::Network)

add_library(nekotune_core STATIC runtime/backend_session.cpp runtime/backend_runtime.cpp)
target_link_libraries(nekotune_core PUBLIC nekotune_ipc nekotune_storage nekotune_providers nekotune_infrastructure)
add_executable(nekotune-backend main.cpp)
target_link_libraries(nekotune-backend PRIVATE nekotune_core)
