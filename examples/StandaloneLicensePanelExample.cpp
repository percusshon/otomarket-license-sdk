#include <otomarket/license/JuceHttpTransport.h>
#include <otomarket/license/LicenseActivationPanel.h>
#include <otomarket/license/Version.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

namespace license = otomarket::license;

namespace {

std::string envOr(const char* name, std::string fallback = {}) {
  if (const char* value = std::getenv(name)) {
    if (*value != '\0') {
      return value;
    }
  }

  return fallback;
}

std::string machineName() {
  return juce::SystemStats::getComputerName().toStdString();
}

class LicenseService {
public:
  LicenseService()
    : client_(makeConfig()) {}

  license::ActivateResult activate(const std::string& licenseKey) {
    return client_.otoActivate(productKey(), licenseKey, machineName());
  }

  bool isLicensed() {
    return client_.otoIsLicensed();
  }

  license::DeactivateResult deactivate() {
    return client_.otoDeactivate();
  }

  license::Client& client() {
    return client_;
  }

  static std::string productKey() {
    return envOr("OTOMARKET_LICENSE_PRODUCT_KEY", "your-product-key");
  }

private:
  static license::Config makeConfig() {
    license::Config config;
    config.baseUrl = envOr("OTOMARKET_LICENSE_BASE_URL", "https://otomarket.jp/api/license/v1");
    config.publicKeyPem = envOr("OTOMARKET_LICENSE_PUBLIC_KEY");
    config.expectedProductId = productKey();
    config.cachePath = license::defaultCachePath("OtoMarketLicensePanelExample");
    config.http = std::make_shared<license::JuceHttpTransport>();
    return config;
  }

  license::Client client_;
};

license::LicenseActivationPanelOptions panelOptions() {
  license::LicenseActivationPanelOptions options;
  options.productId = LicenseService::productKey();
  options.machineName = machineName();
  options.strings = license::englishLicenseActivationStrings();
  return options;
}

class MainComponent final : public juce::Component {
public:
  MainComponent()
    : activationPanel_(licenses_.client(), panelOptions()) {
    addAndMakeVisible(activationPanel_);
    setSize(560, 220);
  }

  void resized() override {
    activationPanel_.setBounds(getLocalBounds());
  }

private:
  LicenseService licenses_;
  license::LicenseActivationPanel activationPanel_;
};

class MainWindow final : public juce::DocumentWindow {
public:
  explicit MainWindow(juce::String name)
    : juce::DocumentWindow(
        std::move(name),
        juce::Desktop::getInstance().getDefaultLookAndFeel()
          .findColour(juce::ResizableWindow::backgroundColourId),
        juce::DocumentWindow::allButtons
      ) {
    setUsingNativeTitleBar(true);
    setContentOwned(new MainComponent(), true);
    centreWithSize(getWidth(), getHeight());
    setVisible(true);
  }

  void closeButtonPressed() override {
    juce::JUCEApplication::getInstance()->systemRequestedQuit();
  }
};

class ExampleApplication final : public juce::JUCEApplication {
public:
  const juce::String getApplicationName() override {
    return "OtoMarket License Panel Example";
  }

  const juce::String getApplicationVersion() override {
    return OTOMARKET_LICENSE_SDK_VERSION_STRING;
  }

  void initialise(const juce::String&) override {
    mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
  }

  void shutdown() override {
    mainWindow_.reset();
  }

private:
  std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace

START_JUCE_APPLICATION(ExampleApplication)
