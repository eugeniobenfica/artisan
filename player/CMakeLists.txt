# artisan-player: runs a game. The editor starts it for the preview; exported games are this
# program next to the game's data (desktop) or the library SDL's Android activity loads.

if(ANDROID)
    add_library(main SHARED main.cpp)
    set(PLAYER main)
else()
    add_executable(artisan-player WIN32 main.cpp)
    set(PLAYER artisan-player)
    if(MINGW)
        # One file to ship: no MinGW runtime DLLs next to an exported game.
        target_link_options(artisan-player PRIVATE -static)
    endif()
endif()
target_link_libraries(${PLAYER} PRIVATE artisan_engine SDL3::SDL3-static)
