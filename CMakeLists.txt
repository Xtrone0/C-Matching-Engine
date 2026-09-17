cmake_minimum_required(VERSION 3.20)

project(MatchingEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Useful for clangd and other tooling.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(enable_warnings target)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow

            # -Wconversion
            # -Wsign-conversion
            -Wfloat-equal
            -Wformat=2
            -Wduplicated-cond
            -Wduplicated-branches
            -Wlogical-op
            -Wnull-dereference
            -Wvla
        )
    endif()
endfunction()

# --------------------
# Order book library
# --------------------
add_library(order_book
    src/order_book.cpp
)

target_include_directories(order_book PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

enable_warnings(order_book)

# --------------------
# Tests
# --------------------
add_executable(order_book_tests
    tests/order_book_test.cpp
)

target_link_libraries(order_book_tests PRIVATE
    order_book
)
target_compile_definitions(order_book_tests PRIVATE
    TEST_TRACE_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/test-failures"
    TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")

enable_warnings(order_book_tests)

# Run each named case separately so a crash cannot hide other results.
enable_testing()

# Discover the named cases from the source at configure time.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    tests/order_book_test.cpp)
file(STRINGS tests/order_book_test.cpp test_case_declarations
    REGEX "^[ \t]*TEST_CASE\\([a-zA-Z0-9_]+\\)")

foreach(declaration IN LISTS test_case_declarations)
    string(REGEX MATCH "TEST_CASE\\(([a-zA-Z0-9_]+)\\)" unused "${declaration}")
    set(case_name "${CMAKE_MATCH_1}")
    add_test(NAME "order_book.${case_name}" COMMAND order_book_tests "${case_name}")
    set_tests_properties("order_book.${case_name}" PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 20)
endforeach()

# Timing benchmarks run separately from correctness tests and CTest.
add_executable(order_book_benchmark benchmarks/order_book_benchmark.cpp)
target_link_libraries(order_book_benchmark PRIVATE order_book)
enable_warnings(order_book_benchmark)
