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
# Tests + benchmark
# --------------------
add_executable(order_book_tests
    tests/order_book_test.cpp
)

target_link_libraries(order_book_tests PRIVATE
    order_book
)

enable_warnings(order_book_tests)

# Also register it with CTest for later.
enable_testing()
add_test(NAME order_book_tests COMMAND order_book_tests)
