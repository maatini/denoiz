cmake_minimum_required(VERSION 3.28)
project(denoiz LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -flto -ffast-math -march=armv8.5-a -mtune=apple-m1")
set(CMAKE_OBJCXX_FLAGS_RELEASE "-O3 -flto -ffast-math -march=armv8.5-a -mtune=apple-m1")

# ── FFmpeg via pkg-config ──────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavformat libswscale libavutil)
pkg_check_modules(LIBVMAF REQUIRED libvmaf)

set(NLM_SOURCES
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

set(NLM_VIDEO_SOURCES
    src/nlm_video_temporal.cpp
    src/nlm_video_tuning.cpp
)

# ── denoise ────────────────────────────────────────────────────────────
add_executable(denoise src/nlm_main.cpp src/nlm_image_tuning.cpp ${NLM_SOURCES})

target_include_directories(denoise PRIVATE
    src
    ${LIBVMAF_INCLUDE_DIRS}
)

target_link_libraries(denoise PRIVATE
    ${LIBVMAF_LIBRARIES}
    "-framework Foundation"
    "-framework Metal"
    "-framework CoreGraphics"
    "-framework Accelerate"
)

target_link_options(denoise PRIVATE ${LIBVMAF_LDFLAGS})

# ── v-denoise ──────────────────────────────────────────────────────────
add_executable(v-denoise src/nlm_video_main.cpp ${NLM_SOURCES} ${NLM_VIDEO_SOURCES})

target_include_directories(v-denoise PRIVATE
    src
    ${FFMPEG_INCLUDE_DIRS}
    ${LIBVMAF_INCLUDE_DIRS}
)

target_link_libraries(v-denoise PRIVATE
    ${FFMPEG_LIBRARIES}
    ${LIBVMAF_LIBRARIES}
    "-framework Foundation"
    "-framework Metal"
    "-framework CoreGraphics"
    "-framework Accelerate"
)

target_link_options(v-denoise PRIVATE ${FFMPEG_LDFLAGS} ${LIBVMAF_LDFLAGS})

# ── Tests / Install ────────────────────────────────────────────────────
enable_testing()
add_subdirectory(test)

install(TARGETS denoise RUNTIME DESTINATION bin)
install(TARGETS v-denoise RUNTIME DESTINATION bin)
