#pragma once
#include <Arduino.h>

#include "euclidean.hpp"
#include "utils.hpp"

#include "quantizer.cpp"
#include "scales.cpp"

// Define a type for the DAC output type
enum OutputType {
    DigitalOut = 0,
    DACOut = 1,
};

// Implement WaveformType enum
enum WaveformType {
    Square = 0,
    Triangle,
    Sine,
    Parabolic,
    Sawtooth,
    ExpEnvelope,
    LogEnvelope,
    InvExpEnvelope,
    InvLogEnvelope,
    Hatchet2,       // 2 square sub-pulses per clock cycle
    Hatchet4,       // 4 square sub-pulses per clock cycle
    Noise,
    SmoothNoise,
    SampleHold,
    ResetTrig,
    Play,
    ADEnvelope,
    AREnvelope,
    ADSREnvelope,
    QuantizeInput,
};

String WaveformTypeDescriptions[] = {
    "Square",
    "Triangle",
    "Sine",
    "Parabolic",
    "Sawtooth",
    "Exp Env",
    "Log Env",
    "Inv Exp Env",
    "Inv Log Env",
    "Hatchet x2",
    "Hatchet x4",
    "Noise",
    "SmoothNoise",
    "S&H",
    "Reset",
    "Play",
    "AD Env",
    "AR Env",
    "ADSR Env",
    "Quantize",
};
int WaveformTypeLength = sizeof(WaveformTypeDescriptions) / sizeof(WaveformTypeDescriptions[0]);

// ADSR envelope parameters
typedef struct {
    float attack;       // Attack time in ms
    float decay;        // Decay time in ms
    float sustain;      // Sustain level (0-100%)
    float release;      // Release time in ms
    float attackCurve;  // 0=log, 0.5=linear, 1=exp
    float decayCurve;   // 0=log, 0.5=linear, 1=exp
    float releaseCurve; // 0=log, 0.5=linear, 1=exp
    bool retrigger;     // Retrigger on gate high
} EnvelopeParams;

typedef struct {
    bool enable;
    int octaveShift;
    int channelSensitivity;
    int scaleIndex;
    int noteIndex;
} QuantizerParams;

class Output {
  public:
    // Constructor
    Output(int ID, OutputType type);

    // Pulse State
    void Pulse(int PPQN, unsigned long tickCounter);
    void GeneratePulse(int PPQN, unsigned long tickCounter);
    void GenEnvelope();
    bool GetPulseState() { return _isPulseOn; }
    void SetPulse(bool state) { _isPulseOn = state; }
    void TogglePulse() { _isPulseOn = !_isPulseOn; }
    bool HasPulseChanged();
    // Display blink indicator: toggles once per output period in GeneratePulse().
    // Unlike _isPulseOn, this is waveform-type agnostic and doesn't alias at fast rates.
    bool GetBlinkState() { return _blinkState; }
    void SetExternalClock(bool state) { _externalClock = state; }
    void IncrementInternalCounter() { _internalPulseCounter++; }

    // Output State
    bool GetOutputState() { return _state; }
    void SetOutputState(bool state) { _state = state; }
    void ToggleOutputState() { _state = !_state; }
    void ToggleMasterState();
    void SetMasterState(bool state);

    // Divider
    int GetDividerIndex() { return _dividerIndex; }
    void SetDivider(int index) {
#if !defined(ARDUINO_ARCH_RP2040)
        // DigitalOut (SAMD21 only) cannot use the Env divider slot
        if (_outputType == OutputType::DigitalOut && index >= _dividerAmount - 1) {
            index = _dividerAmount - 2;
        }
#endif
        // Env slot (last index) is not user-selectable; SetWaveformType uses SetDividerInternal.
        _dividerIndex = constrain(index, 0, _dividerAmount - 2);
    }
    String GetDividerDescription() { return _dividerDescription[_dividerIndex]; }
    int GetDividerAmounts() { return _dividerAmount - 1; } // Env slot is not user-selectable

    // Duty Cycle
    int GetDutyCycle() { return _dutyCycle; }
    void SetDutyCycle(int dutyCycle) { _dutyCycle = constrain(dutyCycle, 1, 99); }
    String GetDutyCycleDescription() { return String(_dutyCycle) + "%"; }

    // Output Level
    uint32_t GetLevel() { return _level; }
    uint32_t GetOutputLevel(); // Output Level based on the output type
    void QuantizerCVValue(float CVValue);
    String GetLevelDescription() { return String(_level) + "%"; }
    void SetLevel(int level) { _level = constrain(level, 0, 100); }

    // Output Offset
    int GetOffset() { return _offset; }
    void SetOffset(int offset) { _offset = constrain(offset, 0, 100); }
    String GetOffsetDescription() { return String(_offset) + "%"; }

    // Swing
    void SetSwingAmount(int swingAmount) { _swingAmountIndex = constrain(swingAmount, 0, 6); }
    int GetSwingAmountIndex() { return _swingAmountIndex; }
    int GetSwingAmounts() { return _swingAmount; }
    String GetSwingAmountDescription() { return _swingAmountDescriptions[_swingAmountIndex]; }
    void SetSwingEvery(int swingEvery) { _swingEvery = constrain(swingEvery, 1, _swingEveryAmount); }
    int GetSwingEvery() { return _swingEvery; }
    int GetSwingEveryAmounts() { return _swingEveryAmount; }

    // Pulse Probability
    void SetPulseProbability(int pulseProbability) { _pulseProbability = constrain(pulseProbability, 0, 100); }
    int GetPulseProbability() { return _pulseProbability; }
    String GetPulseProbabilityDescription() { return String(_pulseProbability) + "%"; }

    // Euclidean Rhythm
    EuclideanParams GetEuclideanParams() { return _euclideanParams; }
    void SetEuclideanParams(EuclideanParams params) { _euclideanParams = params; }
    void SetEuclidean(bool euclidean);
    void ToggleEuclidean() { SetEuclidean(!_euclideanParams.enabled); }
    bool GetEuclidean() { return _euclideanParams.enabled; }
    int GetRhythmStep(int i) { return _euclideanRhythm[i]; }
    void SetEuclideanSteps(int steps);
    int GetEuclideanSteps() { return _euclideanParams.steps; }
    void SetEuclideanTriggers(int triggers);
    int GetEuclideanTriggers() { return _euclideanParams.triggers; }
    void SetEuclideanRotation(int rotation);
    int GetEuclideanRotation() { return _euclideanParams.rotation; }
    void SetEuclideanPadding(int pad);
    int GetEuclideanPadding() { return _euclideanParams.pad; }

    // Phase
    void SetPhase(int phase) { _phase = constrain(phase, 0, 100); }
    int GetPhase() { return _phase; }
    String GetPhaseDescription() { return String(_phase) + "%"; }

    // Waveform Type
    int GetWaveformTypeIndex() { return int(_waveformType); }
    void SetWaveformType(WaveformType type);
    WaveformType GetWaveformType() { return _waveformType; }
    String GetWaveformTypeDescription() { return WaveformTypeDescriptions[_waveformType]; }
    bool IsEnvelopeType() {
        return _waveformType == WaveformType::ADEnvelope ||
               _waveformType == WaveformType::AREnvelope ||
               _waveformType == WaveformType::ADSREnvelope;
    }

    // Trigger mode control
    void SetTriggerMode(bool enabled) { _triggerMode = enabled; }
    bool GetTriggerMode() { return _triggerMode; }
    bool GetExternalTrigger() { return _externaltrigger; }
    void SetExternalTrigger(bool state) {
        if (state != _externaltrigger) {
            _externaltrigger = state;
            if (state)
                HandleTrigger();
            else
                HandleGateRelease();
        }
    }
    void SetCVValue(float CVValue) { _inputCV = CVValue; }; // Set the input CV value for the quantizer

    // Envelope parameter setters
    EnvelopeParams GetEnvelopeParams() { return _envParams; }
    void SetEnvelopeParams(EnvelopeParams params) { _envParams = params; }
    void SetAttack(float ms) { _envParams.attack = constrain(ms, 0.1f, 10000.0f); }
    void SetDecay(float ms) { _envParams.decay = constrain(ms, 0.1f, 10000.0f); }
    void SetSustain(float level) { _envParams.sustain = constrain(level, 0.0f, 100.0f); }
    void SetRelease(float ms) { _envParams.release = constrain(ms, 0.1f, 10000.0f); }
    float GetAttack() { return _envParams.attack; }
    float GetDecay() { return _envParams.decay; }
    float GetSustain() { return _envParams.sustain; }
    float GetRelease() { return _envParams.release; }
    String GetAttackDescription() {
        return _envParams.attack > 999 ? String(_envParams.attack / 1000.0f, 1) + "s" : String((int)_envParams.attack) + "ms";
    }
    String GetDecayDescription() { return _envParams.decay > 999 ? String(_envParams.decay / 1000.0f, 1) + "s" : String((int)_envParams.decay) + "ms"; }
    String GetSustainDescription() { return String((int)_envParams.sustain) + "%"; }
    String GetReleaseDescription() { return _envParams.release > 999 ? String(_envParams.release / 1000.0f, 1) + "s" : String((int)_envParams.release) + "ms"; }
    void SetRetrigger(bool state) { _envParams.retrigger = state; }
    bool GetRetrigger() { return _envParams.retrigger; }
    void ToggleRetrigger() { _envParams.retrigger = !_envParams.retrigger; }
    String GetRetriggerDescription() { return _envParams.retrigger ? "Yes" : "No"; }
    void SetAttackCurve(float curve) { _envParams.attackCurve = constrain(curve, 0.0f, 1.0f); }
    void SetDecayCurve(float curve) { _envParams.decayCurve = constrain(curve, 0.0f, 1.0f); }
    void SetReleaseCurve(float curve) { _envParams.releaseCurve = constrain(curve, 0.0f, 1.0f); }
    float GetAttackCurve() { return _envParams.attackCurve; }
    float GetDecayCurve() { return _envParams.decayCurve; }
    float GetReleaseCurve() { return _envParams.releaseCurve; }
    void SetCurve(float curve) {
        SetAttackCurve(curve);
        SetDecayCurve(curve);
        SetReleaseCurve(curve);
    }
    float GetCurve() { return (_envParams.attackCurve + _envParams.decayCurve + _envParams.releaseCurve) / 3.0f; }
    String GetCurveDescription() { return String(GetCurve() * 100, 0) + "%"; }

    // Quantizer
    QuantizerParams GetQuantizerParams() { return _quantizerParams; }
    void SetQuantizerParams(QuantizerParams params) { _quantizerParams = params; }
    void SetupQuantizer();
    void SetQuantizerEnable(bool enable) { _quantizerParams.enable = enable; }
    bool GetQuantizerEnable() { return _quantizerParams.enable; }
    void ToggleQuantizer() { _quantizerParams.enable = !_quantizerParams.enable; }
    String GetQuantizerEnableDescription() { return _quantizerParams.enable ? "On" : "Off"; }
    void SetQuantizerOctaveShift(int shift) {
        _quantizerParams.octaveShift = constrain(shift, 0, 6);
        SetupQuantizer();
    }
    int GetQuantizerOctaveShift() { return _quantizerParams.octaveShift; }
    String GetQuantizerOctaveShiftDescription() { return String(_quantizerParams.octaveShift - 3); }
    void SetQuantizerChannelSensitivity(int sensitivity) {
        _quantizerParams.channelSensitivity = constrain(sensitivity, 0, 8);
        SetupQuantizer();
    }
    int GetQuantizerChannelSensitivity() { return _quantizerParams.channelSensitivity; }
    String GetQuantizerChannelSensitivityDescription() { return String(_quantizerParams.channelSensitivity); }
    void SetQuantizerScaleIndex(int index) {
        _quantizerParams.scaleIndex = constrain(index, 0, numScales);
        SetupQuantizer();
    }
    int GetQuantizerScaleIndex() { return _quantizerParams.scaleIndex; }
    String GetQuantizerScaleDescription() { return scaleNames[_quantizerParams.scaleIndex]; }
    void SetQuantizerNoteIndex(int index) {
        _quantizerParams.noteIndex = constrain(index, 0, 11);
        SetupQuantizer();
    }
    int GetQuantizerNoteIndex() { return _quantizerParams.noteIndex; }
    String GetQuantizerNoteDescription() { return noteNames[_quantizerParams.noteIndex]; }

  private:
    // Constants
    const int MaxDACValue = 3830; // Effective full-scale DAC count (~4.68V).
                                  // 4095 drives the output op-amp into its near-rail
                                  // saturation region, causing a visible flat top on
                                  // continuous waveforms.  3830 stays in the linear
                                  // output range while still covering nearly the full
                                  // 0–5V span.
    const float MaxWaveValue = 255.0;
    static int const _dividerAmount = 21;
    float _clockDividers[_dividerAmount] = {0.0078125, 0.015625, 0.03125, 0.0625, 0.125, 0.25, 0.3333333333, 0.5, 0.6666666667, 1.0, 1.5, 2.0, 3.0, 4.0, 8.0, 16.0, 24.0, 32.0, 48.0, 64.0, 10000};
    String _dividerDescription[_dividerAmount] = {"/128", "/64", "/32", "/16", "/8", "/4", "/3", "/2", "/1.5", "x1", "x1.5", "x2", "x3", "x4", "x8", "x16", "x24", "x32", "x48", "x64", "Env"};
    static int const MaxEuclideanSteps = 64;

    // The shuffle of the TR-909 delays each even-numbered 1/16th by 2/96 of a beat for shuffle setting 1,
    // 4/96 for 2, 6/96 for 3, 8/96 for 4, 10/96 for 5 and 12/96 for 6.
    static int const _swingAmount = 7;
    float _swingAmounts[_swingAmount] = {0, 2, 4, 6, 8, 10, 12};
    String _swingAmountDescriptions[_swingAmount] = {"0", "2/96", "4/96", "6/96", "8/96", "10/96", "12/96"};

    // Variables
    int _ID;
    bool _externalClock = false;  // External clock state
    OutputType _outputType;       // 0 = Digital, 1 = DAC
    int _dividerIndex = 9;        // Default to 1
    int _dutyCycle = 50;          // Default to 50%
    int _phase = 0;               // Phase offset, default to 0% (in phase with master)
    int _level = 100;             // Output voltage level for DAC outs (Default to 100%)
    int _offset = 0;              // Output voltage offset for DAC outs (default to 0%)
    bool _isPulseOn = false;      // Pulse state
    bool _lastPulseState = false; // Last pulse state
    bool _blinkState = false;     // Display blink indicator (toggled once per output period)
    bool _state = true;           // Output state
    bool _oldState = true;        // Previous output state (for master stop)
    bool _masterState = true;     // Master output state
    int _pulseProbability = 100;  // % chance of pulse

    QuantizerParams _quantizerParams = {
        .enable = false,
        .octaveShift = 3,
        .channelSensitivity = 4,
        .scaleIndex = 1,
        .noteIndex = 0,
    };
    int _quantizerThresholdBuff[62]; // input quantize
    bool _activeNotes[12] = {false}; // 1=note valid,0=note invalid
    float _inputCV = 0.0f;           // Input CV value for quantizer (set by HandleCVTarget)

    unsigned long _internalPulseCounter = 0; // Pulse counter (used for external clock division)
    unsigned long _resetPulseStart = 0;      // Reset pulse start time

    // Waveform generation variables
    WaveformType _waveformType = WaveformType::Square; // Default to square wave
    bool _waveActive = false;
    bool _waveDirection = true; // Waveform direction (true = up, false = down)
    float _waveValue = 0.0f;
    uint32_t _oldOutputLevel = 0.0f;
    float _triangleWaveStep = 0.0f;
    float _sineWaveAngle = 0.0f;
    unsigned long _inactiveTickCounter = 0;
    unsigned long _randomTickCounter = 0;
    unsigned long _envTickCounter = 0; // Logarithmic envelope ticks

    // Swing variables
    int _swingEveryAmount = 16;         // Max swing every value
    int _swingEvery = 2;                // Swing every x notes
    unsigned int _swingAmountIndex = 0; // Swing amount index

    // Euclidean rhythm variables
    int _euclideanStepIndex = 0; // Current step in the pattern
    EuclideanParams _euclideanParams = {
        .enabled = false,
        .steps = 10,   // Number of steps in the pattern
        .triggers = 6, // Number of triggers in the pattern
        .rotation = 1, // Rotation of the pattern
        .pad = 0,      // No trigger steps added to the end of the pattern
    };
    int _euclideanRhythm[MaxEuclideanSteps]; // Euclidean rhythm pattern

    // Envelope
    bool _triggerMode = false;
    bool _externaltrigger = false;
    unsigned long _envStartTime = 0;
    unsigned long _lastTriggerTime = 0; // Debounce: time of last HandleTrigger() fire
    float _lastEnvValue = 0.0f; // Stores last envelope value for retriggering

    // ADSR envelope parameters
    EnvelopeParams _envParams = {
        .attack = 200.0f,
        .decay = 200.0f,
        .sustain = 70.0f,
        .release = 250.0f,
        .attackCurve = 0.5f,
        .decayCurve = 0.5f,
        .releaseCurve = 0.5f,
        .retrigger = false,
    };

    // Envelope state tracking
    enum EnvelopeState {
        Idle,
        Attack,
        AttackHold, // New state for AR envelope
        Decay,
        Sustain,
        Release
    } _envState = Idle;

    // -------------- Private Functions --------------

    // Bypass the user-selectable cap to allow setting the Env slot (index _dividerAmount-1).
    // Only called by SetWaveformType when an envelope waveform is selected.
    void SetDividerInternal(int index) {
        _dividerIndex = constrain(index, 0, _dividerAmount - 1);
    }

    // Start the waveform generation
    void StartWaveform() {
        _waveActive = true;
        switch (_waveformType) {
        case WaveformType::Square:
            SetPulse(true);
            break;
        case WaveformType::Triangle:
        case WaveformType::Sawtooth:
        case WaveformType::Sine:
        case WaveformType::Parabolic:
            if (!_externalClock) {
                _waveValue = 0.0f;
                _triangleWaveStep = 0.0f;
                _sineWaveAngle = 0.0f;
            }
            break;
        case WaveformType::ExpEnvelope:
        case WaveformType::LogEnvelope:
            _waveValue = 0; // Start at 0 value for envelopes
            _envTickCounter = 0;
            break;
        case WaveformType::InvExpEnvelope:
        case WaveformType::InvLogEnvelope:
            _waveValue = MaxWaveValue; // Start at maximum value for inverted envelopes
            _envTickCounter = 0;
            break;
        case WaveformType::Noise:
        case WaveformType::SmoothNoise:
        case WaveformType::SampleHold:
            _randomTickCounter = 0;
            break;
        case WaveformType::ResetTrig:
            _isPulseOn = (_waveValue > 0);
            break;
        case WaveformType::Play:
            _waveValue = _masterState ? MaxWaveValue : 0;
            _isPulseOn = _masterState;
            break;
        case WaveformType::ADEnvelope:
        case WaveformType::AREnvelope:
        case WaveformType::ADSREnvelope:
            if (_envState == EnvelopeState::Idle || _envParams.retrigger) {
                _lastEnvValue = _waveValue;
                _envState = EnvelopeState::Attack;
                _envStartTime = micros();
                _waveActive = true;
            }
            break;
        default:
            SetPulse(true);
            break;
        }
    }

    // Reset the waveform values
    void ResetWaveform() {
        switch (_waveformType) {
        case WaveformType::ExpEnvelope:
        case WaveformType::LogEnvelope:
        case WaveformType::InvExpEnvelope:
        case WaveformType::InvLogEnvelope:
            break;
        default:
            _waveActive = false;
            _waveDirection = true;
            _waveValue = 0.0f;
            _triangleWaveStep = 0.0f;
            _sineWaveAngle = 0.0f;
        }
    }

    // Function to stop waveform generation
    void StopWaveform() {
        switch (_waveformType) {
        case WaveformType::Square:
            SetPulse(false);
            _waveActive = false;
            break;
        case WaveformType::Hatchet2:
        case WaveformType::Hatchet4:
            SetPulse(false);
            _waveActive = false;
            break;
        case WaveformType::ExpEnvelope:
        case WaveformType::LogEnvelope:
        case WaveformType::InvExpEnvelope:
        case WaveformType::InvLogEnvelope:
            break;
        case WaveformType::ADEnvelope:
        case WaveformType::AREnvelope:
        case WaveformType::ADSREnvelope:
            // For AR/ADSR, only trigger release phase
            if (_waveActive && (_waveformType == WaveformType::AREnvelope ||
                                _waveformType == WaveformType::ADSREnvelope)) {
                HandleGateRelease();
            }
            break;
        case WaveformType::SmoothNoise:
        case WaveformType::Noise:
        case WaveformType::Triangle:
        case WaveformType::Sawtooth:
        case WaveformType::Sine:
        case WaveformType::Parabolic:
        case WaveformType::SampleHold:
        default:
            SetPulse(false);
            break;
        }
    }

    // Generate a triangle wave
    void GenerateTriangleWave(int PPQN) {
        if (_waveActive) {
            float periodTicks = PPQN / _clockDividers[_dividerIndex];
            float risingTicks = periodTicks * (_dutyCycle / 100.0f);
            float fallingTicks = periodTicks - risingTicks;

            // Calculate step sizes
            float risingStep = MaxWaveValue / risingTicks;
            float fallingStep = MaxWaveValue / fallingTicks;

            // Update waveform value
            if (_waveDirection) {
                _waveValue += risingStep;
                if (_waveValue >= MaxWaveValue) {
                    _waveValue = MaxWaveValue;
                    _waveDirection = false;
                }
            } else {
                _waveValue -= fallingStep;
                if (_waveValue <= 0.0f) {
                    _waveValue = 0.0f;
                    _waveDirection = true;
                }
            }
            _isPulseOn = true;
        }
    }

    // Generate a sine wave
    void GenerateSineWave(int PPQN) {
        if (_waveActive) {
            // Calculate the period of the waveform in ticks
            float periodInTicks = PPQN / _clockDividers[_dividerIndex];

            // Calculate the angle increment per tick
            float angleIncrement = (2.0f * PI) / periodInTicks;

            // Update the angle for the sine function
            _sineWaveAngle += angleIncrement;

            // Keep the angle within 0 to 2*PI
            if (_sineWaveAngle >= 2.0f * PI) {
                _sineWaveAngle -= 2.0f * PI;
            }

            // Apply phase shift to align the lowest point with pulse start
            float shiftedAngle = _sineWaveAngle + (3.0f * PI / 2.0f);

            // Calculate the sine value scaled to the amplitude range
            // Adjust the sine value based on the duty cycle
            float sineValue = sin(shiftedAngle);
            if (sineValue > 0) {
                sineValue = pow(sineValue, _dutyCycle / 50.0f);
            } else {
                sineValue = -pow(-sineValue, _dutyCycle / 50.0f);
            }
            _waveValue = (sineValue * MaxWaveValue / 2) + MaxWaveValue / 2;

            _isPulseOn = true;
        }
    }

    // Generate a parabolic wave
    void GenerateParabolicWave(int PPQN) {
        if (_waveActive) {
            float periodInTicks = PPQN / _clockDividers[_dividerIndex];
            float activeTicks = periodInTicks * (_dutyCycle / 100.0f);
            float angleIncrement = (PI) / activeTicks; // Half sine wave for duty cycle

            _sineWaveAngle += angleIncrement;
            if (_sineWaveAngle >= PI) {
                _sineWaveAngle = 0.0f;
                // Inactive period
                _waveActive = false;
                _isPulseOn = false;
                _inactiveTickCounter = periodInTicks - activeTicks;
            }

            // Calculate sine value
            _waveValue = sin(_sineWaveAngle) * MaxWaveValue;
            _isPulseOn = true;
        }
    }

    // Generate a sawtooth wave
    void GenerateSawtoothWave(int PPQN) {
        if (_waveActive) {
            float periodTicks = PPQN / _clockDividers[_dividerIndex];
            float activeTicks = periodTicks * (_dutyCycle / 100.0f);
            float inactiveTicks = periodTicks - activeTicks;

            // Calculate step size
            float step = MaxWaveValue / activeTicks;

            // Update waveform value
            _waveValue += step;
            if (_waveValue >= MaxWaveValue) {
                _waveValue = 0.0f;
                // Adjust for inactive period
                _waveActive = false;
                _inactiveTickCounter = inactiveTicks;
            }
            _isPulseOn = true;
        } else {
            // Handle inactive period
            _inactiveTickCounter--;
            if (_inactiveTickCounter <= 0) {
                _waveActive = true;
            }
            _isPulseOn = false;
        }
    }

    // Generate random values
    void GenerateNoiseWave(int PPQN) {
        if (_waveActive) {
            // Generate white noise waveform
            _waveValue = random(MaxWaveValue + 1); // Random value
            _isPulseOn = true;
            _randomTickCounter++;
        }
    }

    // Generate smooth random waveform
    void GenerateSmoothNoiseWave(int PPQN) {
        if (_waveActive) {
            // Generate smooth random waveform with smooth peaks and valleys
            static float phase = 0.0f;
            static float frequency = 0.3f;             // Adjust frequency for smoothness
            static float amplitude = MaxWaveValue / 2; // Amplitude for wave value range
            static float lastValue = MaxWaveValue / 2; // Last generated value
            static float smoothValue = 50.0f;          // Smoothed value

            // Increment phase
            phase += frequency;

            // Generate smooth random value using a random walk
            float randomStep = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f; // Random step between -1 and 1
            lastValue += randomStep * amplitude * frequency;                          // Adjust step size by amplitude and frequency
            lastValue = fmin(fmax(lastValue, 0.0f), MaxWaveValue);                    // Clamp value

            // Apply a low-pass filter to smooth out the waveform
            float alpha = 0.01f; // Smoothing factor (0 < alpha < 1)
            smoothValue = alpha * lastValue + (1.0f - alpha) * smoothValue;

            _waveValue = smoothValue;

            _isPulseOn = true;
        }
    }

    // Generate an inverted exponential envelope waveform (starts from 100% and decays to 0%)
    void GenerateInvExpEnvelope(int PPQN) {
        if (_waveActive) {
            float periodTicks = PPQN / _clockDividers[_dividerIndex];
            float decayTicks = periodTicks * (_dutyCycle / 100.0f);

            if (_envTickCounter >= decayTicks) {
                _waveValue = 0.0f;
                _waveActive = false;
                _envTickCounter = 0; // Reset for the next pulse
                return;
            }

            // Calculate decay factor for the exponential decay
            float k = 6.90776f / decayTicks; // ln(100) ≈ 4.60517, total decay over decayTicks
            _waveValue = MaxWaveValue * exp(-k * _envTickCounter);

            _envTickCounter++;
            _isPulseOn = true;
        }
    }

    // Generate an inverted logarithm envelope waveform (starts from 100% and decays to 0%)
    void GenerateInvLogEnvelope(int PPQN) {
        if (_waveActive) {
            float periodTicks = PPQN / _clockDividers[_dividerIndex];
            float decayTicks = periodTicks * (_dutyCycle / 100.0f);

            if (_envTickCounter >= decayTicks) {
                _waveValue = 0.0f;
                _waveActive = false;
                _envTickCounter = 0; // Reset for the next pulse
                return;
            }

            // Calculate decay factor to span the entire pulse duration
            float decayFactor = log10(decayTicks - _envTickCounter + 1) / log10(decayTicks + 1);

            // Update waveform value
            _waveValue = decayFactor * MaxWaveValue; // Scale to 0-100%

            _envTickCounter++;
            _isPulseOn = true;
        }
    }

    // Generate an exponential envelope waveform (starts from 0% and rises to 100%)
    void GenerateExpEnvelope(int PPQN) {
        if (_waveActive) {
            float periodTicks = PPQN / _clockDividers[_dividerIndex];
            float attackTicks = periodTicks * (_dutyCycle / 100.0f);

            if (_envTickCounter >= attackTicks) {
                _waveValue = MaxWaveValue;
                _waveActive = false;
                _envTickCounter = 0; // Reset for the next pulse
                return;
            }

            // Use normalized time from 0-1
            float normalizedTime = _envTickCounter / attackTicks;

            // Apply exponential curve: e^(k*t) - 1 / (e^k - 1)
            // This gives a true exponential curve that starts at 0 and ends at 1
            float k = 4.0f; // Determines curve steepness
            _waveValue = MaxWaveValue * ((exp(k * normalizedTime) - 1.0f) / (exp(k) - 1.0f));

            _envTickCounter++;
            _isPulseOn = true;
        }
    }

    // Generate a logarithmic envelope waveform (starts from 0% and rises to 100%)
    void GenerateLogEnvelope(int PPQN) {
        if (_waveActive) {
            float periodTicks = PPQN / _clockDividers[_dividerIndex];
            float attackTicks = periodTicks * (_dutyCycle / 100.0f);

            if (_envTickCounter >= attackTicks) {
                _waveValue = MaxWaveValue;
                _waveActive = false;
                _envTickCounter = 0; // Reset for the next pulse
                return;
            }

            // Calculate attack factor to span the entire pulse duration
            float attackFactor = log10(_envTickCounter + 1) / log10(attackTicks + 1);

            // Update waveform value
            _waveValue = attackFactor * MaxWaveValue; // Scale to 0-100%

            _envTickCounter++;
            _isPulseOn = true;
        }
    }

    // Generate a Sample and Hold waveform where on each pulse, a random value is generated
    void GenerateSampleHold(int PPQN) {
        if (_waveActive) {
            // Generate a random value at the start of each pulse
            if (_randomTickCounter == 0) {
                _waveValue = random(MaxWaveValue + 1);
            }
            _isPulseOn = true;
            _randomTickCounter++;
        }
    }

    // ----------- Envelope Generation Functions -----------
    void HandleTrigger() {
        if (!_state) return; // Output disabled — ignore triggers
        if (random(100) >= _pulseProbability) return; // Probability gate
        if (_triggerMode && (_waveformType == WaveformType::ADEnvelope || _waveformType == WaveformType::AREnvelope || _waveformType == WaveformType::ADSREnvelope)) {
            if (!_waveActive || _envParams.retrigger) {
#ifdef ENVELOPE_DEBUG
                Serial.printf("[TRIG ch%d] wActive=%d state=%d wv=%.2f t=%lums\n",
                    _ID, (int)_waveActive, (int)_envState, _waveValue, millis());
#endif
                _lastEnvValue = _envParams.retrigger ? _waveValue : 0.0f;
                _envState = EnvelopeState::Attack;
                _envStartTime = micros();
                _waveActive = true;
                _isPulseOn = true;
                _lastTriggerTime = millis(); // Record for HandleGateRelease debounce
            }
        }
    }

    void HandleGateRelease() {
        if (_triggerMode && (_waveformType == WaveformType::ADEnvelope || _waveformType == WaveformType::AREnvelope || _waveformType == WaveformType::ADSREnvelope)) {
            if (_waveformType == WaveformType::AREnvelope ||
                _waveformType == WaveformType::ADSREnvelope) {
                // Debounce: ignore releases that arrive within 50 ms of the last
                // trigger. This covers the worst-case UpdateBPM timer-cancel gap
                // (≤30 ms with the rate-limit) plus IIR filter settling time.
                if (millis() - _lastTriggerTime < 50) {
#ifdef ENVELOPE_DEBUG
                    Serial.printf("[REL_DEBOUNCE ch%d] age=%lums state=%d wv=%.2f\n",
                        _ID, millis() - _lastTriggerTime, (int)_envState, _waveValue);
#endif
                    return;
                }
                if (_waveActive && _envState != EnvelopeState::Release) {
#ifdef ENVELOPE_DEBUG
                    Serial.printf("[RELEASE ch%d] state=%d wv=%.2f lev=%.2f t=%lums\n",
                        _ID, (int)_envState, _waveValue, _lastEnvValue, millis());
#endif
                    _lastEnvValue = _waveValue;
                    _envState = EnvelopeState::Release;
                    _envStartTime = micros();
                }
            }
        }
    }

    float ApplyCurve(float input, float curve) {
        // input and output are 0-1 range
        // Clamp input so curves never produce negative waveValues (would otherwise
        // happen when normalizedTime > 1, i.e. loop lagged past the phase duration).
        input = constrain(input, 0.0f, 1.0f);
        if (curve == 0.5f)
            return input; // Linear

        // Convert curve 0-1 to power range 0.1 to 10
        float power = pow(10.0f, (curve - 0.5f) * 2.0f);
        return pow(input, power);
    }

    // Generate an Attack-Decay envelope waveform
    void GenerateADEnvelope() {
        if (!_waveActive)
            return;

        float currentTime = (micros() - _envStartTime) / 1000.0f;

        switch (_envState) {
        case EnvelopeState::Attack: {
            float normalizedTime = currentTime / _envParams.attack;
            float curvedTime = ApplyCurve(normalizedTime, _envParams.attackCurve);

            if (_envParams.retrigger) {
                _waveValue = _lastEnvValue + ((MaxWaveValue - _lastEnvValue) * curvedTime);
            } else {
                _waveValue = curvedTime * MaxWaveValue;
            }

            if (currentTime >= _envParams.attack) {
                _envState = EnvelopeState::Decay;
                _envStartTime = micros();
                // Use constrained waveValue so _lastEnvValue is never above MaxWaveValue.
                // (waveValue may be > MaxWaveValue here before the end-of-function constrain.)
                _lastEnvValue = constrain(_waveValue, 0.0f, (float)MaxWaveValue);
            }
        } break;

        case EnvelopeState::Decay: {
            float normalizedTime = currentTime / _envParams.decay;
            float curvedTime = ApplyCurve(normalizedTime, _envParams.decayCurve);
            // Use _lastEnvValue (actual attack peak) as starting point so the decay
            // ramps from the true peak even if attack ended slightly below MaxWaveValue.
            _waveValue = _lastEnvValue * (1.0f - curvedTime);

            if (currentTime >= _envParams.decay) {
                _waveActive = false;
                _envState = EnvelopeState::Idle;
                _lastEnvValue = 0;
            }
        } break;

        default:
            break;
        }

        _waveValue = constrain(_waveValue, 0, MaxWaveValue);
    }

    // Generate an Attack-Release envelope waveform
    void GenerateAREnvelope() {
        if (!_waveActive)
            return;

        float currentTime = (micros() - _envStartTime) / 1000.0f;

        switch (_envState) {
        case EnvelopeState::Attack: {
            float normalizedTime = currentTime / _envParams.attack;
            float curvedTime = ApplyCurve(normalizedTime, _envParams.attackCurve);

            if (_envParams.retrigger) {
                _waveValue = _lastEnvValue + ((MaxWaveValue - _lastEnvValue) * curvedTime);
            } else {
                _waveValue = curvedTime * MaxWaveValue;
            }

            if (currentTime >= _envParams.attack) {
                _envState = EnvelopeState::AttackHold;
                _waveValue = MaxWaveValue;
                _lastEnvValue = MaxWaveValue;
                // Reset the phase timer for Release so its currentTime starts at 0
                // when HandleGateRelease() later transitions to Release.
                _envStartTime = micros();
            }
        } break;

        case EnvelopeState::AttackHold:
            _waveValue = MaxWaveValue;
            _isPulseOn = true; // Keep output high throughout hold phase
#ifdef ENVELOPE_DEBUG
            {
                static unsigned long _lastHoldGenMs = 0;
                unsigned long _nowHoldMs = millis();
                if (_nowHoldMs - _lastHoldGenMs >= 20) {
                    _lastHoldGenMs = _nowHoldMs;
                    Serial.printf("[GEN_HOLD ch%d] wv=%.2f iPO=%d wAct=%d t=%lums\n",
                        _ID, _waveValue, (int)_isPulseOn, (int)_waveActive, _nowHoldMs);
                }
            }
#endif
            break;

        case EnvelopeState::Release: {
            float normalizedTime = currentTime / _envParams.release;
            float curvedTime = ApplyCurve(normalizedTime, _envParams.releaseCurve);
            _waveValue = _lastEnvValue * (1.0f - curvedTime);

            if (currentTime >= _envParams.release) {
                _waveActive = false;
                _envState = EnvelopeState::Idle;
                _lastEnvValue = 0;
            }
        } break;

        default:
            break;
        }
        _waveValue = constrain(_waveValue, 0, MaxWaveValue);
    }

    // Generate an ADSR envelope waveform
    void GenerateADSREnvelope() {
        if (!_waveActive)
            return;

        float currentTime = (micros() - _envStartTime) / 1000.0f;

        switch (_envState) {
        case EnvelopeState::Attack: {
            float normalizedTime = currentTime / _envParams.attack;
            float curvedTime = ApplyCurve(normalizedTime, _envParams.attackCurve);

            if (_envParams.retrigger) {
                _waveValue = _lastEnvValue + ((MaxWaveValue - _lastEnvValue) * curvedTime);
            } else {
                _waveValue = curvedTime * MaxWaveValue;
            }

            if (currentTime >= _envParams.attack) {
                _envState = EnvelopeState::Decay;
                _envStartTime = micros();
                _lastEnvValue = constrain(_waveValue, 0.0f, (float)MaxWaveValue);
            }
        } break;

        case EnvelopeState::Decay: {
            float normalizedTime = currentTime / _envParams.decay;
            float curvedTime = ApplyCurve(normalizedTime, _envParams.decayCurve);
            float sustainLevel = MaxWaveValue * (_envParams.sustain / 100.0f);
            // Use _lastEnvValue (actual attack peak) so decay starts from the true
            // peak rather than MaxWaveValue — same fix as GenerateADEnvelope().
            _waveValue = _lastEnvValue - ((_lastEnvValue - sustainLevel) * curvedTime);

            if (currentTime >= _envParams.decay) {
                _envState = EnvelopeState::Sustain;
                _lastEnvValue = _waveValue;
            }
        } break;

        case EnvelopeState::Sustain:
            _waveValue = MaxWaveValue * (_envParams.sustain / 100.0f);
            break;

        case EnvelopeState::Release: {
            float normalizedTime = currentTime / _envParams.release;
            float curvedTime = ApplyCurve(normalizedTime, _envParams.releaseCurve);
            _waveValue = _lastEnvValue * (1.0f - curvedTime);

            if (currentTime >= _envParams.release) {
                _waveActive = false;
                _envState = EnvelopeState::Idle;
                _lastEnvValue = 0;
            }
        } break;

        default:
            break;
        }
        _waveValue = constrain(_waveValue, 0, MaxWaveValue);
    }
};

// Constructor
Output::Output(int ID, OutputType type) {
    _ID = ID;
    _outputType = type;
    GeneratePattern(_euclideanParams, _euclideanRhythm);
    SetupQuantizer();
}

// Setup quantizer scale and buffer
void Output::SetupQuantizer() {
    BuildScale(_quantizerParams.scaleIndex, _quantizerParams.noteIndex, _activeNotes);
    BuildQuantBuffer(_activeNotes, _quantizerThresholdBuff);
}

// Generate envelope based on trigger state
void Output::GenEnvelope() {
    // Handle envelope generation based on trigger state
    if (_triggerMode) {

#ifdef ENVELOPE_DEBUG
        // Warn if waveValue drops below 50% during a phase that should be held high.
        if (_waveActive && _waveValue < MaxWaveValue * 0.5f &&
            (_envState == EnvelopeState::AttackHold ||
             _envState == EnvelopeState::Sustain)) {
            static unsigned long _lastDbgMs = 0;
            unsigned long _nowMs = millis();
            if (_nowMs - _lastDbgMs >= 10) {
                _lastDbgMs = _nowMs;
                Serial.printf("[ENV_DROP ch%d] state=%d wv=%.2f lEv=%.2f iPO=%d t=%lums\n",
                    _ID, (int)_envState, _waveValue, _lastEnvValue,
                    (int)_isPulseOn, _nowMs);
            }
        }
#endif

        switch (_waveformType) {
        case WaveformType::ADEnvelope: {
#ifdef ENVELOPE_DEBUG
            EnvelopeState _prevState = _envState;
#endif
            GenerateADEnvelope();
#ifdef ENVELOPE_DEBUG
            if (_envState != _prevState)
                Serial.printf("[TRANS ch%d] %d->%d wv=%.2f lEv=%.2f iPO=%d t=%lums\n",
                    _ID, (int)_prevState, (int)_envState, _waveValue, _lastEnvValue,
                    (int)_isPulseOn, millis());
#endif
            break;
        }
        case WaveformType::AREnvelope: {
#ifdef ENVELOPE_DEBUG
            EnvelopeState _prevState = _envState;
#endif
            GenerateAREnvelope();
#ifdef ENVELOPE_DEBUG
            if (_envState != _prevState)
                Serial.printf("[TRANS ch%d] %d->%d wv=%.2f lEv=%.2f iPO=%d t=%lums\n",
                    _ID, (int)_prevState, (int)_envState, _waveValue, _lastEnvValue,
                    (int)_isPulseOn, millis());
#endif
            break;
        }
        case WaveformType::ADSREnvelope: {
#ifdef ENVELOPE_DEBUG
            EnvelopeState _prevState = _envState;
#endif
            GenerateADSREnvelope();
#ifdef ENVELOPE_DEBUG
            if (_envState != _prevState)
                Serial.printf("[TRANS ch%d] %d->%d wv=%.2f lEv=%.2f iPO=%d t=%lums\n",
                    _ID, (int)_prevState, (int)_envState, _waveValue, _lastEnvValue,
                    (int)_isPulseOn, millis());
#endif
            break;
        }
        default:
            // Handle other waveforms as before
            break;
        }
    }
}

void Output::GeneratePulse(int PPQN, unsigned long globalTick) {
    bool shouldTrigger = (random(100) < _pulseProbability);
    if (!_euclideanParams.enabled) {
        // If not using Euclidean rhythm, generate waveform based on the pulse probability
        if (shouldTrigger) {
            StartWaveform();
        } else {
            // We stop the waveform directly if the pulse probability is not met since StopWaveform() is used for the square wave
            ResetWaveform();
        }
    } else {
        // If using Euclidean rhythm, check if the current step is active
        if (_euclideanRhythm[_euclideanStepIndex] == 1) {
            // Active step in the pattern
            if (shouldTrigger) {
                StartWaveform();
            } else {
                ResetWaveform();
            }
        } else {
            // Inactive step in the pattern
            ResetWaveform();
        }

        _euclideanStepIndex++;
        // Restart the Euclidean rhythm if it reaches the end
        if (_euclideanStepIndex >= _euclideanParams.steps + _euclideanParams.pad) {
            _euclideanStepIndex = 0;
        }
    }
}

void Output::Pulse(int PPQN, unsigned long globalTick) {
    // If not stopped, generate the pulse
    if (!_state) {
        StopWaveform();
        return;
    }

    // Special handling for ResetTrig - we don't want it to follow normal clock timing
    if (_waveformType == WaveformType::ResetTrig) {
        // The reset pulse is controlled entirely by SetMasterState
        // Just check if we need to end the pulse after duration
        if (_isPulseOn && (millis() - _resetPulseStart >= 10)) {
            _waveValue = 0;
            _isPulseOn = false;
            _waveActive = false;
        }
        return;
    }

    // Calculate the period duration in ticks
    float periodTicks = PPQN / _clockDividers[_dividerIndex];

    // Calculate the phase offset in ticks
    unsigned long phaseOffsetTicks = periodTicks * (_phase / 100.0);

    // Apply swing to the tick counter
    unsigned long tickCounterSwing = globalTick;

    // Calculate the tick counter with swing applied
    if (int(globalTick / periodTicks) % _swingEvery == 0) {
        tickCounterSwing = globalTick - (_swingAmounts[_swingAmountIndex] * PPQN / 96); // Since our swing is in 96th notes
    }

    // Calculate the clock divider for external clock
    int clockDividerExternal = 1 / _clockDividers[_dividerIndex];

    // Compute display blink state deterministically from the tick counter so
    // outputs with identical settings are always in phase and a tickCounter
    // reset (play start, load defaults, first external sync) auto-syncs all.
    // Cap the blink rate at x8 (PPQN/8 ticks per period): faster multipliers
    // alias against the ~20 Hz display refresh and appear to blink slower.
    // Envelope outputs follow _waveActive so the indicator is filled while the
    // envelope is running and outline when idle — no fast-blinking from the Env
    // divider slot (clockDivider=10000).
    if (_triggerMode) {
        _blinkState = _waveActive;
    } else {
        const float blinkMinPeriod = PPQN / 8.0f;  // x8 is the fastest visible blink rate
        if (_externalClock && _clockDividers[_dividerIndex] < 1) {
            unsigned long blinkDivider = max((unsigned long)clockDividerExternal, (unsigned long)8);
            _blinkState = ((_internalPulseCounter / blinkDivider) % 2 == 0);
        } else {
            float blinkPeriod = max(periodTicks, blinkMinPeriod);
            unsigned long adjustedTick = (tickCounterSwing >= phaseOffsetTicks)
                                             ? (tickCounterSwing - phaseOffsetTicks)
                                             : 0;
            _blinkState = ((adjustedTick / (unsigned long)blinkPeriod) % 2 == 0);
        }
    }

    // Calculate the pulse duration (in ticks) based on the duty cycle
    unsigned int _pulseDuration = int(periodTicks * (_dutyCycle / 100.0));
    unsigned int _externalPulseDuration = int(clockDividerExternal * (_dutyCycle / 100.0));

    // Trigger-mode outputs (AD/AR/ADSR envelopes) are fired exclusively via
    // SetExternalTrigger() → HandleTrigger(). Skip all clock-based triggering
    // to avoid the Env divider slot (clockDivider=10000 → periodTicks<1 →
    // int(periodTicks)==0 → % 0 UB fires GeneratePulse() every tick).
    if (!_triggerMode) {
        bool _isHatchet = (_waveformType == WaveformType::Hatchet2 || _waveformType == WaveformType::Hatchet4);
        // If using an external clock, generate a pulse based on the internal pulse counter
        // dirty workaround to make this work with clock dividers
        if (_externalClock && _clockDividers[_dividerIndex] < 1) {
            if (_internalPulseCounter % clockDividerExternal == 0 || _internalPulseCounter == 0) {
                GeneratePulse(PPQN, globalTick);
            } else if (!_isHatchet && _internalPulseCounter % clockDividerExternal == _externalPulseDuration) {
                StopWaveform();
            }
        } else {
            // Guard against periodTicks < 1 (would make int(periodTicks)==0, causing % 0 UB)
            int iPeriod = int(periodTicks);
            if (iPeriod > 0) {
                if ((tickCounterSwing - phaseOffsetTicks) % iPeriod == 0 || (globalTick == 0)) {
                    GeneratePulse(PPQN, globalTick);
                } else if (!_isHatchet && (tickCounterSwing - phaseOffsetTicks) % iPeriod == _pulseDuration) {
                    StopWaveform();
                }
            }
        }
    }
    // Handle the waveform generation
    switch (_waveformType) {
    case WaveformType::Triangle:
        GenerateTriangleWave(PPQN);
        break;
    case WaveformType::Sine:
        GenerateSineWave(PPQN);
        break;
    case WaveformType::Parabolic:
        GenerateParabolicWave(PPQN);
        break;
    case WaveformType::Sawtooth:
        GenerateSawtoothWave(PPQN);
        break;
    case WaveformType::Noise:
        GenerateNoiseWave(PPQN);
        break;
    case WaveformType::SmoothNoise:
        GenerateSmoothNoiseWave(PPQN);
        break;
    case WaveformType::ExpEnvelope:
        GenerateExpEnvelope(PPQN);
        break;
    case WaveformType::LogEnvelope:
        GenerateLogEnvelope(PPQN);
        break;
    case WaveformType::InvExpEnvelope:
        GenerateInvExpEnvelope(PPQN);
        break;
    case WaveformType::InvLogEnvelope:
        GenerateInvLogEnvelope(PPQN);
        break;
    case WaveformType::SampleHold:
        GenerateSampleHold(PPQN);
        break;
    case WaveformType::Hatchet2:
    case WaveformType::Hatchet4: {
        if (_waveActive) {
            int N = (_waveformType == WaveformType::Hatchet2) ? 2 : 4;
            int iPeriod = max(1, int(periodTicks));
            unsigned long adjustedTick = (tickCounterSwing >= phaseOffsetTicks)
                                             ? (tickCounterSwing - phaseOffsetTicks) : 0;
            int tickInPeriod = (int)(adjustedTick % (unsigned long)iPeriod);
            if (tickInPeriod < (int)_pulseDuration) {
                // Inside the "up" window: evenly divide into N square sub-pulses at 50% duty each
                int subPeriod   = max(1, (int)_pulseDuration / N);
                int subDuration = max(1, subPeriod / 2);
                _isPulseOn = (tickInPeriod % subPeriod) < subDuration;
            } else {
                // Inside the "down" window: always silent
                _isPulseOn = false;
            }
        } else {
            _isPulseOn = false;
        }
        break;
    }
    case WaveformType::Play:
        if (_waveActive) {
            _waveValue = MaxWaveValue;
            _isPulseOn = true;
        } else {
            _waveValue = 0;
            _isPulseOn = false;
        }
        break;
    case WaveformType::QuantizeInput:
        // Divide by 4095.0f (ADC full-scale), not MaxDACValue (output ceiling).
        // These are independent — ADC range is always 0-4095 regardless of
        // where we cap the DAC output.
        _waveValue = (_inputCV / 4095.0f) * MaxWaveValue;
        _isPulseOn = true;
        break;

    default:
        // For square wave or other types
        break;
    }
}

void Output::SetWaveformType(WaveformType type) {
    // If reverting from a trigger to a waveform, reset the divider to x1
    if (((_waveformType == WaveformType::ADEnvelope) ||
         (_waveformType == WaveformType::AREnvelope) ||
         (_waveformType == WaveformType::ADSREnvelope)) &&
        ((type != WaveformType::ADEnvelope) &&
         (type != WaveformType::AREnvelope) &&
         (type != WaveformType::ADSREnvelope))) {
        SetDivider(9);
    }
    // Set the waveform
    _waveformType = type;
    if (_waveformType == WaveformType::ADEnvelope || _waveformType == WaveformType::AREnvelope || _waveformType == WaveformType::ADSREnvelope) {
        _waveActive = false;
        _envState = EnvelopeState::Idle;
        _waveValue = 0.0f;
        _lastEnvValue = 0.0f;
        _envStartTime = 0;
        _triggerMode = true;
        SetDividerInternal(_dividerAmount - 1);  // Env slot (always the last entry)
    } else if (_waveformType == WaveformType::QuantizeInput) {
        _waveActive = true;
        _triggerMode = false;
        if (!_quantizerParams.enable) {
            // Enable quantizer if it's not already enabled
            _quantizerParams.enable = true;
            SetupQuantizer();
        }
    } else {
        // For other waveforms, disable quantizer
        _quantizerParams.enable = false;
        _triggerMode = false;
    }
}

// Check if the pulse state has changed
bool Output::HasPulseChanged() {
    bool pulseChanged = (_isPulseOn != _lastPulseState);
    _lastPulseState = _isPulseOn;
    return pulseChanged;
}

void Output::SetMasterState(bool state) {
    if (_masterState != state) {
        _masterState = state;

        // Reset all counters when state changes
        _internalPulseCounter = 0;
        _envTickCounter = 0;
        _randomTickCounter = 0;
        _euclideanStepIndex = 0;

        if (!_masterState) {
            _oldState = _state;
            _state = false;
            if (_waveformType == WaveformType::ResetTrig) {
                _waveValue = 0;
                _isPulseOn = false;
                _waveActive = false;
            }
        } else {
            _state = _oldState;
            // Only generate reset pulse when transitioning to true
            if (_waveformType == WaveformType::ResetTrig) {
                _waveValue = MaxWaveValue;
                _resetPulseStart = millis();
                _isPulseOn = true;
                _waveActive = true;
            }
        }
    }
}

// Master stop, stops all outputs but on resume, the outputs will resume to previous state
void Output::ToggleMasterState() {
    SetMasterState(!_masterState);
}

// Output Level based on the output type and pulse state
uint32_t Output::GetOutputLevel() {
    float adjustedLevel;
    float outputLevel;

#if !defined(ARDUINO_ARCH_RP2040)
    if (_outputType == OutputType::DigitalOut) {
        return _isPulseOn ? HIGH : LOW;
    } else
#endif
    {
        // floorVal: the DC floor (0V when offset=0%).
        // ceilVal:  floor + amplitude headroom — capped at MaxWaveValue so the
        //           combined signal never exceeds full scale regardless of offset.
        // This prevents digital clipping when offset > 0 (previously the offset
        // was added on top of the full-amplitude signal and then constrained,
        // creating a flat top on the waveform).
        float floorVal = (_offset / 100.0f) * MaxWaveValue;
        float ceilVal  = min(MaxWaveValue, floorVal + (_level / 100.0f) * MaxWaveValue);

        if (_waveformType == WaveformType::Square ||
            _waveformType == WaveformType::Hatchet2 ||
            _waveformType == WaveformType::Hatchet4) {
            adjustedLevel = _isPulseOn ? ceilVal : floorVal;
        } else {
            // Scale waveValue [0..MaxWaveValue] into [floorVal..ceilVal]
            adjustedLevel = floorVal + _waveValue * (ceilVal - floorVal) / MaxWaveValue;
            adjustedLevel = _isPulseOn ? adjustedLevel : floorVal;
        }

        outputLevel = adjustedLevel * MaxDACValue / MaxWaveValue;

#ifdef ENVELOPE_DEBUG
        // Probe: log ALL GetOutputLevel calls during AttackHold — regardless of
        // _waveActive — to catch if the hold phase ever outputs unexpectedly low.
        if (_triggerMode && (_envState == EnvelopeState::AttackHold || _envState == EnvelopeState::Sustain)) {
            static unsigned long _lastHoldDbgMs = 0;
            unsigned long _nowMs = millis();
            if (_nowMs - _lastHoldDbgMs >= 20) {
                _lastHoldDbgMs = _nowMs;
                Serial.printf("[HOLD_OUT ch%d] state=%d wAct=%d wv=%.2f adj=%.2f out=%.0f iPO=%d lv=%d t=%lums\n",
                    _ID, (int)_envState, (int)_waveActive, _waveValue, adjustedLevel, outputLevel,
                    (int)_isPulseOn, _level, _nowMs);
            }
        }
        // Also catch unexpected low output during Attack (but only at > halfway through attack,
        // i.e. wv should be above 50% by now if it's a late-attack drop).
        if (_triggerMode && _waveActive && outputLevel < MaxDACValue * 0.5f &&
            _envState == EnvelopeState::Attack && _waveValue > MaxWaveValue * 0.5f) {
            Serial.printf("[ATTACK_DROP ch%d] wv=%.2f out=%.0f t=%lums\n",
                _ID, _waveValue, outputLevel, millis());
        }
#endif

        if (_quantizerParams.enable) {
            // Apply quantization
            QuantizeCV(outputLevel, _oldOutputLevel, _quantizerThresholdBuff, _quantizerParams.channelSensitivity, _quantizerParams.octaveShift, &outputLevel);
        }

        // Final clamp: QuantizeCV hardcodes constrain(0,4095) internally, so
        // its output can exceed MaxDACValue.  Cap here to stay in linear range.
        outputLevel = constrain(outputLevel, 0.0f, (float)MaxDACValue);

        _oldOutputLevel = outputLevel;
        return uint32_t(outputLevel);
    }
}

// Euclidean Rhythm Functions
void Output::SetEuclidean(bool enabled) {
    _euclideanParams.enabled = enabled;
    if (_euclideanParams.enabled) {
        GeneratePattern(_euclideanParams, _euclideanRhythm);
    }
}

// Set the number of steps in the Euclidean rhythm
void Output::SetEuclideanSteps(int steps) {
    _euclideanParams.steps = constrain(steps, 1, MaxEuclideanSteps);
    // Ensure that the number of triggers is less than the number of steps
    if (_euclideanParams.triggers > _euclideanParams.steps) {
        _euclideanParams.triggers = _euclideanParams.steps;
    }
    if (_euclideanParams.pad > MaxEuclideanSteps - _euclideanParams.steps) {
        _euclideanParams.pad = MaxEuclideanSteps - _euclideanParams.steps;
    }
    if (_euclideanParams.enabled) {
        GeneratePattern(_euclideanParams, _euclideanRhythm);
    }
}

// Set the number of triggers in the Euclidean rhythm
void Output::SetEuclideanTriggers(int triggers) {
    _euclideanParams.triggers = constrain(triggers, 1, _euclideanParams.steps);
    if (_euclideanParams.enabled) {
        GeneratePattern(_euclideanParams, _euclideanRhythm);
    }
}

// Set the rotation of the Euclidean rhythm
void Output::SetEuclideanRotation(int rotation) {
    _euclideanParams.rotation = constrain(rotation, 0, _euclideanParams.steps - 1);
    if (_euclideanParams.enabled) {
        GeneratePattern(_euclideanParams, _euclideanRhythm);
    }
}

void Output::SetEuclideanPadding(int pad) {
    _euclideanParams.pad = constrain(pad, 0, MaxEuclideanSteps - _euclideanParams.steps);
    if (_euclideanParams.enabled) {
        GeneratePattern(_euclideanParams, _euclideanRhythm);
    }
}
