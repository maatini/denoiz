cmake_minimum_required(VERSION 3.28)
project(nlm_denoise LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -ffast-math -march=armv8.5-a -mtune=apple-m1")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O3 -flto -ffast-math -march=armv8.5-a -mtune=apple-m1")

set(SOURCES
    src/nlm_main.cpp
    src/nlm_cli.cpp
    src/nlm_io.cpp
    src/nlm_cpu.cpp
    src/nlm_cpu_neon.cpp
    src/nlm_cpu_neon_fast.cpp
    src/nlm_wavelet.cpp
    src/nlm_adaptive_h.cpp
    src/nlm_ensemble.cpp
    src/nlm_coarse_to_fine.cpp
    src/nlm_metal.mm
)

add_executable(nlm_denoise ${SOURCES})

target_include_directories(nlm_denoise PRIVATE src)

target_link_libraries(nlm_denoise PRIVATE
    "-framework Foundation"
    "-framework Metal"
    "-framework CoreGraphics"
    "-framework Accelerate"
)

enable_testing()
add_subdirectory(test)

install(TARGETS nlm_denoise RUNTIME DESTINATION bin)
