cmake_minimum_required(VERSION 3.10)
project(ShieldTV-dxtdecoder)

set(CMAKE_CXX_STANDARD 17)

find_library(SDL2 SDL2)
find_library(log-lib log)
find_library(android-lib android)
find_library(dl-lib dl) 

add_library(dobby STATIC IMPORTED)
set_target_properties(dobby PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/libs/arm/libdobby.a)
    
target_include_directories(dxt_decoder PRIVATE
    ${CMAKE_SOURCE_DIR}/external/dobby/include
)

add_library(dxt_decoder SHARED
    src/dxtdecoder/libdxt_decoder.cpp 
    src/dxtdecoder/pfh.cpp 
)

target_link_libraries(dxt_decoder dobby
    ${log-lib}
    ${dl-lib} 
    ${android-lib} 
    ${egl-lib} 
    ${glesv3-lib}
)
