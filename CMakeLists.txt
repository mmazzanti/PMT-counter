# What CMake to start at
cmake_minimum_required(VERSION 3.12)

# Include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
include($ENV{PICO_SDK_PATH}/tools/CMakeLists.txt)


# Include the subsidiary .cmake file to get the SDK
#include(pico_sdk_import.cmake)

# Set the name and version of the project
project(PMT-Counter VERSION 1.0.0)

# Initalise the SDK
pico_sdk_init()

# Link the Project to a source file (step 4.6)
add_executable(${PROJECT_NAME} main.c)

# Create C header file with the name <pio program>.pio.h
pico_generate_pio_header(${PROJECT_NAME}  
        ${CMAKE_CURRENT_LIST_DIR}/pio_code.pio
)

# Link the Project to an extra library (pico_stdlib)
target_link_libraries(${PROJECT_NAME} pico_stdlib hardware_pio hardware_dma)

# Enable usb output, disable uart output
pico_enable_stdio_usb(${PROJECT_NAME} 1)
pico_enable_stdio_uart(${PROJECT_NAME} 0)

# Create map/bin/hex/uf2 files
pico_add_extra_outputs(${PROJECT_NAME})

#configure_file(build/PMT-Counter.uf2 ../release/PMT-Counter.uf2 COPYONLY)