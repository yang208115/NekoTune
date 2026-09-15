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
