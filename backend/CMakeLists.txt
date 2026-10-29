# Core library: algorithms, simulation and API logic. Tests and the server link against it.
file(GLOB_RECURSE SCX_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
list(FILTER SCX_CORE_SOURCES EXCLUDE REGEX "/src/main\\.cpp$")
list(FILTER SCX_CORE_SOURCES EXCLUDE REGEX "/src/memory/AllocationHooks\\.cpp$")

add_library(scx_core STATIC ${SCX_CORE_SOURCES})
target_include_directories(scx_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(scx_core SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
if(WIN32)
  target_compile_definitions(scx_core PUBLIC _WIN32_WINNT=0x0A00)  # cpp-httplib needs the Windows 10 API level
  target_link_libraries(scx_core PUBLIC ws2_32)
endif()
if(MSVC)
  target_compile_options(scx_core PRIVATE /W4)
else()
  target_compile_options(scx_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Replaces global operator new/delete so runs can report peak heap use. Only linked where wanted.
add_library(scx_alloc_hooks OBJECT ${CMAKE_CURRENT_SOURCE_DIR}/src/memory/AllocationHooks.cpp)
target_link_libraries(scx_alloc_hooks PRIVATE scx_core)

# Executables that replace operator new/delete must link the C++ runtime statically on MinGW: otherwise
# code inside libstdc++-6.dll (e.g. std::string growth) allocates with the DLL's own operator new and our
# operator delete would free memory it never allocated. Static linking also lets scx_server.exe run
# without the MinGW DLLs on PATH.
function(scx_link_hooked_statically target)
  if(MINGW)
    target_link_options(${target} PRIVATE -static)
  endif()
endfunction()

add_executable(scx_server src/main.cpp $<TARGET_OBJECTS:scx_alloc_hooks>)
target_link_libraries(scx_server PRIVATE scx_core)
scx_link_hooked_statically(scx_server)

# Worked examples of the syllabus modules (DP, backtracking, merge sort, topological sort).
add_executable(scx_advanced_demo tools/advanced_demo.cpp)
target_link_libraries(scx_advanced_demo PRIVATE scx_core)
scx_link_hooked_statically(scx_advanced_demo)

# Command-line experiment runner (hooked + static so each run reports measured peak heap).
add_executable(scx_experiments tools/experiments_main.cpp $<TARGET_OBJECTS:scx_alloc_hooks>)
target_link_libraries(scx_experiments PRIVATE scx_core)
scx_link_hooked_statically(scx_experiments)

# Multi-day experiment runner (hooked + static like scx_experiments).
add_executable(scx_multiday_experiments tools/multiday_experiments_main.cpp $<TARGET_OBJECTS:scx_alloc_hooks>)
target_link_libraries(scx_multiday_experiments PRIVATE scx_core)
scx_link_hooked_statically(scx_multiday_experiments)

# One test executable per file in tests/; each is registered with CTest.
file(GLOB SCX_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp)
foreach(test_source ${SCX_TEST_SOURCES})
  get_filename_component(test_name ${test_source} NAME_WE)
  add_executable(${test_name} ${test_source})
  target_link_libraries(${test_name} PRIVATE scx_core)
  target_compile_definitions(${test_name} PRIVATE SCX_DATA_DIR="${CMAKE_SOURCE_DIR}/data")
  if(test_name STREQUAL "test_api")
    target_sources(${test_name} PRIVATE $<TARGET_OBJECTS:scx_alloc_hooks>)   # API tests check real memory figures
  endif()
  scx_link_hooked_statically(${test_name})
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
