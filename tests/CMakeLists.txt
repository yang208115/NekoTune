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

add_executable(nekotune_music_directory_test music_directory_test.cpp)
target_link_libraries(nekotune_music_directory_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
target_compile_definitions(nekotune_music_directory_test PRIVATE NEKOTUNE_BACKEND_BINARY="$<TARGET_FILE:nekotune-backend>")
add_dependencies(nekotune_music_directory_test nekotune-backend)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME nekotune_music_directory_test COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/with_test_keyring.sh $<TARGET_FILE:nekotune_music_directory_test>)
    set_tests_properties(nekotune_music_directory_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

add_executable(nekotune_lyrics_test lyrics_test.cpp)
target_link_libraries(nekotune_lyrics_test PRIVATE nekotune_core Qt6::Test)
add_test(NAME nekotune_lyrics_test COMMAND nekotune_lyrics_test)

add_executable(nekotune_cover_test cover_test.cpp)
target_link_libraries(nekotune_cover_test PRIVATE nekotune_core Qt6::Test)
add_test(NAME nekotune_cover_test COMMAND nekotune_cover_test)

find_package(Qt6 6.6 REQUIRED COMPONENTS QuickTest)

add_executable(nekotune_slider_quick_test slider_quick_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_slider_quick_test PRIVATE nekotune_frontend Qt6::QuickTest Qt6::QuickControls2)
target_compile_definitions(nekotune_slider_quick_test PRIVATE
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml"
)
add_test(NAME nekotune_slider_quick_test COMMAND nekotune_slider_quick_test)
set_tests_properties(nekotune_slider_quick_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion"
)

add_executable(nekotune_refactor_test refactor_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_refactor_test PRIVATE nekotune_core nekotune_frontend Qt6::Test Qt6::Quick Qt6::QuickControls2)
target_compile_definitions(nekotune_refactor_test PRIVATE NEKOTUNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME nekotune_refactor_test COMMAND nekotune_refactor_test)
set_tests_properties(nekotune_refactor_test PROPERTIES
 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Fusion")
add_test(NAME nekotune_dependency_boundaries
 COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/dependency_boundaries.cmake)

add_executable(nekotune_credential_store_test credential_store_test.cpp)
target_link_libraries(nekotune_credential_store_test PRIVATE nekotune_core Qt6::Test)
add_test(NAME nekotune_credential_store_test COMMAND nekotune_credential_store_test)

add_executable(nekotune_ai_test ai_test.cpp)
target_link_libraries(nekotune_ai_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_test(NAME nekotune_ai_test COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/with_test_keyring.sh $<TARGET_FILE:nekotune_ai_test>)
    set_tests_properties(nekotune_ai_test PROPERTIES SKIP_RETURN_CODE 77)
else()
    add_test(NAME nekotune_ai_test COMMAND nekotune_ai_test)
endif()

# Other platforms opt in to creating temporary entries in their native keychain.
option(NEKOTUNE_TEST_NATIVE_KEYCHAIN "Run tests using the native system keychain" OFF)
add_executable(nekotune_system_credential_store_test system_credential_store_test.cpp)
target_link_libraries(nekotune_system_credential_store_test PRIVATE nekotune_core Qt6::Test)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(nekotune_system_credential_store_test PRIVATE NEKOTUNE_LEGACY_SECRET_SERVICE)
    target_link_libraries(nekotune_system_credential_store_test PRIVATE PkgConfig::LIBSECRET)
    add_test(NAME nekotune_system_credential_store_test COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/with_test_keyring.sh $<TARGET_FILE:nekotune_system_credential_store_test>)
    set_tests_properties(nekotune_system_credential_store_test PROPERTIES SKIP_RETURN_CODE 77)
elseif(NEKOTUNE_TEST_NATIVE_KEYCHAIN)
    add_test(NAME nekotune_system_credential_store_test COMMAND nekotune_system_credential_store_test)
endif()

add_executable(nekotune_audio_duration_test audio_duration_test.cpp)
target_link_libraries(nekotune_audio_duration_test PRIVATE nekotune_core Qt6::Test)
add_test(NAME nekotune_audio_duration_test COMMAND nekotune_audio_duration_test)

add_executable(nekotune_playback_order_test playback_order_test.cpp)
target_link_libraries(nekotune_playback_order_test PRIVATE nekotune_core nekotune_frontend Qt6::Test)
add_test(NAME nekotune_playback_order_test COMMAND nekotune_playback_order_test)

add_executable(nekotune_extensions_test extensions_test.cpp ../frontend/qt-qml/resources.qrc)
target_link_libraries(nekotune_extensions_test PRIVATE nekotune_core nekotune_frontend Qt6::Test Qt6::Quick Qt6::QuickControls2)
target_compile_definitions(nekotune_extensions_test PRIVATE NEKOTUNE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME nekotune_extensions_test COMMAND nekotune_extensions_test)
set_tests_properties(nekotune_extensions_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
add_test(NAME nekotune_extension_sdk_test COMMAND "${NEKOTUNE_NODE_BINARY}" --test tests/packages.test.mjs tests/audio.test.mjs tests/runtime.test.mjs)
set_tests_properties(nekotune_extension_sdk_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/extensions" TIMEOUT 120)
