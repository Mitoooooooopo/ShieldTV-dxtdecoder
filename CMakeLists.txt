cmake_minimum_required(VERSION 3.10)
project(ShieldTV-dxtdecoder)

set(CMAKE_CXX_STANDARD 17)

set(DOBBY_DIR ${CMAKE_SOURCE_DIR}/external/dobby)

macro(SET_OPTION option value)
    set(${option} ${value} CACHE INTERNAL "" FORCE)
    endmacro()

    SET_OPTION(DOBBY_DEBUG OFF)
    SET_OPTION(DOBBY_GENERATE_SHARED ON)

    add_subdirectory(${DOBBY_DIR} dobby)

    # Ensure Dobby exports correct include root

        
find_library(SDL2 SDL2)
find_library(log-lib log)
find_library(android-lib android)
find_library(dl-lib dl) 


add_library(dxt_decoder SHARED
    src/dxtdecoder/libdxt_decoder.cpp 
    src/dxtdecoder/pfh.cpp 
)

target_link_libraries(dxt_decoder 
    dobby 
    ${log-lib}
    ${dl-lib} 
    ${android-lib} 
    ${egl-lib} 
    ${glesv3-lib}
)
