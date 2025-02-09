#include "preferences/dialog/dlgprefdeck.h"

#include <QDir>
#include <QDoubleSpinBox>
#include <QList>
#include <QLocale>
#include <QToolTip>
#include <QWidget>

#include "control/controlobject.h"
#include "control/controlproxy.h"
#include "defs_urls.h"
#include "engine/controls/ratecontrol.h"
#include "engine/enginebuffer.h"
#include "mixer/basetrackplayer.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_dlgprefdeck.cpp"
#include "preferences/usersettings.h"
#include "util/duration.h"
#include "widget/wnumberpos.h"

namespace {
constexpr int kDefaultRateRangePercent = 8;
constexpr double kRateDirectionInverted = -1;
constexpr RateControl::RampMode kDefaultRampingMode = RateControl::RampMode::Stepping;
constexpr double kDefaultTemporaryRateChangeCoarse = 4.00; // percent
constexpr double kDefaultTemporaryRateChangeFine = 2.00;
constexpr double kDefaultPermanentRateChangeCoarse = 0.50;
constexpr double kDefaultPermanentRateChangeFine = 0.05;
constexpr int kDefaultRateRampSensitivity = 250;
// bool kDefaultCloneDeckOnLoad is defined in header file to make it available
// to playermanager.cpp
} // namespace

DlgPrefDeck::DlgPrefDeck(QWidget* parent,
        UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_pConfig(pConfig),
          m_pControlTrackTimeDisplay(std::make_unique<ControlObject>(
                  ConfigKey("[Controls]", "ShowDurationRemaining"))),
          m_pControlTrackTimeFormat(std::make_unique<ControlObject>(
                  ConfigKey("[Controls]", "TimeFormat"))),
          m_pNumDecks(
                  make_parented<ControlProxy>("[Master]", "num_decks", this)),
          m_pNumSamplers(make_parented<ControlProxy>(
                  "[Master]", "num_samplers", this)),
          m_iNumConfiguredDecks(0),
          m_iNumConfiguredSamplers(0) {
    setupUi(this);
    // Create text color for the cue mode link "?" to the manual
    createLinkColor();

    m_pNumDecks->connectValueChanged(this, [=, this](double value) { slotNumDecksChanged(value); });
    slotNumDecksChanged(m_pNumDecks->get(), true);

    m_pNumSamplers->connectValueChanged(
            this, [=, this](double value) { slotNumSamplersChanged(value); });
    slotNumSamplersChanged(m_pNumSamplers->get(), true);

    // Set default value in config file and control objects, if not present
    // Default is "0" = Mixxx Mode
    int cueDefaultValue = m_pConfig->getValue(
            ConfigKey("[Controls]", "CueDefault"), 0);

    // Update combo box
    ComboBoxCueMode->addItem(tr("Mixxx mode"), static_cast<int>(CueMode::Mixxx));
    ComboBoxCueMode->addItem(tr("Mixxx mode (no blinking)"), static_cast<int>(CueMode::MixxxNoBlinking));
    ComboBoxCueMode->addItem(tr("Pioneer mode"), static_cast<int>(CueMode::Pioneer));
    ComboBoxCueMode->addItem(tr("Denon mode"), static_cast<int>(CueMode::Denon));
    ComboBoxCueMode->addItem(tr("Numark mode"), static_cast<int>(CueMode::Numark));
    ComboBoxCueMode->addItem(tr("CUP mode"), static_cast<int>(CueMode::CueAndPlay));
    const int cueModeIndex = cueDefaultIndexByData(cueDefaultValue);
    ComboBoxCueMode->setCurrentIndex(cueModeIndex);
    slotCueModeCombobox(cueModeIndex);
    for (ControlProxy* pControl : qAsConst(m_cueControls)) {
        pControl->set(static_cast<int>(m_cueMode));
    }
    connect(ComboBoxCueMode,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::slotCueModeCombobox);

    // Track time display configuration
    connect(m_pControlTrackTimeDisplay.get(),
            &ControlObject::valueChanged,
            this,
            QOverload<double>::of(&DlgPrefDeck::slotSetTrackTimeDisplay));

    double positionDisplayType = m_pConfig->getValue(
            ConfigKey("[Controls]", "PositionDisplay"),
            static_cast<double>(TrackTime::DisplayMode::ELAPSED_AND_REMAINING));
    if (positionDisplayType ==
            static_cast<double>(TrackTime::DisplayMode::REMAINING)) {
        radioButtonRemaining->setChecked(true);
        m_pControlTrackTimeDisplay->set(
            static_cast<double>(TrackTime::DisplayMode::REMAINING));
    } else if (positionDisplayType ==
                   static_cast<double>(TrackTime::DisplayMode::ELAPSED_AND_REMAINING)) {
        radioButtonElapsedAndRemaining->setChecked(true);
        m_pControlTrackTimeDisplay->set(
            static_cast<double>(TrackTime::DisplayMode::ELAPSED_AND_REMAINING));
    } else {
        radioButtonElapsed->setChecked(true);
        m_pControlTrackTimeDisplay->set(
            static_cast<double>(TrackTime::DisplayMode::ELAPSED));
    }
    connect(buttonGroupTrackTime,
            QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this,
            QOverload<QAbstractButton*>::of(
                    &DlgPrefDeck::slotSetTrackTimeDisplay));

    // display time format
    connect(m_pControlTrackTimeFormat.get(),
            &ControlObject::valueChanged,
            this,
            &DlgPrefDeck::slotTimeFormatChanged);

    // Track Display model
    comboBoxTimeFormat->clear();

    comboBoxTimeFormat->addItem(tr("mm:ss%1zz - Traditional")
                                .arg(mixxx::DurationBase::kDecimalSeparator),
                                static_cast<int>
                                (TrackTime::DisplayFormat::TRADITIONAL));

    comboBoxTimeFormat->addItem(tr("mm:ss - Traditional (Coarse)"),
                                static_cast<int>
                                (TrackTime::DisplayFormat::TRADITIONAL_COARSE));

    comboBoxTimeFormat->addItem(tr("s%1zz - Seconds")
                                .arg(mixxx::DurationBase::kDecimalSeparator),
                                static_cast<int>
                                (TrackTime::DisplayFormat::SECONDS));

    comboBoxTimeFormat->addItem(tr("sss%1zz - Seconds (Long)")
                                .arg(mixxx::DurationBase::kDecimalSeparator),
                                static_cast<int>
                                (TrackTime::DisplayFormat::SECONDS_LONG));

    comboBoxTimeFormat->addItem(tr("s%1sss%2zz - Kiloseconds")
                                .arg(QString(mixxx::DurationBase::kDecimalSeparator),
                                     QString(mixxx::DurationBase::kKiloGroupSeparator)),
                                static_cast<int>
                                (TrackTime::DisplayFormat::KILO_SECONDS));

    double time_format = static_cast<double>(
                                            m_pConfig->getValue(
                                            ConfigKey("[Controls]", "TimeFormat"),
                                            static_cast<int>(TrackTime::DisplayFormat::TRADITIONAL)));
    m_pControlTrackTimeFormat->set(time_format);
    comboBoxTimeFormat->setCurrentIndex(
                comboBoxTimeFormat->findData(time_format));

    // Override Playing Track on Track Load
    // The check box reflects the opposite of the config value
    m_bDisallowTrackLoadToPlayingDeck = !m_pConfig->getValue(
            ConfigKey("[Controls]", "AllowTrackLoadToPlayingDeck"), false);
    checkBoxDisallowLoadToPlayingDeck->setChecked(m_bDisallowTrackLoadToPlayingDeck);
    connect(checkBoxDisallowLoadToPlayingDeck,
            &QCheckBox::toggled,
            this,
            &DlgPrefDeck::slotDisallowTrackLoadToPlayingDeckCheckbox);

    comboBoxLoadPoint->addItem(tr("Intro start"), static_cast<int>(SeekOnLoadMode::IntroStart));
    comboBoxLoadPoint->addItem(tr("Main cue"), static_cast<int>(SeekOnLoadMode::MainCue));
    comboBoxLoadPoint->addItem(tr("First sound (skip silence)"), static_cast<int>(SeekOnLoadMode::FirstSound));
    comboBoxLoadPoint->addItem(tr("Beginning of track"), static_cast<int>(SeekOnLoadMode::Beginning));
    bool seekModeExisted = m_pConfig->exists(ConfigKey("[Controls]", "CueRecall"));
    int seekMode = m_pConfig->getValue(ConfigKey("[Controls]", "CueRecall"),
            static_cast<int>(SeekOnLoadMode::IntroStart));
    comboBoxLoadPoint->setCurrentIndex(
            comboBoxLoadPoint->findData(seekMode));
    m_seekOnLoadMode = static_cast<SeekOnLoadMode>(seekMode);
    connect(comboBoxLoadPoint,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::slotSetTrackLoadMode);

    // This option was introduced in Mixxx 2.3 with the intro & outro cues.
    // If the user has set main cue points with the intention of starting tracks
    // from those points, enable this option. With Denon and Numark CueModes,
    // it is not safe to assume that the user wants to start tracks from the
    // main cue point because it is very easy to move the main cue point without
    // explicitly intending to in those modes (the main cue point moves whenever
    // the deck is not at the main cue point and play is pressed).
    bool introStartMoveDefault = (m_seekOnLoadMode == SeekOnLoadMode::MainCue ||
                                         !seekModeExisted) &&
            !(m_cueMode == CueMode::Denon ||
                    m_cueMode == CueMode::Numark);
    m_bSetIntroStartAtMainCue = m_pConfig->getValue(ConfigKey("[Controls]", "SetIntroStartAtMainCue"),
            introStartMoveDefault);
    // This is an ugly hack to ensure AnalyzerSilence gets the correct default
    // value because ConfigValue::getValue does not set the value of the ConfigValue
    // in case no value had been set previously (when mixxx.cfg is empty).
    m_pConfig->setValue(ConfigKey("[Controls]", "SetIntroStartAtMainCue"), m_bSetIntroStartAtMainCue);
    checkBoxIntroStartMove->setChecked(m_bSetIntroStartAtMainCue);
    connect(checkBoxIntroStartMove,
            &QCheckBox::toggled,
            this,
            &DlgPrefDeck::slotMoveIntroStartCheckbox);

    // Double-tap Load to clone a deck via keyboard or controller ([ChannelN],LoadSelectedTrack)
    m_bCloneDeckOnLoadDoubleTap = m_pConfig->getValue(
            ConfigKey("[Controls]", "CloneDeckOnLoadDoubleTap"), true);
    checkBoxCloneDeckOnLoadDoubleTap->setChecked(m_bCloneDeckOnLoadDoubleTap);
    connect(checkBoxCloneDeckOnLoadDoubleTap,
            &QCheckBox::toggled,
            this,
            &DlgPrefDeck::slotCloneDeckOnLoadDoubleTapCheckbox);

    m_bRateDownIncreasesSpeed = m_pConfig->getValue(ConfigKey("[Controls]", "RateDir"), true);
    setRateDirectionForAllDecks(m_bRateDownIncreasesSpeed);
    checkBoxInvertSpeedSlider->setChecked(m_bRateDownIncreasesSpeed);
    connect(checkBoxInvertSpeedSlider,
            &QCheckBox::toggled,
            this,
            &DlgPrefDeck::slotRateInversionCheckbox);

    ComboBoxRateRange->clear();
    ComboBoxRateRange->addItem(tr("4%"), 4);
    ComboBoxRateRange->addItem(tr("6% (semitone)"), 6);
    ComboBoxRateRange->addItem(tr("8% (Technics SL-1210)"), 8);
    ComboBoxRateRange->addItem(tr("10%"), 10);
    ComboBoxRateRange->addItem(tr("16%"), 16);
    ComboBoxRateRange->addItem(tr("24%"), 24);
    ComboBoxRateRange->addItem(tr("50%"), 50);
    ComboBoxRateRange->addItem(tr("90%"), 90);
    connect(ComboBoxRateRange,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefDeck::slotRateRangeComboBox);

    // RateRange is the legacy ConfigKey. RateRangePercent is used now.
    if (m_pConfig->exists(ConfigKey("[Controls]", "RateRange")) &&
        !m_pConfig->exists(ConfigKey("[Controls]", "RateRangePercent"))) {
        int legacyIndex = m_pConfig->getValueString(ConfigKey("[Controls]", "RateRange")).toInt();
        if (legacyIndex == 0) {
            m_iRateRangePercent = 6;
        } else if (legacyIndex == 1) {
            m_iRateRangePercent = 8;
        } else {
            m_iRateRangePercent = (legacyIndex-1) * 10;
        }
    } else {
        m_iRateRangePercent = m_pConfig->getValue(ConfigKey("[Controls]", "RateRangePercent"),
                                                  kDefaultRateRangePercent);
    }
    if (!(m_iRateRangePercent > 0 && m_iRateRangePercent <= 90)) {
        m_iRateRangePercent = kDefaultRateRangePercent;
    }
    setRateRangeForAllDecks(m_iRateRangePercent);

    //
    // Key lock mode
    //
    connect(buttonGroupKeyLockMode,
            QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this,
            &DlgPrefDeck::slotKeyLockModeSelected);

    m_keylockMode = static_cast<KeylockMode>(
        m_pConfig->getValue(ConfigKey("[Controls]", "keylockMode"),
                            static_cast<int>(KeylockMode::LockOriginalKey)));
    for (ControlProxy* pControl : qAsConst(m_keylockModeControls)) {
        pControl->set(static_cast<double>(m_keylockMode));
    }

    //
    // Key unlock mode
    //
    connect(buttonGroupKeyUnlockMode,
            QOverload<QAbstractButton*>::of(&QButtonGroup::buttonClicked),
            this,
            &DlgPrefDeck::slotKeyUnlockModeSelected);

    m_keyunlockMode = static_cast<KeyunlockMode>(
        m_pConfig->getValue(ConfigKey("[Controls]", "keyunlockMode"),
        static_cast<int>(KeyunlockMode::ResetLockedKey)));
    for (ControlProxy* pControl : qAsConst(m_keyunlockModeControls)) {
        pControl->set(static_cast<int>(m_keyunlockMode));
    }

    //
    // Cue Mode
    //

    // Add "(?)" with a manual link to the label
    labelCueMode->setText(labelCueMode->text() + QStringLiteral(" ") +
            coloredLinkString(
                    m_pLinkColor,
                    QStringLiteral("(?)"),
                    MIXXX_MANUAL_CUE_MODES_URL));

    //
    // Speed / Pitch reset configuration
    //

    // Update "reset speed" and "reset pitch" check boxes
    // TODO: All defaults should only be set in slotResetToDefaults.
    int configSPAutoReset = m_pConfig->getValue<int>(
                    ConfigKey("[Controls]", "SpeedAutoReset"),
                    BaseTrackPlayer::RESET_PITCH);

    m_speedAutoReset = (configSPAutoReset==BaseTrackPlayer::RESET_SPEED ||
                        configSPAutoReset==BaseTrackPlayer::RESET_PITCH_AND_SPEED);
    m_pitchAutoReset = (configSPAutoReset==BaseTrackPlayer::RESET_PITCH ||
                        configSPAutoReset==BaseTrackPlayer::RESET_PITCH_AND_SPEED);

    checkBoxResetSpeed->setChecked(m_speedAutoReset);
    checkBoxResetPitch->setChecked(m_pitchAutoReset);

    connect(checkBoxResetSpeed, &QCheckBox::toggled, this, &DlgPrefDeck::slotUpdateSpeedAutoReset);
    connect(checkBoxResetPitch, &QCheckBox::toggled, this, &DlgPrefDeck::slotUpdatePitchAutoReset);

    //
    // Ramping Temporary Rate Change configuration
    //

    // Rate Ramp Sensitivity slider & spinbox
    connect(SliderRateRampSensitivity,
            QOverload<int>::of(&QAbstractSlider::valueChanged),
            SpinBoxRateRampSensitivity,
            QOverload<int>::of(&QSpinBox::setValue));
    connect(SpinBoxRateRampSensitivity,
            QOverload<int>::of(&QSpinBox::valueChanged),
            SliderRateRampSensitivity,
            QOverload<int>::of(&QAbstractSlider::setValue));

    m_iRateRampSensitivity =
            m_pConfig->getValue(ConfigKey("[Controls]", "RateRampSensitivity"),
                    kDefaultRateRampSensitivity);
    SliderRateRampSensitivity->setValue(m_iRateRampSensitivity);
    connect(SliderRateRampSensitivity,
            &QSlider::valueChanged,
            this,
            &DlgPrefDeck::slotRateRampSensitivitySlider);

    // Enable/disable permanent rate spinboxes when smooth ramping is selected
    connect(radioButtonRateRampModeLinear,
            &QRadioButton::toggled,
            labelSpeedRampSensitivity,
            &QWidget::setEnabled);
    connect(radioButtonRateRampModeLinear,
            &QRadioButton::toggled,
            SliderRateRampSensitivity,
            &QWidget::setEnabled);
    connect(radioButtonRateRampModeLinear,
            &QRadioButton::toggled,
            SpinBoxRateRampSensitivity,
            &QWidget::setEnabled);
    // Enable/disable temporary rate spinboxes when abrupt ramping is selected
    connect(radioButtonRateRampModeStepping,
            &QRadioButton::toggled,
            labelSpeedTemporary,
            &QWidget::setEnabled);
    connect(radioButtonRateRampModeStepping,
            &QRadioButton::toggled,
            spinBoxTemporaryRateCoarse,
            &QWidget::setEnabled);
    connect(radioButtonRateRampModeStepping,
            &QRadioButton::toggled,
            spinBoxTemporaryRateFine,
            &QWidget::setEnabled);
    // Set Ramp Rate On or Off
    connect(radioButtonRateRampModeLinear,
            &QRadioButton::toggled,
            this,
            &DlgPrefDeck::slotRateRampingModeLinearButton);
    m_bRateRamping = static_cast<RateControl::RampMode>(
            m_pConfig->getValue(ConfigKey("[Controls]", "RateRamp"),
                    static_cast<int>(kDefaultRampingMode)));
    if (m_bRateRamping == RateControl::RampMode::Linear) {
        radioButtonRateRampModeLinear->setChecked(true);
    } else {
        radioButtonRateRampModeStepping->setChecked(true);
    }

    // Rate buttons configuration
    connect(spinBoxTemporaryRateCoarse,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &DlgPrefDeck::slotRateTempCoarseSpinbox);
    connect(spinBoxTemporaryRateFine,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &DlgPrefDeck::slotRateTempFineSpinbox);
    connect(spinBoxPermanentRateCoarse,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &DlgPrefDeck::slotRatePermCoarseSpinbox);
    connect(spinBoxPermanentRateFine,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            &DlgPrefDeck::slotRatePermFineSpinbox);

    m_dRateTempCoarse = m_pConfig->getValue(ConfigKey("[Controls]", "RateTempLeft"),
            kDefaultTemporaryRateChangeCoarse);
    m_dRateTempFine = m_pConfig->getValue(ConfigKey("[Controls]", "RateTempRight"),
            kDefaultTemporaryRateChangeFine);
    m_dRatePermCoarse = m_pConfig->getValue(ConfigKey("[Controls]", "RatePermLeft"),
            kDefaultPermanentRateChangeCoarse);
    m_dRatePermFine = m_pConfig->getValue(ConfigKey("[Controls]", "RatePermRight"),
            kDefaultPermanentRateChangeFine);

    spinBoxTemporaryRateCoarse->setValue(m_dRateTempCoarse);
    spinBoxTemporaryRateFine->setValue(m_dRateTempFine);
    spinBoxPermanentRateCoarse->setValue(m_dRatePermCoarse);
    spinBoxPermanentRateFine->setValue(m_dRatePermFine);

    RateControl::setTemporaryRateChangeCoarseAmount(m_dRateTempCoarse);
    RateControl::setTemporaryRateChangeFineAmount(m_dRateTempFine);
    RateControl::setPermanentRateChangeCoarseAmount(m_dRatePermCoarse);
    RateControl::setPermanentRateChangeFineAmount(m_dRatePermFine);

    slotUpdate();
}

DlgPrefDeck::~DlgPrefDeck() {
    qDeleteAll(m_rateControls);
    qDeleteAll(m_rateDirectionControls);
    qDeleteAll(m_cueControls);
    qDeleteAll(m_rateRangeControls);
    qDeleteAll(m_keylockModeControls);
    qDeleteAll(m_keyunlockModeControls);
}

void DlgPrefDeck::slotUpdate() {
    checkBoxIntroStartMove->setChecked(m_pConfig->getValue(
            ConfigKey("[Controls]", "SetIntroStartAtMainCue"), false));

    slotSetTrackTimeDisplay(m_pControlTrackTimeDisplay->get());

    checkBoxDisallowLoadToPlayingDeck->setChecked(!m_pConfig->getValue(
            ConfigKey("[Controls]", "AllowTrackLoadToPlayingDeck"), false));

    checkBoxCloneDeckOnLoadDoubleTap->setChecked(m_pConfig->getValue(
            ConfigKey("[Controls]", "CloneDeckOnLoadDoubleTap"), true));

    double rateRange = m_rateRangeControls[0]->get();
    int index = ComboBoxRateRange->findData(static_cast<int>(rateRange * 100.0));
    if (index == -1) {
        ComboBoxRateRange->addItem(QString::number(rateRange * 100.).append("%"),
                rateRange * 100.);
    }
    ComboBoxRateRange->setCurrentIndex(index);

    double rateDirection = m_rateDirectionControls[0]->get();
    checkBoxInvertSpeedSlider->setChecked(rateDirection == kRateDirectionInverted);

    double cueMode = m_cueControls[0]->get();
    index = ComboBoxCueMode->findData(static_cast<int>(cueMode));
    ComboBoxCueMode->setCurrentIndex(index);

    KeylockMode keylockMode =
            static_cast<KeylockMode>(static_cast<int>(m_keylockModeControls[0]->get()));
    if (keylockMode == KeylockMode::LockCurrentKey) {
        radioButtonCurrentKey->setChecked(true);
    } else {
        radioButtonOriginalKey->setChecked(true);
    }

    KeyunlockMode keyunlockMode =
            static_cast<KeyunlockMode>(static_cast<int>(m_keyunlockModeControls[0]->get()));
    if (keyunlockMode == KeyunlockMode::KeepLockedKey) {
        radioButtonKeepUnlockedKey->setChecked(true);
    } else {
        radioButtonResetUnlockedKey->setChecked(true);
    }

    int reset = m_pConfig->getValue(ConfigKey("[Controls]", "SpeedAutoReset"),
        static_cast<int>(BaseTrackPlayer::RESET_PITCH));
    if (reset == BaseTrackPlayer::RESET_PITCH) {
        checkBoxResetPitch->setChecked(true);
        checkBoxResetSpeed->setChecked(false);
    } else if (reset == BaseTrackPlayer::RESET_SPEED) {
        checkBoxResetPitch->setChecked(false);
        checkBoxResetSpeed->setChecked(true);
    } else if (reset == BaseTrackPlayer::RESET_PITCH_AND_SPEED) {
        checkBoxResetPitch->setChecked(true);
        checkBoxResetSpeed->setChecked(true);
    } else if (reset == BaseTrackPlayer::RESET_NONE) {
        checkBoxResetPitch->setChecked(false);
        checkBoxResetSpeed->setChecked(false);
    }

    if (m_bRateRamping == RateControl::RampMode::Linear) {
        radioButtonRateRampModeLinear->setChecked(true);
    } else {
        radioButtonRateRampModeStepping->setChecked(true);
    }

    SliderRateRampSensitivity->setValue(
        m_pConfig->getValue(ConfigKey("[Controls]", "RateRampSensitivity"),
                            kDefaultRateRampSensitivity));

    spinBoxTemporaryRateCoarse->setValue(RateControl::getTemporaryRateChangeCoarseAmount());
    spinBoxTemporaryRateFine->setValue(RateControl::getTemporaryRateChangeFineAmount());
    spinBoxPermanentRateCoarse->setValue(RateControl::getPermanentRateChangeCoarseAmount());
    spinBoxPermanentRateFine->setValue(RateControl::getPermanentRateChangeFineAmount());
}

void DlgPrefDeck::slotResetToDefaults() {
    // Track time display mode
    radioButtonRemaining->setChecked(true);

    // Up increases speed.
    checkBoxInvertSpeedSlider->setChecked(false);

    // 8% Rate Range
    ComboBoxRateRange->setCurrentIndex(ComboBoxRateRange->findData(kDefaultRateRangePercent));

    // Don't load tracks into playing decks.
    checkBoxDisallowLoadToPlayingDeck->setChecked(true);

    // Clone decks by double-tapping Load button.
    checkBoxCloneDeckOnLoadDoubleTap->setChecked(kDefaultCloneDeckOnLoad);
    // Mixxx cue mode
    ComboBoxCueMode->setCurrentIndex(0);

    // Load at intro start
    comboBoxLoadPoint->setCurrentIndex(
            comboBoxLoadPoint->findData(static_cast<int>(SeekOnLoadMode::IntroStart)));

    // Rate-ramping default off.
    radioButtonRateRampModeStepping->setChecked(true);

    SliderRateRampSensitivity->setValue(kDefaultRateRampSensitivity);

    // Permanent and temporary pitch adjust fine/coarse.
    spinBoxTemporaryRateCoarse->setValue(4.0);
    spinBoxTemporaryRateFine->setValue(2.0);
    spinBoxPermanentRateCoarse->setValue(0.50);
    spinBoxPermanentRateFine->setValue(0.05);

    checkBoxResetSpeed->setChecked(false);
    checkBoxResetPitch->setChecked(true);

    radioButtonOriginalKey->setChecked(true);
    radioButtonResetUnlockedKey->setChecked(true);
}

void DlgPrefDeck::slotMoveIntroStartCheckbox(bool checked) {
    m_bSetIntroStartAtMainCue = checked;
}

void DlgPrefDeck::slotRateRangeComboBox(int index) {
    m_iRateRangePercent = ComboBoxRateRange->itemData(index).toInt();
}

void DlgPrefDeck::setRateRangeForAllDecks(int rangePercent) {
    for (ControlProxy* pControl : qAsConst(m_rateRangeControls)) {
        pControl->set(rangePercent / 100.0);
    }
}

void DlgPrefDeck::slotRateInversionCheckbox(bool inverted) {
    m_bRateDownIncreasesSpeed = inverted;
}

void DlgPrefDeck::setRateDirectionForAllDecks(bool inverted) {
    double oldRateDirectionMultiplier = m_rateDirectionControls[0]->get();
    double rateDirectionMultiplier = 1.0;
    if (inverted) {
        rateDirectionMultiplier = kRateDirectionInverted;
    }
    for (ControlProxy* pControl : qAsConst(m_rateDirectionControls)) {
        pControl->set(rateDirectionMultiplier);
    }

    // If the rate slider direction setting has changed,
    // multiply the rate by -1 so the current sound does not change.
    if (rateDirectionMultiplier != oldRateDirectionMultiplier) {
        for (ControlProxy* pControl : qAsConst(m_rateControls)) {
            pControl->set(-1 * pControl->get());
        }
    }
}

void DlgPrefDeck::slotKeyLockModeSelected(QAbstractButton* pressedButton) {
    if (pressedButton == radioButtonCurrentKey) {
        m_keylockMode = KeylockMode::LockCurrentKey;
    } else {
        m_keylockMode = KeylockMode::LockOriginalKey;
    }
}

void DlgPrefDeck::slotKeyUnlockModeSelected(QAbstractButton* pressedButton) {
    if (pressedButton == radioButtonResetUnlockedKey) {
        m_keyunlockMode = KeyunlockMode::ResetLockedKey;
    } else {
        m_keyunlockMode = KeyunlockMode::KeepLockedKey;
    }
}

void DlgPrefDeck::slotDisallowTrackLoadToPlayingDeckCheckbox(bool checked) {
    m_bDisallowTrackLoadToPlayingDeck = checked;
}

void DlgPrefDeck::slotCueModeCombobox(int index) {
    m_cueMode = static_cast<CueMode>(ComboBoxCueMode->itemData(index).toInt());
}

void DlgPrefDeck::slotCloneDeckOnLoadDoubleTapCheckbox(bool checked) {
    m_bCloneDeckOnLoadDoubleTap = checked;
}

void DlgPrefDeck::slotSetTrackTimeDisplay(QAbstractButton* b) {
    if (b == radioButtonRemaining) {
        m_timeDisplayMode = TrackTime::DisplayMode::REMAINING;
    } else if (b == radioButtonElapsedAndRemaining) {
        m_timeDisplayMode = TrackTime::DisplayMode::ELAPSED_AND_REMAINING;
    } else {
        m_timeDisplayMode = TrackTime::DisplayMode::ELAPSED;
    }
}

void DlgPrefDeck::slotSetTrackTimeDisplay(double v) {
    m_timeDisplayMode = static_cast<TrackTime::DisplayMode>(static_cast<int>(v));
    m_pConfig->set(ConfigKey("[Controls]","PositionDisplay"), ConfigValue(v));
    if (m_timeDisplayMode == TrackTime::DisplayMode::REMAINING) {
        radioButtonRemaining->setChecked(true);
    } else if (m_timeDisplayMode == TrackTime::DisplayMode::ELAPSED_AND_REMAINING) {
        radioButtonElapsedAndRemaining->setChecked(true);
    } else { // Elapsed
        radioButtonElapsed->setChecked(true);
    }
}

void DlgPrefDeck::slotRateTempCoarseSpinbox(double value) {
    m_dRateTempCoarse = value;
}

void DlgPrefDeck::slotRateTempFineSpinbox(double value) {
    m_dRateTempFine = value;
}

void DlgPrefDeck::slotRatePermCoarseSpinbox(double value) {
    m_dRatePermCoarse = value;
}

void DlgPrefDeck::slotRatePermFineSpinbox(double value) {
    m_dRatePermFine = value;
}

void DlgPrefDeck::slotRateRampSensitivitySlider(int value) {
    m_iRateRampSensitivity = value;
}

void DlgPrefDeck::slotRateRampingModeLinearButton(bool checked) {
    if (checked) {
        m_bRateRamping = RateControl::RampMode::Linear;
    } else {
        m_bRateRamping = RateControl::RampMode::Stepping;
    }
}

void DlgPrefDeck::slotTimeFormatChanged(double v) {
    int i = static_cast<int>(v);
    m_pConfig->set(ConfigKey("[Controls]","TimeFormat"), ConfigValue(v));
    comboBoxTimeFormat->setCurrentIndex(
                comboBoxTimeFormat->findData(i));
}

void DlgPrefDeck::slotSetTrackLoadMode(int comboboxIndex) {
    m_seekOnLoadMode = static_cast<SeekOnLoadMode>(
            comboBoxLoadPoint->itemData(comboboxIndex).toInt());
}

void DlgPrefDeck::slotApply() {
    m_pConfig->set(ConfigKey("[Controls]", "SetIntroStartAtMainCue"),
            ConfigValue(m_bSetIntroStartAtMainCue));

    double timeDisplay = static_cast<double>(m_timeDisplayMode);
    m_pConfig->set(ConfigKey("[Controls]","PositionDisplay"), ConfigValue(timeDisplay));
    m_pControlTrackTimeDisplay->set(timeDisplay);

    // time format
    double timeFormat = comboBoxTimeFormat->itemData(comboBoxTimeFormat->currentIndex()).toDouble();
    m_pControlTrackTimeFormat->set(timeFormat);
    m_pConfig->setValue(ConfigKey("[Controls]", "TimeFormat"), timeFormat);

    // Set cue mode for every deck
    for (ControlProxy* pControl : qAsConst(m_cueControls)) {
        pControl->set(static_cast<int>(m_cueMode));
    }
    m_pConfig->setValue(ConfigKey("[Controls]", "CueDefault"), static_cast<int>(m_cueMode));

    m_pConfig->setValue(ConfigKey("[Controls]", "AllowTrackLoadToPlayingDeck"),
                        !m_bDisallowTrackLoadToPlayingDeck);

    m_pConfig->setValue(ConfigKey("[Controls]", "CueRecall"), static_cast<int>(m_seekOnLoadMode));
    m_pConfig->setValue(ConfigKey("[Controls]", "CloneDeckOnLoadDoubleTap"),
            m_bCloneDeckOnLoadDoubleTap);

    // Set rate range
    setRateRangeForAllDecks(m_iRateRangePercent);
    m_pConfig->setValue(ConfigKey("[Controls]", "RateRangePercent"),
                        m_iRateRangePercent);

    setRateDirectionForAllDecks(m_bRateDownIncreasesSpeed);
    m_pConfig->setValue(ConfigKey("[Controls]", "RateDir"),
            static_cast<int>(m_bRateDownIncreasesSpeed));

    int configSPAutoReset = BaseTrackPlayer::RESET_NONE;

    if (m_speedAutoReset && m_pitchAutoReset) {
        configSPAutoReset = BaseTrackPlayer::RESET_PITCH_AND_SPEED;
    } else if (m_speedAutoReset) {
        configSPAutoReset = BaseTrackPlayer::RESET_SPEED;
    } else if (m_pitchAutoReset) {
        configSPAutoReset = BaseTrackPlayer::RESET_PITCH;
    }

    m_pConfig->set(ConfigKey("[Controls]", "SpeedAutoReset"),
                   ConfigValue(configSPAutoReset));

    m_pConfig->setValue(ConfigKey("[Controls]", "keylockMode"),
                        static_cast<int>(m_keylockMode));
    // Set key lock behavior for every group
    for (ControlProxy* pControl : qAsConst(m_keylockModeControls)) {
        pControl->set(static_cast<double>(m_keylockMode));
    }

    m_pConfig->setValue(ConfigKey("[Controls]", "keyunlockMode"),
                        static_cast<int>(m_keyunlockMode));
    // Set key un-lock behavior for every group
    for (ControlProxy* pControl : qAsConst(m_keyunlockModeControls)) {
        pControl->set(static_cast<double>(m_keyunlockMode));
    }

    RateControl::setRateRampMode(m_bRateRamping);
    m_pConfig->setValue(ConfigKey("[Controls]", "RateRamp"), static_cast<int>(m_bRateRamping));

    RateControl::setRateRampSensitivity(m_iRateRampSensitivity);
    m_pConfig->setValue(ConfigKey("[Controls]", "RateRampSensitivity"), m_iRateRampSensitivity);

    RateControl::setTemporaryRateChangeCoarseAmount(m_dRateTempCoarse);
    RateControl::setTemporaryRateChangeFineAmount(m_dRateTempFine);
    RateControl::setPermanentRateChangeCoarseAmount(m_dRatePermCoarse);
    RateControl::setPermanentRateChangeFineAmount(m_dRatePermFine);

    m_pConfig->setValue(ConfigKey("[Controls]", "RateTempLeft"), m_dRateTempCoarse);
    m_pConfig->setValue(ConfigKey("[Controls]", "RateTempRight"), m_dRateTempFine);
    m_pConfig->setValue(ConfigKey("[Controls]", "RatePermLeft"), m_dRatePermCoarse);
    m_pConfig->setValue(ConfigKey("[Controls]", "RatePermRight"), m_dRatePermFine);
}

void DlgPrefDeck::slotNumDecksChanged(double new_count, bool initializing) {
    int numdecks = static_cast<int>(new_count);
    if (numdecks <= m_iNumConfiguredDecks) {
        // TODO(owilliams): If we implement deck deletion, shrink the size of configured decks.
        return;
    }

    for (int i = m_iNumConfiguredDecks; i < numdecks; ++i) {
        QString group = PlayerManager::groupForDeck(i);
        m_rateControls.push_back(new ControlProxy(
                group, "rate"));
        m_rateRangeControls.push_back(new ControlProxy(
                group, "rateRange"));
        m_rateDirectionControls.push_back(new ControlProxy(
                group, "rate_dir"));
        m_cueControls.push_back(new ControlProxy(
                group, "cue_mode"));
        m_keylockModeControls.push_back(new ControlProxy(
                group, "keylockMode"));
        m_keylockModeControls.last()->set(static_cast<double>(m_keylockMode));
        m_keyunlockModeControls.push_back(new ControlProxy(
                group, "keyunlockMode"));
        m_keyunlockModeControls.last()->set(static_cast<double>(m_keyunlockMode));
    }

    m_iNumConfiguredDecks = numdecks;

    // The rate range hasn't been read from the config file when this is first called.
    if (!initializing) {
        setRateDirectionForAllDecks(m_rateDirectionControls[0]->get() == kRateDirectionInverted);
        setRateRangeForAllDecks(static_cast<int>(m_rateRangeControls[0]->get() * 100.0));
    }
}

void DlgPrefDeck::slotNumSamplersChanged(double new_count, bool initializing) {
    int numsamplers = static_cast<int>(new_count);
    if (numsamplers <= m_iNumConfiguredSamplers) {
        return;
    }

    for (int i = m_iNumConfiguredSamplers; i < numsamplers; ++i) {
        QString group = PlayerManager::groupForSampler(i);
        m_rateControls.push_back(new ControlProxy(
                group, "rate"));
        m_rateRangeControls.push_back(new ControlProxy(
                group, "rateRange"));
        m_rateDirectionControls.push_back(new ControlProxy(
                group, "rate_dir"));
        m_cueControls.push_back(new ControlProxy(
                group, "cue_mode"));
        m_keylockModeControls.push_back(new ControlProxy(
                group, "keylockMode"));
        m_keylockModeControls.last()->set(static_cast<double>(m_keylockMode));
        m_keyunlockModeControls.push_back(new ControlProxy(
                group, "keyunlockMode"));
        m_keyunlockModeControls.last()->set(static_cast<double>(m_keyunlockMode));
    }

    m_iNumConfiguredSamplers = numsamplers;

    // The rate range hasn't been read from the config file when this is first called.
    if (!initializing) {
        setRateDirectionForAllDecks(m_rateDirectionControls[0]->get() == kRateDirectionInverted);
        setRateRangeForAllDecks(static_cast<int>(m_rateRangeControls[0]->get() * 100.0));
    }
}

void DlgPrefDeck::slotUpdateSpeedAutoReset(bool b) {
    m_speedAutoReset = b;
}

void DlgPrefDeck::slotUpdatePitchAutoReset(bool b) {
    m_pitchAutoReset = b;
}

int DlgPrefDeck::cueDefaultIndexByData(int userData) const {
    for (int i = 0; i < ComboBoxCueMode->count(); ++i) {
        if (ComboBoxCueMode->itemData(i).toInt() == userData) {
            return i;
        }
    }
    qWarning() << "No default cue behavior found for value" << userData
               << "returning default";
    return 0;
}
