cmake_minimum_required(VERSION 3.12)

project(gameboy VERSION 0.5 DESCRIPTION "GameboyDMG Emulator written in C." LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(UNIX AND NOT APPLE)  # Linux
	set(SDL_VIDEO ON CACHE BOOL "" FORCE)
    set(SDL_VIDEO_X11 ON CACHE BOOL "" FORCE)
    set(SDL_VIDEO_WAYLAND ON CACHE BOOL "" FORCE)
    set(SDL_WAYLAND_QT_TOUCH ON CACHE BOOL OFF FORCE)
    set(SDL_WAYLAND_LIBDECOR ON CACHE BOOL OFF FORCE)
    set(SDL_AUDIO ON CACHE BOOL "" FORCE)
    set(SDL_ALSA ON CACHE BOOL "" FORCE)
    set(SDL_PULSEAUDIO ON CACHE BOOL "" FORCE)
    set(SDL_PIPEWIRE ON CACHE BOOL "" FORCE)
endif()

set(SDL2TTF_VENDORED ON)
add_executable(gameboy  
				 src/gui/renderer.c 
				 src/gui/gui.c 
				 src/hardware/cpu.c 
				 src/hardware/memory.c 
				 src/hardware/ppu.c 
				 src/hardware/timer.c 
				 src/hardware/joypad.c 
				 src/hardware/device.c 
				 src/hardware/cartridge.c 
				 src/hardware/debugger.c 
				 src/gameboy.c
				 external/SDL_FontCache.c
				 external/microui.c
			) 
#find_package(OpenGL REQUIRED COMPONENTS OpenGL)
#add_dependencies(gameboy OpenGL::OpenGL)

add_subdirectory(external/SDL-release-2.32.8 EXCLUDE_FROM_ALL)
add_subdirectory(external/SDL_ttf-release-2.24.0 EXCLUDE_FROM_ALL)

target_link_libraries(gameboy PRIVATE SDL2 SDL2_ttf)

if(WIN32)
	target_link_libraries(gameboy PRIVATE SDL2main)
endif()

target_include_directories(gameboy PRIVATE external/SDL-release-2.23.8/include external/SDL_ttf-release-2.24.0)


file(COPY ${CMAKE_CURRENT_LIST_DIR}/bootroms DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_LIST_DIR}/src/gui/fonts DESTINATION ${CMAKE_BINARY_DIR})
