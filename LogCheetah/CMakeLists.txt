cmake_minimum_required(VERSION 3.27)

project(LogCheetah)

#General Config
set(CMAKE_CXX_STANDARD 20)

#Dependencies
find_package(glbinding CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

#Compile
add_executable(LogCheetah WIN32
    CatWindow.cpp
    ConcurrencyLimiter.cpp
    DebugWindow.cpp
    DialogBlocklistedColumnsEditor.cpp
    DialogFrequencyChart.cpp
    DialogHistogramChart.cpp
    DialogOpenLocal.cpp
    DialogPickLogFormat.cpp
    DialogQosVisualizer.cpp
    DialogSaveLocal.cpp
    DialogSetup.cpp
    DSVParser.cpp
    GenericTextLogParseRouter.cpp
    Globals.cpp
    GuiStatusMonitor.cpp
    IniLexicon.cpp
    JsonParser.cpp
    LogCheetah.rc
    LogFormatter.cpp
    LogParserCommon.cpp
    MainLogView.cpp
    ObtainParseCoordinator.cpp
    Preferences.cpp
    SharedGlobals.cpp
    TRXParser.cpp
    WindowsDragDrop.cpp
    WinMain.cpp
    XmlLexicon.cpp
    )

#Link
target_link_libraries(LogCheetah
    PRIVATE glbinding::glbinding
    PRIVATE glbinding::glbinding-aux
    PRIVATE glm::glm
    PRIVATE ${OPENGL_LIBRARIES}
    gdiplus
    gdi32
    wsock32
    ws2_32
    comctl32
    )

#Extra files needed at run-time
add_custom_command(TARGET LogCheetah POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${PROJECT_SOURCE_DIR}/cats" "$<TARGET_FILE_DIR:LogCheetah>/cats"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory_if_different "${PROJECT_SOURCE_DIR}/shaders" "$<TARGET_FILE_DIR:LogCheetah>/shaders"
    )
