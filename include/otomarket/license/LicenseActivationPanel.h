#pragma once

#include <otomarket/license/LicenseActivationUi.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

namespace otomarket::license {

struct LicenseActivationPanelOptions {
  std::string productId;
  std::string machineName;
  LicenseActivationStrings strings = englishLicenseActivationStrings();
  bool checkOnConstruct = true;
};

class LicenseActivationPanel final
  : public juce::Component,
    private juce::Button::Listener,
    private juce::TextEditor::Listener {
public:
  LicenseActivationPanel(Client& client, std::string productId);
  LicenseActivationPanel(
    Client& client,
    std::string productId,
    LicenseActivationStrings strings
  );
  LicenseActivationPanel(Client& client, LicenseActivationPanelOptions options);
  ~LicenseActivationPanel() override;

  void setStrings(LicenseActivationStrings strings);
  void refreshFromClient();
  void refreshAsync();

  void resized() override;

private:
  enum class Operation {
    Refresh,
    Activate,
    Deactivate,
  };

  struct OperationResult;
  class OperationJob;

  void buttonClicked(juce::Button* button) override;
  void textEditorTextChanged(juce::TextEditor& editor) override;
  void startOperation(Operation operation);
  void finishOperation(const OperationResult& result);
  void applyCurrentView();
  std::string busyTextFor(Operation operation) const;

  Client& client_;
  LicenseActivationPanelOptions options_;
  juce::ThreadPool threadPool_;
  juce::Label licenseKeyLabel_;
  juce::TextEditor licenseKeyEditor_;
  juce::TextButton activateButton_;
  juce::TextButton deactivateButton_;
  juce::Label statusLabel_;
  juce::Label detailsLabel_;
  juce::Label errorLabel_;
  LicenseState displayedState_ = LicenseState::Unknown;
  ErrorCode displayedError_ = ErrorCode::None;
  std::string displayedErrorMessage_;
  std::optional<LicenseInfo> displayedLicense_;
  std::optional<int> displayedSeatsUsed_;
  std::optional<int> displayedMaxActivations_;
  std::optional<std::chrono::system_clock::time_point> displayedExpiresAt_;
  bool busy_ = false;
  Operation busyOperation_ = Operation::Refresh;
};

} // namespace otomarket::license
