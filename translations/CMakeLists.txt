set(TRANSLATION_FILES
  vchar64_es.ts
  )

find_package(Qt6
    COMPONENTS LinguistTools
    REQUIRED
)

#qt6_add_translations(vchar64
#  TS_FILES ${TRANSLATION_FILES}
#)

qt6_add_lrelease(vchar64
  TS_FILES ${TRANSLATION_FILES}
    QM_FILES_OUTPUT_VARIABLE qm_files)
install(FILES ${qm_files} DESTINATION "translations")

