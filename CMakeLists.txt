cmake_minimum_required (VERSION 3.31)

set (target_name rissetdrop)
set (target_version "0.2.0")

project (RissetDrop VERSION ${target_version} LANGUAGES C CXX OBJC OBJCXX)

# Off for release packaging, so building never touches the installed plugins
option (RISSETDROP_INSTALL_PLUGINS "Install the plugins into the user's plugin folders after each build" ON)
option (RISSETDROP_BUILD_TESTS "Build risset_render_test, the offline DSP harness" ON)

# ==== YUP, vendored next to this file
set (YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set (YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
set (YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory (yup)

# ==== Identity. The AU codes are OSTypes: exactly four characters, and the
# manufacturer code needs at least one upper case letter.
set (plugin_name "RissetDrop")
set (plugin_description "Endless Risset accelerando and ritardando for drops, risers and fills.")
set (plugin_vendor "sousastep")
set (plugin_bundle_namespace "quest.sousastep")
set (plugin_email "")
set (plugin_url "https://github.com/jbaylies/RissetDrop")
set (plugin_au_manufacturer "Sous")
set (plugin_au_subtype "RsDp")
set (plugin_copyright "MIT License. Copyright (c) 2026 RissetDrop contributors.")

yup_audio_plugin (
    TARGET_NAME ${target_name}
    TARGET_VERSION ${target_version}
    TARGET_APP_ID "${plugin_bundle_namespace}.${plugin_name}"
    TARGET_APP_NAMESPACE "${plugin_bundle_namespace}"
    TARGET_CXX_STANDARD 20
    TARGET_IDE_GROUP "RissetDrop"
    PLUGIN_ID "${plugin_bundle_namespace}.${plugin_name}"
    PLUGIN_NAME "${plugin_name}"
    PLUGIN_VENDOR "${plugin_vendor}"
    PLUGIN_EMAIL "${plugin_email}"
    PLUGIN_VERSION "${target_version}"
    PLUGIN_DESCRIPTION "${plugin_description}"
    PLUGIN_URL "${plugin_url}"
    PLUGIN_IS_SYNTH OFF
    PLUGIN_IS_MONO OFF
    PLUGIN_CREATE_CLAP ON
    PLUGIN_CREATE_VST3 ON
    PLUGIN_CREATE_AU ON
    PLUGIN_CREATE_AUv3 OFF
    PLUGIN_CREATE_LV2 OFF
    PLUGIN_CREATE_AAX OFF
    PLUGIN_CREATE_STANDALONE ON
    PLUGIN_CLAP_FEATURES audio-effect stereo
    PLUGIN_VST3_CATEGORIES Fx Delay
    PLUGIN_AU_MANUFACTURER "${plugin_au_manufacturer}"
    PLUGIN_AU_SUBTYPE "${plugin_au_subtype}"
    PLUGIN_COPY_AFTER_BUILD ${RISSETDROP_INSTALL_PLUGINS}
    MODULES
        yup::yup_gui
        yup::yup_audio_processors)

# ==== The standalone app processes the audio interface's input, and macOS
# hands an app silence unless its Info.plist says why it wants the microphone.
if (TARGET ${target_name}_standalone_plugin)
    set_target_properties (${target_name}_standalone_plugin PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/cmake/StandaloneInfo.plist.in"
        MACOSX_BUNDLE_COPYRIGHT "${plugin_copyright}")
endif()

# ==== Install real copies of the VST3 and CLAP bundles. YUP symlinks them into
# ~/Library/Audio/Plug-Ins, and a link into a privacy-protected folder such as
# ~/Documents is silently skipped by hosts without access to it (Ableton Live).
# The copy is signed again, ad hoc, since the bundle changes after linking.
if (NOT RISSETDROP_INSTALL_PLUGINS)
    # nothing to install
elseif (APPLE)
    foreach (format vst3 clap)
        set (format_target ${target_name}_${format}_plugin)
        if (NOT TARGET ${format_target})
            continue()
        endif()

        string (TOUPPER "${format}" format_folder)
        set (install_dir "$ENV{HOME}/Library/Audio/Plug-Ins/${format_folder}")
        add_custom_command (TARGET ${format_target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${install_dir}/${format_target}.${format}" "${install_dir}/${plugin_name}.${format}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:${format_target}>" "${install_dir}/${plugin_name}.${format}"
            COMMAND codesign --force --sign - "${install_dir}/${plugin_name}.${format}"
            COMMENT "Copying ${format_folder} plugin to ${install_dir}/${plugin_name}.${format}"
            VERBATIM)
    endforeach()
elseif (WIN32)
    # YUP installs nothing on Windows. The per-user folders from the VST3 and
    # CLAP specs need no administrator rights. A VST3 bundle's inner binary
    # must carry the bundle's name, so both are renamed.
    set (user_plugins "$ENV{LOCALAPPDATA}/Programs/Common")
    if (TARGET ${target_name}_vst3_plugin)
        set (vst3_bundle "${user_plugins}/VST3/${plugin_name}.vst3")
        add_custom_command (TARGET ${target_name}_vst3_plugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${vst3_bundle}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_FILE_DIR:${target_name}_vst3_plugin>/../.." "${vst3_bundle}"
            COMMAND ${CMAKE_COMMAND} -E rename "${vst3_bundle}/Contents/x86_64-win/$<TARGET_FILE_NAME:${target_name}_vst3_plugin>" "${vst3_bundle}/Contents/x86_64-win/${plugin_name}.vst3"
            COMMENT "Copying VST3 plugin to ${vst3_bundle}"
            VERBATIM)
    endif()
    if (TARGET ${target_name}_clap_plugin)
        add_custom_command (TARGET ${target_name}_clap_plugin POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${user_plugins}/CLAP"
            COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:${target_name}_clap_plugin>" "${user_plugins}/CLAP/${plugin_name}.clap"
            COMMENT "Copying CLAP plugin to ${user_plugins}/CLAP/${plugin_name}.clap"
            VERBATIM)
    endif()
endif()

# ==== Strict warnings on our own files only. YUP and its externals are read as
# system headers here (quoted includes inside them inherit that).
if (MSVC)
    set (strict_warnings "/W4;/external:anglebrackets;/external:W0")
else()
    set (strict_warnings "--system-header-prefix=yup_;--system-header-prefix=xsimd/;-Wall;-Wextra;-Wpedantic;-Wshadow;-Wconversion;-Wsign-conversion;-Wfloat-conversion;-Wdouble-promotion")
endif()

# ==== DSP: plain C++, no YUP, shared by the plugin and the offline harness
file (GLOB_RECURSE dsp_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/source/dsp/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source/dsp/*.h")

# ==== Plugin and UI, compiled once into the core every format links
file (GLOB_RECURSE plugin_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/source/plugin/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source/plugin/*.h"
    "${CMAKE_CURRENT_LIST_DIR}/source/ui/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/source/ui/*.h")

set (all_sources ${dsp_sources} ${plugin_sources})
source_group (TREE ${CMAKE_CURRENT_LIST_DIR}/source FILES ${all_sources})
target_sources (${target_name}_shared PRIVATE ${all_sources})
target_include_directories (${target_name}_shared PRIVATE "${CMAKE_CURRENT_LIST_DIR}/source")
set_source_files_properties (${all_sources} PROPERTIES COMPILE_OPTIONS "${strict_warnings}")

# ==== Offline harness: renders test signals through the engine and checks
# them for clicks, level jumps and transition continuity. No YUP at all.
if (RISSETDROP_BUILD_TESTS)
    add_executable (risset_render_test
        ${dsp_sources}
        "${CMAKE_CURRENT_LIST_DIR}/tools/render_test/main.cpp")
    target_include_directories (risset_render_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/source")
    target_compile_features (risset_render_test PRIVATE cxx_std_20)
    target_compile_options (risset_render_test PRIVATE ${strict_warnings})
    set_target_properties (risset_render_test PROPERTIES FOLDER "RissetDrop")
endif()
