cmake_minimum_required(VERSION 3.25)

project(anonrt LANGUAGES C CXX )

find_package(OpenCV REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(anonrt
	src/main.cpp
	src/sokol_impl.mm
	src/sokol_impl_imgui.cpp
	thirdparty/imgui/imgui.cpp
	thirdparty/imgui/imgui_draw.cpp
	thirdparty/imgui/imgui_widgets.cpp
	thirdparty/imgui/imgui_tables.cpp
	thirdparty/tinyfiledialogs/tinyfiledialogs.c
)

target_include_directories(anonrt PRIVATE
	include/
	thirdparty/sokol
	thirdparty/imgui
	thirdparty/tinyfiledialogs
)

target_link_libraries(anonrt PRIVATE
	${OpenCV_LIBRARIES}
)

# --- platform backend selection ---
if(APPLE)
    target_compile_definitions(anonrt PRIVATE SOKOL_METAL)
    target_link_libraries(anonrt PRIVATE
        "-framework Cocoa" "-framework QuartzCore"
        "-framework Metal" "-framework MetalKit"
    )
    # set_source_files_properties(src/sokol_impl.c PROPERTIES LANGUAGE OBJCXX)

elseif(UNIX AND NOT APPLE)
    target_compile_definitions(anonrt PRIVATE SOKOL_GLCORE)
    find_package(OpenGL REQUIRED)
    target_link_libraries(anonrt PRIVATE OpenGL::GL X11 Xi Xcursor dl pthread m)

elseif(WIN32)
    target_compile_definitions(anonrt PRIVATE SOKOL_D3D11)
    target_link_libraries(anonrt PRIVATE d3d11 dxgi)
endif()
