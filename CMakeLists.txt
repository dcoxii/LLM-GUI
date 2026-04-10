cmake_minimum_required(VERSION 3.21)

project(llm_gui_qt VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup()

option(LLM_GUI_USE_QTERMWIDGET "Use QTermWidget for the embedded terminal when available" ON)
set(LLM_GUI_QTERMWIDGET_ENABLED OFF)
if(LLM_GUI_USE_QTERMWIDGET)
    find_package(qtermwidget6 CONFIG QUIET)
    if(qtermwidget6_FOUND)
        set(LLM_GUI_QTERMWIDGET_ENABLED ON)
    endif()
endif()

qt_add_executable(llm-gui
    src/main.cpp

    src/app/MainWindow.h
    src/app/MainWindow.cpp
    src/app/SettingsDialog.h
    src/app/SettingsDialog.cpp
    src/app/HuggingFaceDialog.h
    src/app/HuggingFaceDialog.cpp
    src/app/PluginManagerDialog.h
    src/app/PluginManagerDialog.cpp
    src/app/PtyTerminal.h
    src/app/PtyTerminal.cpp

    src/core/AppSettings.h
    src/core/AppSettings.cpp
    src/core/ChatMessage.h
    src/core/ChatSession.h
    src/core/ChatSession.cpp
    src/core/SessionStore.h
    src/core/SessionStore.cpp

    src/services/ChatStreamParser.h
    src/services/ChatStreamParser.cpp
    src/services/ProviderClient.h
    src/services/DeepSeekClient.h
    src/services/DeepSeekClient.cpp
    src/services/HuggingFaceHubClient.h
    src/services/HuggingFaceHubClient.cpp
    src/services/LlamaCppClient.h
    src/services/LlamaCppClient.cpp
    src/services/ProviderHealthProbe.h
    src/services/ProviderHealthProbe.cpp

    src/util/AttachmentProcessor.h
    src/util/AttachmentProcessor.cpp
    src/util/TranscriptFormatter.h
    src/util/TranscriptFormatter.cpp
    src/tools/ToolRegistry.h
    src/tools/ToolRegistry.cpp
    src/tools/SandboxRunner.h
    src/tools/SandboxRunner.cpp

    resources/app.qrc
)

target_include_directories(llm-gui PRIVATE src)

target_compile_options(llm-gui PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

if(LLM_GUI_QTERMWIDGET_ENABLED)
    target_compile_definitions(llm-gui PRIVATE LLM_GUI_HAS_QTERMWIDGET=1)
    target_link_libraries(llm-gui PRIVATE qtermwidget6)
endif()

target_link_libraries(llm-gui
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
)

include(GNUInstallDirs)

install(TARGETS llm-gui
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES resources/icons/llm-gui.svg
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps
)

install(FILES packaging/linux/llm-gui.desktop
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
)

install(DIRECTORY plugins/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/llm-gui/plugins
    FILES_MATCHING
        PATTERN "*.json"
        PATTERN "*.py"
        PATTERN "README.txt"
)

option(LLM_GUI_BUNDLE_QT "Bundle Qt runtime files for relocatable installs" OFF)

if(LLM_GUI_BUNDLE_QT)
    qt_generate_deploy_app_script(
        TARGET llm-gui
        FILENAME_VARIABLE deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${deploy_script})
endif()

install(DIRECTORY scripts/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/llm-gui
    FILES_MATCHING
        PATTERN "*.sh"
        PATTERN "*.txt"
)

install(DIRECTORY third_party/
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/llm-gui
    FILES_MATCHING
        PATTERN "*.txt"
        PATTERN ".gitkeep"
)


# Embedded llama.cpp integration
option(LLM_GUI_ENABLE_EMBEDDED_LLAMA_CPP "Build with embedded llama.cpp support when sources are present" ON)
set(LLM_GUI_LLAMA_CPP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp" CACHE PATH "Path to llama.cpp sources")

if(LLM_GUI_ENABLE_EMBEDDED_LLAMA_CPP AND EXISTS "${LLM_GUI_LLAMA_CPP_SOURCE_DIR}/CMakeLists.txt")
    message(STATUS "Embedding llama.cpp from ${LLM_GUI_LLAMA_CPP_SOURCE_DIR}")
    add_subdirectory("${LLM_GUI_LLAMA_CPP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/llama.cpp-build" EXCLUDE_FROM_ALL)
    target_compile_definitions(llm-gui PRIVATE LLM_GUI_HAS_LLAMA_CPP=1)

    if(TARGET llama)
        target_link_libraries(llm-gui PRIVATE llama)
    endif()
    if(TARGET ggml)
        target_link_libraries(llm-gui PRIVATE ggml)
    endif()
    if(TARGET common)
        target_link_libraries(llm-gui PRIVATE common)
    endif()
else()
    message(STATUS "Embedded llama.cpp sources not found; local provider will build in disabled mode")
endif()
