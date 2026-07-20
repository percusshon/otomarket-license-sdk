if(NOT TARGET juce::juce_gui_extra)
  find_package(JUCE CONFIG REQUIRED)
endif()

juce_add_gui_app(OtoMarketLicensePanelExample
  PRODUCT_NAME "OtoMarket License Panel Example"
)

target_sources(OtoMarketLicensePanelExample
  PRIVATE
    StandaloneLicensePanelExample.cpp
)

target_compile_definitions(OtoMarketLicensePanelExample
  PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(OtoMarketLicensePanelExample
  PRIVATE
    otomarket::license_sdk_activation_panel
    otomarket::license_sdk_juce
    juce::juce_gui_extra
)

juce_add_gui_app(OtoMarketFirstRunGateExample
  PRODUCT_NAME "OtoMarket First Run Gate Example"
)

target_sources(OtoMarketFirstRunGateExample
  PRIVATE
    FirstRunGateExample.cpp
)

target_compile_definitions(OtoMarketFirstRunGateExample
  PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(OtoMarketFirstRunGateExample
  PRIVATE
    otomarket::license_sdk_activation_panel
    otomarket::license_sdk_juce
    juce::juce_gui_extra
)
