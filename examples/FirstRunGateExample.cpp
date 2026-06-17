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

  bool isLicensed() {
    return client_.otoIsLicensed();
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
    config.cachePath = license::defaultCachePath("OtoMarketFirstRunGateExample");
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
  // 商品がトライアル可のときだけ有効にする。
  options.enableTrial = true;
  return options;
}

class AppRoot final : public juce::Component {
public:
  AppRoot() {
    setSize(560, 320);

    if (licenses_.isLicensed()) {
      showMainUi();
    } else {
      showGate();
    }
  }

  void paint(juce::Graphics& graphics) override {
    graphics.fillAll(juce::Colour(0xff15181d));
    // ここに製品ロゴ/ブランドを描く（背景・配色は host 側）。
  }

  void resized() override {
    if (activationPanel_) {
      activationPanel_->setBounds(getLocalBounds());
    }

    if (mainLabel_) {
      mainLabel_->setBounds(getLocalBounds());
    }
  }

private:
  void showGate() {
    if (mainLabel_) {
      removeChildComponent(mainLabel_.get());
      mainLabel_.reset();
    }

    if (!activationPanel_) {
      activationPanel_ = std::make_unique<license::LicenseActivationPanel>(
        licenses_.client(),
        panelOptions()
      );
      activationPanel_->onLicenseStateChanged = [this](license::LicenseState state) {
        if (state == license::LicenseState::Licensed
            || state == license::LicenseState::LicensedOfflineGrace) {
          showMainUi();
        } else {
          showGate();
        }
      };
      addAndMakeVisible(*activationPanel_);
    }

    resized();
    repaint();
  }

  void showMainUi() {
    if (activationPanel_) {
      removeChildComponent(activationPanel_.get());
      activationPanel_.reset();
    }

    if (!mainLabel_) {
      mainLabel_ = std::make_unique<juce::Label>();
      mainLabel_->setText("Main UI", juce::dontSendNotification);
      mainLabel_->setJustificationType(juce::Justification::centred);
      mainLabel_->setColour(juce::Label::textColourId, juce::Colours::white);
      addAndMakeVisible(*mainLabel_);
    }

    resized();
    repaint();
  }

  LicenseService licenses_;
  std::unique_ptr<license::LicenseActivationPanel> activationPanel_;
  std::unique_ptr<juce::Label> mainLabel_;
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
    setContentOwned(new AppRoot(), true);
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
    return "OtoMarket First Run Gate Example";
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
