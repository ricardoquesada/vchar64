add_compile_definitions(VERSION="0.99.0")
add_compile_definitions(GIT_VERSION="n/a")

set(SOURCES
    aboutdialog.cpp aboutdialog.h
    autoupdater.cpp autoupdater.h
    bigcharwidget.cpp bigcharwidget.h
    charsetwidget.cpp charsetwidget.h
    colorrectwidget.cpp colorrectwidget.h
    commands.cpp commands.h
    exportdialog.cpp exportdialog.h
    exportimagedialog.cpp exportimagedialog.h
    fileutils.cpp fileutils.h
    importkoalabitmapwidget.cpp importkoalabitmapwidget.h
    importkoalacharsetwidget.cpp importkoalacharsetwidget.h
    importkoaladialog.cpp importkoaladialog.h
    importvicecharsetwidget.cpp importvicecharsetwidget.h
    importvicedialog.cpp importvicedialog.h
    importvicescreenramwidget.cpp importvicescreenramwidget.h
    main.cpp
    mainwindow.cpp mainwindow.h
    mappropertiesdialog.cpp mappropertiesdialog.h
    mapwidget.cpp mapwidget.h
    palette.cpp palette.h
    palettewidget.cpp palettewidget.h
    preferences.cpp preferences.h
    preferencesdialog.cpp preferencesdialog.h
    selectcolordialog.cpp selectcolordialog.h
    serverconnectdialog.cpp serverconnectdialog.h
    serverpreview.cpp serverpreview.h
    serverprotocol.h
    state.cpp state.h
    stateexport.cpp stateexport.h
    stateimport.cpp stateimport.h
    tilepropertiesdialog.cpp tilepropertiesdialog.h
    tilesetwidget.cpp tilesetwidget.h
    updatedialog.cpp updatedialog.h
    utils.cpp utils.h
    vchar64application.cpp vchar64application.h
    xlinkpreview.cpp xlinkpreview.h
)

set(UI_SOURCES
    exportimagedialog.ui
    aboutdialog.ui
    exportdialog.ui
    importkoaladialog.ui
    importvicedialog.ui
    mainwindow.ui
    mappropertiesdialog.ui
    preferencesdialog.ui
    selectcolordialog.ui
    serverconnectdialog.ui
    tilepropertiesdialog.ui
    updatedialog.ui
)

find_package(Qt6
    COMPONENTS Core Gui Network Widgets
    REQUIRED
)

qt_add_big_resources(RESOURCES
    resources.qrc
)

# target_sources(rcc_object_resources
#   PRIVATE
# )

# Have big_resource.qrc treated as a source file by Qt Creator
list(APPEND SOURCES resources.qrc)
set_property(SOURCE resources.qrc PROPERTY SKIP_AUTORCC ON)

qt_add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${RESOURCES}
)

qt_add_ui(${PROJECT_NAME}
    SOURCES ${UI_SOURCES}
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
    Qt::Core
    Qt::Gui
    Qt::Network
    Qt::Widgets
)

