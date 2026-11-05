add_executable(phsp_integration integration.cpp)
target_link_libraries(phsp_integration PRIVATE phsp::core nlohmann_json::nlohmann_json)
add_test(NAME phsp_integration COMMAND phsp_integration
    $<TARGET_FILE:phspToRootMT> $<TARGET_FILE:phspAnalyze> $<TARGET_FILE:phspCompare>
    $<TARGET_FILE:phspPipeline> ${CMAKE_CURRENT_BINARY_DIR}/test-output)
set_tests_properties(phsp_integration PROPERTIES TIMEOUT 240)
add_executable(phsp_logging_test logging.cpp)
target_link_libraries(phsp_logging_test PRIVATE phsp::core)
add_test(NAME phsp_logging COMMAND phsp_logging_test ${CMAKE_CURRENT_BINARY_DIR}/logging-test.log)
add_executable(phsp_features_test features.cpp)
target_link_libraries(phsp_features_test PRIVATE phsp::core nlohmann_json::nlohmann_json)
add_test(NAME phsp_features COMMAND phsp_features_test ${CMAKE_CURRENT_BINARY_DIR}/features-output)
set_tests_properties(phsp_features PROPERTIES TIMEOUT 180)
