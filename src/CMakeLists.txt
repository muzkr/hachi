
add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        )

add_executable(hachi hachi.c 
        xmodem.c 
        led.c
        run_prog.S
)

target_include_directories(hachi PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# pull in common dependencies
target_link_libraries(hachi 
        pico_stdlib 
        hardware_timer 
        hardware_flash 
        boot_uf2_headers
)

pico_set_binary_type(hachi default)
pico_set_linker_script(hachi ${CMAKE_CURRENT_SOURCE_DIR}/memmap.ld)

# print elf size
add_custom_command(TARGET hachi POST_BUILD
        COMMAND arm-none-eabi-size $<TARGET_FILE:hachi>
        COMMENT "Printing binary size..."
)

# create map/bin/hex file etc.
pico_add_extra_outputs(hachi)
