# GLSL -> SPIR-V at build time (glslc), embedded in the engine as uint32_t arrays: no shader
# files to ship or to find at run time.
#
#     artisan_embed_shaders(target shaders/a.vert shaders/a.frag)
#
# makes "shaders/a.vert.h" (array `a_vert`) in the build folder, on the target's include path.

set(glslc_hints $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin)
if(ANDROID)
    file(GLOB ndk_shader_tools ${ANDROID_NDK}/shader-tools/*) # the NDK ships glslc
    list(APPEND glslc_hints ${ndk_shader_tools})
endif()
find_program(ARTISAN_GLSLC glslc HINTS ${glslc_hints})
if(NOT ARTISAN_GLSLC)
    message(FATAL_ERROR "glslc not found: install shaderc (MSYS2: mingw-w64-ucrt-x86_64-shaderc, "
                        "Ubuntu: glslc, Homebrew: shaderc) or the Vulkan SDK.")
endif()

function(artisan_embed_shaders target)
    set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    file(MAKE_DIRECTORY ${out_dir})
    foreach(src ${ARGN})
        get_filename_component(name ${src} NAME)
        string(REPLACE "." "_" var ${name})
        set(spv ${out_dir}/${name}.spv)
        set(hdr ${out_dir}/${name}.h)
        add_custom_command(
            OUTPUT ${hdr}
            COMMAND ${ARTISAN_GLSLC} -O --target-env=vulkan1.1 -o ${spv} ${CMAKE_CURRENT_SOURCE_DIR}/${src}
            COMMAND ${CMAKE_COMMAND} -DSPV=${spv} -DHDR=${hdr} -DVAR=${var} -P ${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake
            DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake
            COMMENT "Compiling shader ${name}"
            VERBATIM)
        target_sources(${target} PRIVATE ${hdr})
    endforeach()
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
endfunction()
