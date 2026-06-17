cmake_minimum_required(VERSION 3.20)

project(
  OtoMarketLicenseSdkCpp
  VERSION 0.1.0
  LANGUAGES C CXX
)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

option(OTOMARKET_LICENSE_SDK_BUILD_TESTS "Build OtoMarket license SDK tests" ${PROJECT_IS_TOP_LEVEL})
option(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER "Build the optional JUCE adapter target" OFF)
option(OTOMARKET_LICENSE_SDK_BUILD_ACTIVATION_PANEL "Build the optional JUCE activation panel target" OFF)
option(OTOMARKET_LICENSE_SDK_BUILD_EXAMPLES "Build OtoMarket license SDK examples" OFF)

if(OTOMARKET_LICENSE_SDK_BUILD_EXAMPLES)
  set(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER ON)
  set(OTOMARKET_LICENSE_SDK_BUILD_ACTIVATION_PANEL ON)
endif()

add_library(otomarket_license_sdk STATIC
  src/LicenseActivationUi.cpp
  src/LicenseSdk.cpp
  src/LicenseSnapshot.cpp
  src/tweetnacl_randombytes.cpp
  vendor/tweetnacl/tweetnacl.c
)

add_library(otomarket::license_sdk ALIAS otomarket_license_sdk)
set_target_properties(otomarket_license_sdk PROPERTIES EXPORT_NAME license_sdk)

target_compile_features(otomarket_license_sdk PUBLIC cxx_std_17)
target_include_directories(otomarket_license_sdk
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tweetnacl
)

if(OTOMARKET_LICENSE_SDK_BUILD_JUCE_ADAPTER)
  if(NOT TARGET juce::juce_core)
    find_package(JUCE CONFIG REQUIRED)
  endif()

  add_library(otomarket_license_sdk_juce INTERFACE)
  add_library(otomarket::license_sdk_juce ALIAS otomarket_license_sdk_juce)
  target_link_libraries(otomarket_license_sdk_juce
    INTERFACE
      otomarket_license_sdk
      juce::juce_core
  )
  target_include_directories(otomarket_license_sdk_juce
    INTERFACE
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
endif()

if(OTOMARKET_LICENSE_SDK_BUILD_ACTIVATION_PANEL)
  if(NOT TARGET juce::juce_gui_basics)
    find_package(JUCE CONFIG REQUIRED)
  endif()

  add_library(otomarket_license_sdk_activation_panel STATIC
    src/LicenseActivationPanel.cpp
  )
  add_library(otomarket::license_sdk_activation_panel ALIAS otomarket_license_sdk_activation_panel)

  target_compile_features(otomarket_license_sdk_activation_panel PUBLIC cxx_std_17)
  target_link_libraries(otomarket_license_sdk_activation_panel
    PUBLIC
      otomarket_license_sdk
      juce::juce_gui_basics
  )
  target_include_directories(otomarket_license_sdk_activation_panel
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
  )
endif()

if(OTOMARKET_LICENSE_SDK_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(OTOMARKET_LICENSE_SDK_BUILD_TESTS)
  enable_testing()

  add_executable(otomarket_license_sdk_tests
    tests/license_sdk_tests.cpp
  )
  target_link_libraries(otomarket_license_sdk_tests PRIVATE otomarket_license_sdk)
  target_include_directories(otomarket_license_sdk_tests
    PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tweetnacl
  )

  add_test(NAME otomarket_license_sdk_tests COMMAND otomarket_license_sdk_tests)
endif()

install(TARGETS otomarket_license_sdk
  EXPORT otomarket-license-sdk-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

configure_package_config_file(
  cmake/otomarket-license-sdk-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/otomarket-license-sdk-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/otomarket-license-sdk
)

write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/otomarket-license-sdk-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/otomarket-license-sdk-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/otomarket-license-sdk-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/otomarket-license-sdk
)

install(EXPORT otomarket-license-sdk-targets
  NAMESPACE otomarket::
  FILE otomarket-license-sdk-targets.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/otomarket-license-sdk
)
