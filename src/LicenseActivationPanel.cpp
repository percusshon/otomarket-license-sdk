#include <otomarket/license/LicenseActivationPanel.h>

#include <exception>
#include <utility>

namespace otomarket::license {
namespace {

LicenseActivationPanelOptions makeOptions(
  std::string productId,
  LicenseActivationStrings strings = englishLicenseActivationStrings()
) {
  LicenseActivationPanelOptions options;
  options.productId = std::move(productId);
  options.strings = std::move(strings);
  return options;
}

} // namespace

struct LicenseActivationPanel::OperationResult {
  Operation operation = Operation::Refresh;
  bool ok = false;
  LicenseState state = LicenseState::Unknown;
  ErrorCode error = ErrorCode::None;
  std::string errorMessage;
  std::optional<LicenseInfo> license;
  std::optional<int> seatsUsed;
  std::optional<int> maxActivations;
  std::optional<std::chrono::system_clock::time_point> expiresAt;
};

class LicenseActivationPanel::OperationJob final : public juce::ThreadPoolJob {
public:
  OperationJob(
    LicenseActivationPanel& panel,
    Client& client,
    Operation operation,
    std::string productId,
    std::string licenseKey,
    std::string machineName
  )
    : juce::ThreadPoolJob("OtoMarket license operation"),
      safePanel_(&panel),
      client_(client),
      operation_(operation),
      productId_(std::move(productId)),
      licenseKey_(std::move(licenseKey)),
      machineName_(std::move(machineName)) {}

  JobStatus runJob() override {
    OperationResult result;
    result.operation = operation_;

    try {
      if (operation_ == Operation::Activate || operation_ == Operation::StartTrial) {
        const ActivateResult activated = operation_ == Operation::StartTrial
          ? client_.otoStartTrial(productId_)
          : client_.otoActivate(productId_, licenseKey_, machineName_);
        result.ok = activated.ok;
        result.error = activated.error;
        result.errorMessage = activated.errorMessage;
        result.seatsUsed = activated.seatsUsed;
        if (activated.maxActivations > 0) {
          result.maxActivations = activated.maxActivations;
        }
        result.expiresAt = activated.expiresAt;
      } else if (operation_ == Operation::Deactivate) {
        const DeactivateResult deactivated = client_.otoDeactivate();
        result.ok = deactivated.ok;
        result.error = deactivated.error;
        result.errorMessage = deactivated.errorMessage;
        result.seatsUsed = deactivated.seatsUsed;
      } else {
        result.ok = client_.otoIsLicensed();
        result.error = client_.lastError();
      }

      result.state = client_.state();
      result.license = client_.cachedLicense();
      if (!result.maxActivations && result.license && result.license->maxActivations > 0) {
        result.maxActivations = result.license->maxActivations;
      }
      if (!result.expiresAt && result.license && result.license->expiresAt) {
        result.expiresAt = result.license->expiresAt;
      }
    } catch (const std::exception& exception) {
      result.ok = false;
      result.state = LicenseState::Error;
      result.error = ErrorCode::NetworkError;
      result.errorMessage = exception.what();
    } catch (...) {
      result.ok = false;
      result.state = LicenseState::Error;
      result.error = ErrorCode::NetworkError;
      result.errorMessage = "License operation failed.";
    }

    const auto safePanel = safePanel_;
    juce::MessageManager::callAsync([safePanel, result] {
      if (auto* panel = safePanel.getComponent()) {
        panel->finishOperation(result);
      }
    });

    return jobHasFinished;
  }

private:
  juce::Component::SafePointer<LicenseActivationPanel> safePanel_;
  Client& client_;
  Operation operation_ = Operation::Refresh;
  std::string productId_;
  std::string licenseKey_;
  std::string machineName_;
};

LicenseActivationPanel::LicenseActivationPanel(Client& client, std::string productId)
  : LicenseActivationPanel(client, makeOptions(std::move(productId))) {}

LicenseActivationPanel::LicenseActivationPanel(
  Client& client,
  std::string productId,
  LicenseActivationStrings strings
)
  : LicenseActivationPanel(client, makeOptions(std::move(productId), std::move(strings))) {}

LicenseActivationPanel::LicenseActivationPanel(Client& client, LicenseActivationPanelOptions options)
  : client_(client),
    options_(std::move(options)),
    threadPool_(1) {
  licenseKeyLabel_.setText(options_.strings.licenseKeyLabel, juce::dontSendNotification);
  licenseKeyEditor_.setTextToShowWhenEmpty(
    juce::String(options_.strings.licenseKeyPlaceholder),
    juce::Colours::grey
  );
  licenseKeyEditor_.addListener(this);

  activateButton_.setButtonText(options_.strings.activateButton);
  activateButton_.addListener(this);
  trialButton_.setButtonText(options_.strings.trialButton);
  trialButton_.addListener(this);
  deactivateButton_.setButtonText(options_.strings.deactivateButton);
  deactivateButton_.addListener(this);

  statusLabel_.setJustificationType(juce::Justification::centredLeft);
  detailsLabel_.setJustificationType(juce::Justification::topLeft);
  errorLabel_.setJustificationType(juce::Justification::topLeft);
  errorLabel_.setColour(juce::Label::textColourId, juce::Colours::red);

  addAndMakeVisible(licenseKeyLabel_);
  addAndMakeVisible(licenseKeyEditor_);
  addAndMakeVisible(activateButton_);
  if (options_.enableTrial) {
    addAndMakeVisible(trialButton_);
  } else {
    trialButton_.setVisible(false);
  }
  addAndMakeVisible(deactivateButton_);
  addAndMakeVisible(statusLabel_);
  addAndMakeVisible(detailsLabel_);
  addAndMakeVisible(errorLabel_);

  refreshFromClient();
  if (options_.checkOnConstruct) {
    refreshAsync();
  }
}

LicenseActivationPanel::~LicenseActivationPanel() {
  threadPool_.removeAllJobs(true, 10000);
  activateButton_.removeListener(this);
  trialButton_.removeListener(this);
  deactivateButton_.removeListener(this);
  licenseKeyEditor_.removeListener(this);
}

void LicenseActivationPanel::setStrings(LicenseActivationStrings strings) {
  options_.strings = std::move(strings);
  licenseKeyLabel_.setText(options_.strings.licenseKeyLabel, juce::dontSendNotification);
  licenseKeyEditor_.setTextToShowWhenEmpty(
    juce::String(options_.strings.licenseKeyPlaceholder),
    juce::Colours::grey
  );
  activateButton_.setButtonText(options_.strings.activateButton);
  trialButton_.setButtonText(options_.strings.trialButton);
  deactivateButton_.setButtonText(options_.strings.deactivateButton);
  applyCurrentView();
}

void LicenseActivationPanel::refreshFromClient() {
  displayedState_ = client_.state();
  displayedError_ = client_.lastError();
  displayedErrorMessage_.clear();
  displayedLicense_ = client_.cachedLicense();
  displayedSeatsUsed_.reset();
  displayedMaxActivations_.reset();
  displayedExpiresAt_.reset();

  if (displayedLicense_) {
    displayedMaxActivations_ = displayedLicense_->maxActivations;
    displayedExpiresAt_ = displayedLicense_->expiresAt;
  }

  applyCurrentView();
  notifyLicenseStateChangedIfNeeded();
}

void LicenseActivationPanel::refreshAsync() {
  startOperation(Operation::Refresh);
}

void LicenseActivationPanel::resized() {
  auto area = getLocalBounds().reduced(12);
  licenseKeyLabel_.setBounds(area.removeFromTop(22));

  auto row = area.removeFromTop(34);
  const int buttonWidth = options_.enableTrial
    ? juce::jmin(124, juce::jmax(96, row.getWidth() / 5))
    : juce::jmin(124, juce::jmax(96, row.getWidth() / 4));
  deactivateButton_.setBounds(row.removeFromRight(buttonWidth).reduced(4, 2));
  activateButton_.setBounds(row.removeFromRight(buttonWidth).reduced(4, 2));
  if (options_.enableTrial) {
    trialButton_.setBounds(row.removeFromRight(buttonWidth).reduced(4, 2));
  }
  licenseKeyEditor_.setBounds(row.reduced(0, 2));

  area.removeFromTop(8);
  statusLabel_.setBounds(area.removeFromTop(26));
  detailsLabel_.setBounds(area.removeFromTop(54));
  area.removeFromTop(4);
  errorLabel_.setBounds(area);
}

void LicenseActivationPanel::buttonClicked(juce::Button* button) {
  if (button == &activateButton_) {
    startOperation(Operation::Activate);
  } else if (button == &trialButton_ && options_.enableTrial) {
    startOperation(Operation::StartTrial);
  } else if (button == &deactivateButton_) {
    startOperation(Operation::Deactivate);
  }
}

void LicenseActivationPanel::textEditorTextChanged(juce::TextEditor& editor) {
  if (&editor == &licenseKeyEditor_) {
    applyCurrentView();
  }
}

void LicenseActivationPanel::startOperation(Operation operation) {
  if (busy_) {
    return;
  }

  const std::string licenseKey = licenseKeyEditor_.getText().trim().toStdString();
  if (operation == Operation::Activate && licenseKey.empty()) {
    return;
  }

  busy_ = true;
  busyOperation_ = operation;
  displayedError_ = ErrorCode::None;
  displayedErrorMessage_.clear();
  applyCurrentView();

  threadPool_.addJob(
    new OperationJob(
      *this,
      client_,
      operation,
      options_.productId,
      licenseKey,
      options_.machineName
    ),
    true
  );
}

void LicenseActivationPanel::finishOperation(const OperationResult& result) {
  busy_ = false;
  displayedState_ = result.state;
  displayedError_ = result.error;
  displayedErrorMessage_ = result.errorMessage;
  displayedLicense_ = result.license;
  displayedSeatsUsed_ = result.seatsUsed;
  displayedMaxActivations_ = result.maxActivations;
  displayedExpiresAt_ = result.expiresAt;

  if (result.operation == Operation::Deactivate && result.ok) {
    licenseKeyEditor_.clear();
  }

  applyCurrentView();
  notifyLicenseStateChangedIfNeeded();
}

void LicenseActivationPanel::applyCurrentView() {
  LicenseActivationViewInput input;
  input.state = displayedState_;
  input.error = displayedError_;
  input.errorMessage = displayedErrorMessage_;
  input.license = displayedLicense_;
  input.seatsUsed = displayedSeatsUsed_;
  input.maxActivations = displayedMaxActivations_;
  input.expiresAt = displayedExpiresAt_;
  input.busy = busy_;
  input.busyMessage = busyTextFor(busyOperation_);
  input.hasLicenseKey = !licenseKeyEditor_.getText().trim().isEmpty();

  const LicenseActivationViewModel view = buildLicenseActivationView(input, options_.strings);
  statusLabel_.setText(view.statusText, juce::dontSendNotification);
  detailsLabel_.setText(view.detailsText, juce::dontSendNotification);
  errorLabel_.setText(view.errorText, juce::dontSendNotification);
  activateButton_.setEnabled(view.canActivate);
  const bool hasActiveLicense = displayedState_ == LicenseState::Licensed ||
    displayedState_ == LicenseState::LicensedOfflineGrace ||
    displayedState_ == LicenseState::VerifyDue;
  trialButton_.setEnabled(options_.enableTrial && !busy_ && !hasActiveLicense);
  deactivateButton_.setEnabled(view.canDeactivate);
}

void LicenseActivationPanel::notifyLicenseStateChangedIfNeeded() {
  if (!onLicenseStateChanged || displayedState_ == lastNotifiedState_) {
    return;
  }

  lastNotifiedState_ = displayedState_;
  onLicenseStateChanged(displayedState_);
}

std::string LicenseActivationPanel::busyTextFor(Operation operation) const {
  switch (operation) {
    case Operation::Refresh:
      return options_.strings.checkingStatus;
    case Operation::Activate:
      return options_.strings.activatingStatus;
    case Operation::StartTrial:
      return options_.strings.activatingStatus;
    case Operation::Deactivate:
      return options_.strings.deactivatingStatus;
  }

  return {};
}

} // namespace otomarket::license
