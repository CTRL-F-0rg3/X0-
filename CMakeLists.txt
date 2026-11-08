cmake_minimum_required(VERSION 3.20)
project(x0p LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# libgccjit — opcjonalne na tym etapie, wpinamy się później
find_library(GCCJIT_LIBRARY NAMES gccjit)
if(GCCJIT_LIBRARY)
    message(STATUS "libgccjit: ${GCCJIT_LIBRARY}")
    add_compile_definitions(X0P_HAVE_GCCJIT)
else()
    message(STATUS "libgccjit: not found (backend disabled)")
endif()

# Rdzeń kompilatora jako biblioteka statyczna
add_library(x0p_core STATIC
    src/arena.cpp
    src/diagnostic.cpp
    src/lexer.cpp
    src/ast.cpp
    src/parser.cpp
    src/emit_asm.cpp
    src/emit_cpp.cpp
    src/emit_c.cpp
    src/emit_rust.cpp
)
target_include_directories(x0p_core PUBLIC include)

# CLI driver
add_executable(x0p src/main.cpp)
target_link_libraries(x0p PRIVATE x0p_core)
if(GCCJIT_LIBRARY)
    target_link_libraries(x0p PRIVATE ${GCCJIT_LIBRARY})
endif()
