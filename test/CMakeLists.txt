# Slice 3 tests: comprehensive suite + GPU tests
cmake_minimum_required(VERSION 3.28)

add_test(NAME nlm_denoise_suite
    COMMAND python3 ${CMAKE_SOURCE_DIR}/test/run_tests.py
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

set_tests_properties(nlm_denoise_suite PROPERTIES
    ENVIRONMENT "NLM_BINARY=${CMAKE_BINARY_DIR}/nlm_denoise"
)

add_test(NAME nlm_denoise_gpu_basic
    COMMAND nlm_denoise
        ${CMAKE_SOURCE_DIR}/test/noisy_ramp.png
        ${CMAKE_BINARY_DIR}/test_gpu_output.png
        --patch-size 7 --search-window 21 --h 0.1 --use-gpu
)

add_test(NAME nlm_denoise_gpu_benchmark
    COMMAND nlm_denoise
        ${CMAKE_SOURCE_DIR}/test/noisy_256.png
        ${CMAKE_BINARY_DIR}/test_gpu_bench.png
        --patch-size 7 --search-window 21 --h 0.1 --use-gpu --benchmark
)
