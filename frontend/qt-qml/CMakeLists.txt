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
