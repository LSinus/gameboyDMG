cmake_minimum_required(VERSION 3.12)

project(gameboy VERSION 0.5 DESCRIPTION "GameboyDMG Emulator written in C." LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

if(UNIX)
    execute_process(
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/external/SDL_ttf-release-2.24.0/external/download.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/SDL_ttf-release-2.24.0/external
        RESULT_VARIABLE result
    )
#if(NOT result EQUAL 0)
        #        message(FATAL_ERROR "Dependency installation failed")
        #   endif()
elseif(WIN32)
    execute_process(
        COMMAND powershell -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_SOURCE_DIR}/external/SDL_ttf-release-2.24.0/external/Get-GitModules.ps1"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/external/SDL_ttf-release-2.24.0/external
        RESULT_VARIABLE result
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Dependency installation failed")
    endif()
endif()


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
                 src/hardware/serial.c
				 src/hardware/device.c 
				 src/hardware/cartridge.c 
                 src/hardware/debugger.c
				 src/gameboy.c
                 external/SDL_FontCache.c
                 external/microui.c	   

			) 
add_executable(gameboy_debugger
 				 src/gui/renderer.c 
				 src/gui/gui.c 
				 src/gui/gui_hotreload_posix.c 
				 src/hardware/cpu.c 
				 src/hardware/memory.c 
				 src/hardware/ppu.c 
                 src/hardware/serial.c
				 src/hardware/timer.c 
				 src/hardware/joypad.c 
				 src/hardware/device.c 
				 src/hardware/cartridge.c 
				 src/hardware/debugger.c 
				 src/gameboy.c
				 external/SDL_FontCache.c
                 external/microui.c	   
    )

add_library(gui SHARED 
    src/gui/gui.c 
    src/gui/renderer.c 
    external/SDL_FontCache.c
    external/microui.c
)
target_link_libraries(gui PRIVATE SDL2 SDL2_ttf)
target_compile_definitions(gui PRIVATE DEBUGGER_MODE=1)
set_target_properties(gui PROPERTIES PREFIX "lib" SUFFIX ".so")

add_executable(gameboy_debugger_hotreload
				 src/hardware/cpu.c 
				 src/hardware/memory.c 
				 src/hardware/ppu.c 
                 src/hardware/serial.c
				 src/hardware/timer.c 
				 src/hardware/joypad.c 
				 src/hardware/device.c 
				 src/hardware/cartridge.c 
				 src/hardware/debugger.c 
				 src/gameboy.c
                 src/gui/gui_hotreload_posix.c 
    )

set_target_properties(gameboy_debugger_hotreload PROPERTIES ENABLE_EXPORTS TRUE)
if(APPLE)
    target_link_libraries(gameboy_debugger_hotreload PRIVATE "-Wl,-export_dynamic")
endif()

add_subdirectory(external/SDL-release-2.32.8 EXCLUDE_FROM_ALL)
add_subdirectory(external/SDL_ttf-release-2.24.0 EXCLUDE_FROM_ALL)

target_link_libraries(gameboy PRIVATE SDL2 SDL2_ttf)
target_link_libraries(gameboy_debugger PRIVATE SDL2 SDL2_ttf)
target_link_libraries(gameboy_debugger_hotreload PRIVATE SDL2 SDL2_ttf)

target_compile_definitions(gameboy_debugger PRIVATE DEBUGGER_MODE=1)
target_compile_definitions(gameboy_debugger_hotreload PRIVATE DEBUGGER_MODE=1 DEBUGGER_HOTRELOAD=1)

if(WIN32)
	target_link_libraries(gameboy PRIVATE SDL2main)
	target_link_libraries(gameboy_debugger PRIVATE SDL2main)
	target_link_libraries(gameboy_debugger_hotreload PRIVATE SDL2main)
endif()

target_include_directories(gameboy PRIVATE external/SDL-release-2.23.8/include external/SDL_ttf-release-2.24.0)
target_include_directories(gameboy_debugger PRIVATE external/SDL-release-2.23.8/include external/SDL_ttf-release-2.24.0)
target_include_directories(gameboy_debugger_hotreload PRIVATE external/SDL-release-2.23.8/include external/SDL_ttf-release-2.24.0)


file(COPY ${CMAKE_CURRENT_LIST_DIR}/bootroms DESTINATION ${CMAKE_BINARY_DIR})
file(COPY ${CMAKE_CURRENT_LIST_DIR}/src/gui/fonts DESTINATION ${CMAKE_BINARY_DIR})
