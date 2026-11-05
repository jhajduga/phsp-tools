# Keep the reference IAEA sources isolated from project warning settings.
add_library(phsp_iaea OBJECT
    vendor/iaea/src/iaea_header.cpp
    vendor/iaea/src/iaea_phsp.cpp
    vendor/iaea/src/iaea_record.cpp
    vendor/iaea/src/utilities.cpp)
target_include_directories(phsp_iaea SYSTEM PRIVATE vendor/iaea/include)

add_library(phsp_core
    src/logging.cpp src/conversion.cpp src/analysis.cpp src/comparison.cpp src/pipeline.cpp src/processing.cpp
    $<TARGET_OBJECTS:phsp_iaea>)
add_library(phsp::core ALIAS phsp_core)
add_library(phsp::analysis ALIAS phsp_core)
target_include_directories(phsp_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_include_directories(phsp_core SYSTEM PRIVATE vendor/iaea/include)
target_link_libraries(phsp_core PUBLIC
    fmt::fmt Threads::Threads ROOT::RIO ROOT::Tree ROOT::TreePlayer ROOT::Hist
    ROOT::Graf ROOT::Gpad ROOT::ROOTDataFrame ROOT::Matrix)
target_link_libraries(phsp_core PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(phsp_core PRIVATE PHSP_VERSION="${PROJECT_VERSION}")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(phsp_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
install(TARGETS phsp_core ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/phsp DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES vendor/iaea/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/phsp_tools/licenses RENAME IAEA.txt)
install(FILES vendor/legacy/LICENSE.phspCutter DESTINATION ${CMAKE_INSTALL_DATADIR}/phsp_tools/licenses)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/phsp_tools/licenses RENAME PHSP_Tools.txt)
install(FILES ${PROJECT_SOURCE_DIR}/CITATION.cff DESTINATION ${CMAKE_INSTALL_DATADIR}/phsp_tools)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
