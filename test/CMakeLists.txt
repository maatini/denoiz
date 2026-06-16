# Slice 3 tests: comprehensive suite + GPU tests
cmake_minimum_required(VERSION 3.28)

add_test(NAME denoise_suite
    COMMAND python3 ${CMAKE_SOURCE_DIR}/test/run_tests.py
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

set_tests_properties(denoise_suite PROPERTIES
    ENVIRONMENT "NLM_BINARY=${CMAKE_BINARY_DIR}/denoise"
)

add_test(NAME denoise_gpu_basic
    COMMAND denoise
        ${CMAKE_SOURCE_DIR}/test/noisy_ramp.png
        ${CMAKE_BINARY_DIR}/test_gpu_output.png
        --patch-size 7 --search-window 21 --h 0.1 --use-gpu
)

add_test(NAME denoise_gpu_benchmark
    COMMAND denoise
        ${CMAKE_SOURCE_DIR}/test/noisy_256.png
        ${CMAKE_BINARY_DIR}/test_gpu_bench.png
        --patch-size 7 --search-window 21 --h 0.1 --use-gpu --benchmark
)

add_test(NAME denoise_wavelet
    COMMAND denoise
        ${CMAKE_SOURCE_DIR}/test/noisy_ramp.png
        ${CMAKE_BINARY_DIR}/test_wavelet.png
        --patch-size 7 --search-window 21 --h 0.1 --wavelet
)
