cmake_minimum_required(VERSION 3.20)
project(HolyC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- Optional LLVM dependency ---
find_package(LLVM CONFIG QUIET)
if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    include_directories(${LLVM_INCLUDE_DIRS})
    add_definitions(${LLVM_DEFINITIONS})
else()
    message(STATUS "LLVM not found — building without LLVM backend")
endif()

# --- Collect sources ---
file(GLOB_RECURSE LIB_SOURCES
    src/support/*.cpp
    src/lexer/*.cpp
    src/preprocessor/*.cpp
    src/ast/*.cpp
    src/parser/*.cpp
    src/sema/*.cpp
    src/interpreter/*.cpp
    src/codegen/*.cpp
)

# --- Driver helper sources (not main.cpp) ---
set(DRIVER_SOURCES
    src/driver/Formatter.cpp
    src/driver/JITCache.cpp
    src/lsp/LSPServer.cpp
)

# --- hcc executable ---
add_executable(hcc src/driver/main.cpp ${LIB_SOURCES} ${DRIVER_SOURCES})
target_include_directories(hcc PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(LLVM_FOUND)
    llvm_map_components_to_libnames(LLVM_LIBS core support orcjit executionengine runtimedyld jitlink native passes AllTargetsAsmParsers AllTargetsInfos AllTargetsDescs AllTargetsCodeGens bitwriter bitreader)
    target_link_libraries(hcc PRIVATE ${LLVM_LIBS} -Wl,--whole-archive holyc_rt -Wl,--no-whole-archive dl)
    target_compile_definitions(hcc PRIVATE HCC_HAS_LLVM=1)
else()
    target_link_libraries(hcc PRIVATE dl)
endif()

# --- Build-type flags ---
target_compile_options(hcc PRIVATE
    -Wall -Wextra
    -fstack-protector-strong
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined>
    $<$<CONFIG:Release>:
        -O3
        -march=native
        -fPIE
        -D_FORTIFY_SOURCE=2
        -flto=auto
        -fno-plt
        -funroll-loops
        -fomit-frame-pointer
        -fvisibility=hidden
    >
)
target_link_options(hcc PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
    $<$<CONFIG:Release>:-pie -flto=auto -Wl,-z,relro -Wl,-z,now>
    -rdynamic
)

if(TARGET holyc_rt)
    target_compile_options(holyc_rt PRIVATE -fstack-protector-strong)
endif()

# --- HolyC runtime library ---
add_library(holyc_rt STATIC src/runtime/holyc_rt.cpp)
target_include_directories(holyc_rt PUBLIC ${CMAKE_SOURCE_DIR}/src/runtime)
target_compile_options(holyc_rt PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Release>:-O3 -march=native -flto=auto -fno-plt -funroll-loops -fomit-frame-pointer>
)
# No sanitizers for the runtime — it will be linked into compiled HolyC programs
set_target_properties(holyc_rt PROPERTIES POSITION_INDEPENDENT_CODE ON)

# --- Shared library for tests ---
add_library(hcc_lib STATIC ${LIB_SOURCES} ${DRIVER_SOURCES})
target_include_directories(hcc_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_compile_options(hcc_lib PRIVATE
    -Wall -Wextra
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined>
    $<$<CONFIG:Release>:-O3 -march=native -flto=auto -fno-plt -funroll-loops -fomit-frame-pointer -fvisibility=hidden>
)

# --- Tests ---
enable_testing()

# Lexer unit test
add_executable(test_lexer tests/lexer/test_lexer.cpp)
target_link_libraries(test_lexer PRIVATE hcc_lib)
target_compile_options(test_lexer PRIVATE
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined>
)
target_link_options(test_lexer PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_test(NAME lexer_unit_test COMMAND test_lexer)

# Preprocessor unit test
add_executable(test_preprocessor tests/preprocessor/test_preprocessor.cpp)
target_link_libraries(test_preprocessor PRIVATE hcc_lib)
target_compile_options(test_preprocessor PRIVATE
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined>
)
target_link_options(test_preprocessor PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_test(NAME preprocessor_unit_test COMMAND test_preprocessor)

# Parser unit test
add_executable(test_parser tests/parser/test_parser.cpp)
target_link_libraries(test_parser PRIVATE hcc_lib)
target_compile_options(test_parser PRIVATE
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined>
)
target_link_options(test_parser PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_test(NAME parser_unit_test COMMAND test_parser)

# Interpreter unit test
add_executable(test_interpreter tests/interpreter/test_interpreter.cpp)
target_link_libraries(test_interpreter PRIVATE hcc_lib)
target_compile_options(test_interpreter PRIVATE
    $<$<CONFIG:Debug>:-g -fsanitize=address,undefined>
)
target_link_options(test_interpreter PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
add_test(NAME interpreter_unit_test COMMAND test_interpreter)

# .HC file tests
file(GLOB HC_LEXER_TESTS tests/lexer/*.HC)
foreach(test_file ${HC_LEXER_TESTS})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_test(
        NAME lexer/${test_name}
        COMMAND hcc --print-tokens ${test_file}
    )
endforeach()

# --- Optional readline ---
find_library(READLINE_LIB readline)
if(READLINE_LIB)
    target_compile_definitions(hcc PRIVATE HCC_HAS_READLINE=1)
    target_link_libraries(hcc PRIVATE readline)
endif()

function(add_holyc_rt_target triple compiler)
    string(REPLACE "-" "_" triple_safe "${triple}")
    find_program(CROSS_CXX_${triple_safe} ${compiler})
    if(CROSS_CXX_${triple_safe})
        set(OUTPUT_OBJ ${CMAKE_BINARY_DIR}/lib/holyc_rt_${triple}.o)
        add_custom_command(
            OUTPUT ${OUTPUT_OBJ}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib
            COMMAND ${CROSS_CXX_${triple_safe}} -c ${CMAKE_SOURCE_DIR}/src/runtime/holyc_rt.cpp
                    -o ${OUTPUT_OBJ} -std=c++17 -O2 -fno-exceptions
            DEPENDS ${CMAKE_SOURCE_DIR}/src/runtime/holyc_rt.cpp
            COMMENT "Cross-compiling holyc_rt for ${triple}"
        )
        add_custom_target(holyc_rt_${triple_safe} DEPENDS ${OUTPUT_OBJ})
        message(STATUS "Will cross-compile holyc_rt for ${triple} using ${CROSS_CXX_${triple_safe}}")
    else()
        message(STATUS "Cross-compiler ${compiler} not found; skipping holyc_rt for ${triple}")
    endif()
endfunction()

add_holyc_rt_target("aarch64-linux-gnu" "aarch64-linux-gnu-g++")
add_holyc_rt_target("x86_64-w64-mingw32" "x86_64-w64-mingw32-g++")

include(GNUInstallDirs)
install(TARGETS hcc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_BINARY_DIR}/libholyc_rt.a
        DESTINATION ${CMAKE_INSTALL_LIBDIR}
        OPTIONAL)
install(DIRECTORY stdlib/ DESTINATION ${CMAKE_INSTALL_DATADIR}/holyc/stdlib
        FILES_MATCHING PATTERN "*.HC" PATTERN "*.HH")
