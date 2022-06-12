cmake_minimum_required(VERSION 3.12)

include(pico_sdk_import.cmake)

project(midi_sync)

pico_sdk_init()

add_executable(midi_sync
    midi_sync.c
    display.c
    switch.c
)

target_link_libraries(midi_sync pico_stdlib hardware_uart)

pico_add_extra_outputs(midi_sync)
