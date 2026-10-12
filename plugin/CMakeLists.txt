cmake_minimum_required(VERSION 3.24)
project(TES64 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(../extern/CommonLibSSE CommonLibSSE EXCLUDE_FROM_ALL)
include(../extern/CommonLibSSE/cmake/CommonLibSSE.cmake)

add_commonlibsse_plugin(TES64
	NAME "TES64"
	AUTHOR "TES64"
	USE_ADDRESS_LIBRARY
	SOURCES
		src/main.cpp
		src/Config.cpp
		src/Sm64.cpp
		src/Input.cpp
		src/Mario.cpp
		src/Collision.cpp
		src/Render.cpp
		src/Camera.cpp
)
target_include_directories(TES64 PRIVATE src ../native/sm64/dist)
target_precompile_headers(TES64 PRIVATE src/PCH.h)
target_compile_definitions(TES64 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

