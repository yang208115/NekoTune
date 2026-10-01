file(GLOB FRONTEND_CONTROLLERS CONFIGURE_DEPENDS src/controllers/*.cpp src/controllers/*.h src/models/*.cpp src/models/*.h)
add_library(nekotune_frontend STATIC src/ipc_client.cpp src/ipc_client.h src/i18n.cpp src/i18n.h ${FRONTEND_CONTROLLERS})
target_include_directories(nekotune_frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(nekotune_frontend PUBLIC Qt6::Core Qt6::Network)
add_executable(nekotune-qt-qml
    src/main.cpp
    resources.qrc
)

target_link_libraries(nekotune-qt-qml
    PRIVATE
        nekotune_frontend
        Qt6::Core
        Qt6::Network
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
)

# Unified desktop binary.  It embeds the backend and starts the QML client
# from the same process, while retaining the standalone targets above for
# headless/server deployments and replaceable clients.
add_executable(nekotune
    src/main.cpp
    resources.qrc
)

target_compile_definitions(nekotune
    PRIVATE
        NEKOTUNE_EMBED_BACKEND
)

target_include_directories(nekotune
    PRIVATE
        ${CMAKE_SOURCE_DIR}/backend
)

target_link_libraries(nekotune
    PRIVATE
        nekotune_frontend
        nekotune_core
        Qt6::Core
        Qt6::Network
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
)

set_target_properties(nekotune PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
