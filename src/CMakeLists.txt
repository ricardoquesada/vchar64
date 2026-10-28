# Source build definition enforcing VChar64's 3-target acyclic dependency architecture:
#
# - `vchar64_core` (STATIC): Headless C64 domain model, QUndoCommands, QImage tile atlas,
#   clipboard codec, and file importers/exporters. Links ONLY Qt6::Core and Qt6::Gui
#   (CPU value types such as QUndoStack, QImage, and QColor; zero Qt6::Widgets or QPA windowing).
# - `vchar64_net` (STATIC): Headless Contiki TCP live preview (`ServerPreview`, C89 wire
#   protocol) and HTTPS `AutoUpdater`. Links ONLY `vchar64_core` and Qt6::Network.
# - `vchar64` (EXECUTABLE): Desktop Qt6::Widgets MDI user interface, dock panels, and dialogs.

# ---------------------------------------------------------------------------
# Layer 1: Headless Core Domain & Codec Library (`vchar64_core`)
# ---------------------------------------------------------------------------
set(VCHAR64_CORE_SOURCES
    commands.cpp
    commands.h
    core/charsetimagecache.cpp
    core/charsetimagecache.h
    core/clipboardcodec.cpp
    core/clipboardcodec.h
    core/documentimagerenderer.cpp
    core/documentimagerenderer.h
    core/indexformat.h
    core/result.h
    core/tileatlascache.cpp
    core/tileatlascache.h
    fileutils.cpp
    fileutils.h
    palette.cpp
    palette.h
    state.cpp
    state.h
    stateexport.cpp
    stateexport.h
    stateimport.cpp
    stateimport.h
    ui/activedocumentrelay.cpp
    ui/activedocumentrelay.h
)

qt_add_library(vchar64_core STATIC
    ${VCHAR64_CORE_SOURCES}
)

target_include_directories(vchar64_core
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(vchar64_core
    PUBLIC
    Qt6::Core
    Qt6::Gui
    PRIVATE
    vchar64_warnings
)

# ---------------------------------------------------------------------------
# Layer 2: Headless Network Preview & Updater Library (`vchar64_net`)
# ---------------------------------------------------------------------------
set(VCHAR64_NET_SOURCES
    autoupdater.cpp
    autoupdater.h
    preview/ilivepreviewsink.cpp
    preview/ilivepreviewsink.h
    serverpreview.cpp
    serverpreview.h
    serverprotocol.h
)

qt_add_library(vchar64_net STATIC
    ${VCHAR64_NET_SOURCES}
)

target_include_directories(vchar64_net
    PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(vchar64_net
    PUBLIC
    vchar64_core
    Qt6::Network
    PRIVATE
    vchar64_warnings
)

# ---------------------------------------------------------------------------
# Layer 3: Desktop Qt6::Widgets Application (`vchar64`)
# ---------------------------------------------------------------------------
set(VCHAR64_UI_SOURCES
    aboutdialog.cpp aboutdialog.h
    bigcharwidget.cpp bigcharwidget.h
    charsetwidget.cpp charsetwidget.h
    colorrectwidget.cpp colorrectwidget.h
    exportdialog.cpp exportdialog.h
    exportimagedialog.cpp exportimagedialog.h
    importbinarydialog.cpp importbinarydialog.h
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
    palettewidget.cpp palettewidget.h
    preferences.cpp preferences.h
    preferencesdialog.cpp preferencesdialog.h
    selectcolordialog.cpp selectcolordialog.h
    serverconnectdialog.cpp serverconnectdialog.h
    tilepropertiesdialog.cpp tilepropertiesdialog.h
    tilesetwidget.cpp tilesetwidget.h
    updatedialog.cpp updatedialog.h
    utils.cpp utils.h
    vchar64application.cpp vchar64application.h
)

set(UI_FORMS
    aboutdialog.ui
    exportdialog.ui
    exportimagedialog.ui
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

qt_add_executable(${PROJECT_NAME}
    ${VCHAR64_UI_SOURCES}
    ${UI_FORMS}
    resources.qrc
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
    vchar64_core
    vchar64_net
    vchar64_warnings
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Widgets
)
