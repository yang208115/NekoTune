add_executable(nekotune_player_queue_test
    player_queue_test.cpp
)

target_link_libraries(nekotune_player_queue_test
    PRIVATE
        nekotune_core
        Qt6::Test
)

add_test(NAME nekotune_player_queue_test COMMAND nekotune_player_queue_test)

add_executable(nekotune_song_store_test
    song_store_test.cpp
)

target_link_libraries(nekotune_song_store_test
    PRIVATE
        nekotune_core
        Qt6::Test
)

add_test(NAME nekotune_song_store_test COMMAND nekotune_song_store_test)

add_executable(nekotune_lyrics_test lyrics_test.cpp)
target_link_libraries(nekotune_lyrics_test PRIVATE nekotune_core Qt6::Test)
add_test(NAME nekotune_lyrics_test COMMAND nekotune_lyrics_test)

find_package(Qt6 6.6 REQUIRED COMPONENTS QuickTest)

add_executable(nekotune_slider_quick_test slider_quick_test.cpp)
target_link_libraries(nekotune_slider_quick_test PRIVATE Qt6::QuickTest Qt6::QuickControls2)
target_compile_definitions(nekotune_slider_quick_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
add_test(NAME nekotune_slider_quick_test COMMAND nekotune_slider_quick_test)
set_tests_properties(nekotune_slider_quick_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion"
)
