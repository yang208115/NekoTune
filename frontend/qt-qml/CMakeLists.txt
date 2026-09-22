add_executable(nekotune-qt-qml
    src/i18n.cpp
    src/i18n.h
    src/ipc_client.cpp
    src/ipc_client.h
    src/main.cpp
    resources.qrc
)

target_link_libraries(nekotune-qt-qml
    PRIVATE
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
    src/i18n.cpp
    src/i18n.h
    src/ipc_client.cpp
    src/ipc_client.h
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
