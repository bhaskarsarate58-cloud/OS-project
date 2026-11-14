cmake_minimum_required(VERSION 3.16)
project(DemandPagingSimulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_executable(DemandPagingSimulator
    src/main.cpp
    src/MemoryManager.cpp
    src/PageReplacement.cpp
    src/SwapManager.cpp
)
target_include_directories(DemandPagingSimulator PRIVATE src)

if(WIN32)
    target_link_libraries(DemandPagingSimulator PRIVATE ws2_32)
endif()

if(MSVC)
    target_compile_options(DemandPagingSimulator PRIVATE /W4 /permissive-)
else()
    target_compile_options(DemandPagingSimulator PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(DemandPagingTests
        tests/test_memory_manager.cpp
        src/MemoryManager.cpp
        src/PageReplacement.cpp
        src/SwapManager.cpp
    )
    target_include_directories(DemandPagingTests PRIVATE src)
    if(MSVC)
        target_compile_options(DemandPagingTests PRIVATE /W4 /permissive-)
    else()
        target_compile_options(DemandPagingTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME DemandPagingEngineTests COMMAND DemandPagingTests)
endif()
