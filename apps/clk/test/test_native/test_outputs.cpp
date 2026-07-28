#include <gtest/gtest.h>

#include "outputs.hpp"

class OutputTest : public ::testing::Test {
  protected:
    void SetUp() override {
        digitalOutput = new Output(0, OutputType::DigitalOut);
        dacOutput = new Output(1, OutputType::DACOut);
    }

    void TearDown() override {
        delete digitalOutput;
        delete dacOutput;
    }

    void AdvanceTime(unsigned long microseconds) {
        // Simulate time advancement for envelope tests
        for (int i = 0; i < microseconds / 1000; i++) {
            dacOutput->GenEnvelope();
        }
    }

    Output *digitalOutput;
    Output *dacOutput;
};

// Constructor Tests
TEST_F(OutputTest, ConstructorInitialization) {
    EXPECT_EQ(digitalOutput->GetOutputState(), true);
    EXPECT_EQ(digitalOutput->GetDividerIndex(), 9); // Default x1
    EXPECT_EQ(digitalOutput->GetDutyCycle(), 50);
    EXPECT_EQ(digitalOutput->GetLevel(), 100);
    EXPECT_EQ(digitalOutput->GetOffset(), 0);
    EXPECT_EQ(digitalOutput->GetWaveformType(), WaveformType::Square);
}

// Basic Output State Tests
TEST_F(OutputTest, OutputStateControl) {
    digitalOutput->SetOutputState(false);
    EXPECT_FALSE(digitalOutput->GetOutputState());
    EXPECT_FALSE(digitalOutput->GetPulseState());

    digitalOutput->ToggleOutputState();
    EXPECT_TRUE(digitalOutput->GetOutputState());
}

// Waveform Type Tests

// Test sine wave generation
// TEST_F(OutputTest, SineWaveGeneration) {
//     const int PPQN = 24;
//     dacOutput->SetWaveformType(WaveformType::Sine);
//     dacOutput->SetPulse(true);

//     // Test sine wave output
//     int lastValue = 0;
//     for (int i = 0; i < 100; i++) {
//         dacOutput->Pulse(PPQN, i);
//         int value = dacOutput->GetOutputLevel();
//         EXPECT_GT(value, lastValue);
//         lastValue = value;
//     }
// }

TEST_F(OutputTest, WaveformTypeControl) {
    // Test default waveform state
    EXPECT_FALSE(dacOutput->GetTriggerMode());

    // Test envelope trigger modes
    dacOutput->SetWaveformType(WaveformType::ADEnvelope);
    EXPECT_TRUE(dacOutput->GetTriggerMode());
    EXPECT_EQ(dacOutput->GetDividerIndex(), 18); // Should be in trigger mode

    dacOutput->SetWaveformType(WaveformType::AREnvelope);
    EXPECT_TRUE(dacOutput->GetTriggerMode());

    dacOutput->SetWaveformType(WaveformType::ADSREnvelope);
    EXPECT_TRUE(dacOutput->GetTriggerMode());

    // Test non-envelope waveform
    dacOutput->SetWaveformType(WaveformType::Triangle);
    EXPECT_FALSE(dacOutput->GetTriggerMode());
    EXPECT_EQ(dacOutput->GetDividerIndex(), 9); // Should return to normal mode
}

// Envelope Tests
// TEST_F(OutputTest, EnvelopeGenerationWithoutRetrigger) {
//     dacOutput->SetWaveformType(WaveformType::ADSREnvelope);
//     dacOutput->SetAttack(100);  // 100ms attack
//     dacOutput->SetDecay(100);   // 100ms decay
//     dacOutput->SetSustain(70);  // 70% sustain
//     dacOutput->SetRelease(100); // 100ms release
//     dacOutput->SetRetrigger(false);

//     // Trigger envelope
//     dacOutput->SetExternalTrigger(true);

//     // Check attack phase
//     AdvanceTime(50000); // 50ms into attack
//     float midAttackValue = dacOutput->GetOutputLevel();
//     EXPECT_GT(midAttackValue, 0);
//     EXPECT_LT(midAttackValue, 4095);

//     // Complete attack and into decay
//     AdvanceTime(150000);
//     float sustainValue = dacOutput->GetOutputLevel();
//     EXPECT_NEAR(sustainValue, 4095 * 0.7, 100); // Should be near 70% sustain level

//     // Release
//     dacOutput->SetExternalTrigger(false);
//     AdvanceTime(50000); // 50ms into release
//     float midReleaseValue = dacOutput->GetOutputLevel();
//     EXPECT_LT(midReleaseValue, sustainValue);

//     // Trigger again during release - should not retrigger
//     dacOutput->SetExternalTrigger(true);
//     AdvanceTime(10000);
//     EXPECT_LT(dacOutput->GetOutputLevel(), midReleaseValue);
// }

// TEST_F(OutputTest, EnvelopeGenerationWithRetrigger) {
//     dacOutput->SetWaveformType(WaveformType::ADSREnvelope);
//     dacOutput->SetAttack(100);
//     dacOutput->SetDecay(100);
//     dacOutput->SetSustain(70);
//     dacOutput->SetRelease(100);
//     dacOutput->SetRetrigger(true);

//     // Initial trigger
//     dacOutput->SetExternalTrigger(true);
//     AdvanceTime(150000); // Into decay phase
//     float firstValue = dacOutput->GetOutputLevel();

//     // Retrigger
//     dacOutput->SetExternalTrigger(false);
//     dacOutput->SetExternalTrigger(true);
//     AdvanceTime(50000);
//     float retriggeredValue = dacOutput->GetOutputLevel();
//     EXPECT_GT(retriggeredValue, 0);
// }

// Clock Division Tests
// TEST_F(OutputTest, ClockDivision) {
//     const int PPQN = 24;
//     digitalOutput->SetDivider(9); // x1

//     // Test normal timing
//     digitalOutput->Pulse(PPQN, 0);
//     EXPECT_TRUE(digitalOutput->GetPulseState());

//     digitalOutput->Pulse(PPQN, PPQN / 2);
//     EXPECT_FALSE(digitalOutput->GetPulseState());

//     // Test divided timing
//     digitalOutput->SetDivider(8); // x1/2
//     digitalOutput->Pulse(PPQN, 0);
//     EXPECT_TRUE(digitalOutput->GetPulseState());
//     digitalOutput->Pulse(PPQN, PPQN);
//     EXPECT_FALSE(digitalOutput->GetPulseState());
// }

// Euclidean Pattern Tests
TEST_F(OutputTest, EuclideanPatterns) {
    digitalOutput->SetEuclidean(true);
    digitalOutput->SetEuclideanSteps(4);
    digitalOutput->SetEuclideanTriggers(2);
    digitalOutput->SetEuclideanRotation(1);

    EXPECT_TRUE(digitalOutput->GetEuclidean());
    EXPECT_EQ(digitalOutput->GetEuclideanSteps(), 4);
    EXPECT_EQ(digitalOutput->GetEuclideanTriggers(), 2);
    EXPECT_EQ(digitalOutput->GetEuclideanRotation(), 1);

    // Test pattern generation
    int triggerCount = 0;
    for (int i = 0; i < 4; i++) {
        if (digitalOutput->GetRhythmStep(i))
            triggerCount++;
    }
    EXPECT_EQ(triggerCount, 2);
}

// DAC Output Level Tests
TEST_F(OutputTest, DACOutputLevels) {
    dacOutput->SetLevel(75);
    dacOutput->SetOffset(25);

    EXPECT_EQ(dacOutput->GetLevel(), 75);
    EXPECT_EQ(dacOutput->GetOffset(), 25);

    // Test square wave output levels
    dacOutput->SetWaveformType(WaveformType::Square);
    dacOutput->SetPulse(true);
    int highLevel = dacOutput->GetOutputLevel();
    dacOutput->SetPulse(false);
    int lowLevel = dacOutput->GetOutputLevel();

    EXPECT_GT(highLevel, lowLevel);
    EXPECT_GT(lowLevel, 0); // Due to offset
}

// Master State Control Tests
TEST_F(OutputTest, MasterStateControl) {
    digitalOutput->SetMasterState(false);
    EXPECT_FALSE(digitalOutput->GetOutputState());

    digitalOutput->SetMasterState(true);
    EXPECT_TRUE(digitalOutput->GetOutputState());

    // Test state memory
    digitalOutput->SetOutputState(false);
    digitalOutput->SetMasterState(false);
    digitalOutput->SetMasterState(true);
    EXPECT_FALSE(digitalOutput->GetOutputState());
}
