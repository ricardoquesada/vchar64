add_compile_definitions(VERSION="0.2.5")
add_compile_definitions(GIT_VERSION="n/a")

set(SOURCES
    aboutdialog.cpp aboutdialog.h
    autoupdater.cpp autoupdater.h
    bigcharwidget.cpp bigcharwidget.h
    charsetwidget.cpp charsetwidget.h
    colorrectwidget.cpp colorrectwidget.h
    commands.cpp commands.h
    exportdialog.cpp exportdialog.h
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

find_package(Qt6
    COMPONENTS Core Gui Network Widgets
    REQUIRED
)

qt_standard_project_setup()

qt_add_big_resources(RESOURCES
    resources.qrc
)

qt_add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${RESOURCES}
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

