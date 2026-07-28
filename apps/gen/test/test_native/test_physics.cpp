#include <gtest/gtest.h>

#include <vector>

#include "physics.hpp"

namespace {

// Run a world forward in 1 ms slices. PhysicsWorld::Advance() caps catch-up at
// PHYS_MAX_STEPS_PER_CALL, so stepping in real 1 ms increments is the only way
// to be sure the requested amount of time is actually simulated.
void RunMs(PhysicsWorld &w, int ms, unsigned long startUs = 1000) {
    for (int i = 0; i < ms; i++) {
        w.Advance(startUs + (unsigned long)i * 1000UL);
    }
}

bool InsideContainer(const Container &c, const Ball &b) {
    float dx = b.x - c.CentreX();
    float dy = b.y - c.CentreY();
    // Allow a hair over the wall: the collision response repositions the ball
    // exactly onto (R - ballR), and float rounding can leave it a whisker out.
    float limit = PHYS_R - PHYS_BALL_R + 0.01f;
    return dx * dx + dy * dy <= limit * limit;
}

} // namespace

// ── Containment ──────────────────────────────────────────────────────────────

TEST(Physics, BallsNeverEscapeTheContainer) {
    PhysicsWorld w;
    w.Get(0).SetBallCount(PHYS_MAX_BALLS);
    w.Get(1).SetBallCount(PHYS_MAX_BALLS);
    w.Get(0).SetOmega(8.0f); // a fast spin is the case most likely to fling one out
    w.Get(1).SetOmega(-8.0f);

    RunMs(w, 5000);

    for (int ci = 0; ci < 2; ci++) {
        const Container &c = w.Get(ci);
        for (int i = 0; i < c.GetBallCount(); i++) {
            EXPECT_TRUE(InsideContainer(c, c.GetBall(i)))
                << "container " << ci << " ball " << i << " escaped";
        }
    }
}

TEST(Physics, HighGravityStillContains) {
    // Tunnelling is a timestep problem: the faster the ball, the more likely it
    // crosses the wall within one step. Max gravity is the worst case.
    PhysicsWorld w;
    w.Get(0).SetGravity(1200.0f);
    w.Get(0).SetRestitution(0.99f);
    w.Get(0).SetBallCount(PHYS_MAX_BALLS);

    RunMs(w, 4000);

    const Container &c = w.Get(0);
    for (int i = 0; i < c.GetBallCount(); i++) {
        EXPECT_TRUE(InsideContainer(c, c.GetBall(i))) << "ball " << i << " tunnelled";
    }
}

// ── Determinism (required for the VCV port to match the hardware) ────────────

TEST(Physics, IsDeterministic) {
    PhysicsWorld a, b;
    RunMs(a, 2000);
    RunMs(b, 2000);

    for (int ci = 0; ci < 2; ci++) {
        for (int i = 0; i < a.Get(ci).GetBallCount(); i++) {
            EXPECT_FLOAT_EQ(a.Get(ci).GetBall(i).x, b.Get(ci).GetBall(i).x);
            EXPECT_FLOAT_EQ(a.Get(ci).GetBall(i).y, b.Get(ci).GetBall(i).y);
        }
    }
}

// ── Peg mapping ──────────────────────────────────────────────────────────────

TEST(Physics, PegAtContactSplitsTheCircleEvenly) {
    Container c;
    c.SetPegCount(4);
    // Rotation 0: peg 0 starts at angle 0 (3 o'clock) and they run clockwise in
    // screen coordinates, where +y is down.
    EXPECT_EQ(0, c.PegAtContact(1.0f, 0.0f));   // 0
    EXPECT_EQ(1, c.PegAtContact(0.0f, 1.0f));   // pi/2
    EXPECT_EQ(2, c.PegAtContact(-1.0f, 0.0f));  // pi
    EXPECT_EQ(3, c.PegAtContact(0.0f, -1.0f));  // 3pi/2
}

TEST(Physics, PegIndexAlwaysInRange) {
    Container c;
    for (int pegs = PHYS_MIN_PEGS; pegs <= PHYS_MAX_PEGS; pegs++) {
        c.SetPegCount(pegs);
        for (int deg = 0; deg < 360; deg += 7) {
            float a = (float)deg * 3.14159265f / 180.0f;
            int p = c.PegAtContact(cosf(a), sinf(a));
            EXPECT_GE(p, 0);
            EXPECT_LT(p, pegs) << "pegs=" << pegs << " deg=" << deg;
        }
    }
}

// ── Note events ──────────────────────────────────────────────────────────────

TEST(Physics, ProducesHitsAndTheyAreInRange) {
    PhysicsWorld w;
    w.Get(0).SetPegCount(8);
    w.Get(0).SetBallCount(3);

    int hits = 0;
    for (int i = 0; i < 5000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float energy = 0.0f;
        if (w.Get(0).ConsumeHit(peg, energy)) {
            hits++;
            EXPECT_GE(peg, 0);
            EXPECT_LT(peg, 8);
            EXPECT_GT(energy, 0.0f);
        }
    }
    EXPECT_GT(hits, 5) << "5 seconds of falling balls should produce notes";
}

TEST(Physics, MutedPegsAreSilentButStillBounce) {
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetPegCount(8);
    c.SetBallCount(4);
    c.SetPegMask(0); // every peg muted

    int hits = 0;
    for (int i = 0; i < 5000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            hits++;
        }
    }
    EXPECT_EQ(0, hits) << "muted pegs must not emit notes";
    // ...but the balls are still bouncing around inside, not frozen or escaped.
    for (int i = 0; i < c.GetBallCount(); i++) {
        EXPECT_TRUE(InsideContainer(c, c.GetBall(i)));
    }
}

TEST(Physics, BallsDoNotSettleIntoSilence) {
    // The energy floor exists so a patch never dies. Low bounce + no spin is the
    // configuration that would otherwise pile the balls up at the bottom.
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetRestitution(0.10f);
    c.SetOmega(0.0f);
    c.SetBallCount(2);
    c.SetPegCount(6);

    RunMs(w, 8000); // let it settle as far as it is going to

    int hits = 0;
    for (int i = 0; i < 4000; i++) {
        w.Advance(9000000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            hits++;
        }
    }
    EXPECT_GT(hits, 0) << "the sequencer went permanently silent";
}

// ── Proximity / coupling geometry ────────────────────────────────────────────

TEST(Physics, ProximityMovesTheContainersTogether) {
    PhysicsWorld w;

    w.SetProximity(0.0f);
    EXPECT_FLOAT_EQ(PHYS_D_MAX, w.Separation());
    EXPECT_FLOAT_EQ(0.0f, w.Overlap());
    EXPECT_FLOAT_EQ(0.0f, w.OverlapArc()) << "apart: no shared arc";
    EXPECT_LT(w.Get(0).CentreX(), w.Get(1).CentreX());

    w.SetProximity(1.0f);
    EXPECT_FLOAT_EQ(PHYS_D_MIN, w.Separation());
    EXPECT_FLOAT_EQ(1.0f, w.Overlap());
    EXPECT_FLOAT_EQ(w.Get(0).CentreX(), w.Get(1).CentreX()) << "merged: concentric";
    EXPECT_NEAR(3.14159265f, w.OverlapArc(), 1e-4f) << "merged: the whole rim is shared";
}

TEST(Physics, OverlapArcGrowsWithProximity) {
    PhysicsWorld w;
    float prev = -1.0f;
    for (float p = 0.0f; p <= 1.0f; p += 0.1f) {
        w.SetProximity(p);
        float arc = w.OverlapArc();
        EXPECT_GE(arc, prev) << "arc must grow monotonically, at proximity " << p;
        prev = arc;
    }
}

// Coupling has to be measurably strong, not merely non-zero. The original
// version of this test only asserted the two worlds diverged by >0.01 px, which
// any chaotic perturbation satisfies — it would have passed even if the effect
// were imperceptible. Measured at full proximity: ~12 transmitted strikes per
// second, displacing balls by ~13 px on a 40 px container.
TEST(Physics, CouplingActuallyPerturbsTheOtherContainer) {
    // Same geometry in both worlds — the ONLY difference is the couple amount,
    // so any divergence is attributable to coupling and nothing else.
    PhysicsWorld uncoupled, coupled;
    for (PhysicsWorld *w : {&uncoupled, &coupled}) {
        w->SetProximity(1.0f);
        w->Get(0).SetOmega(1.5f);
        w->Get(1).SetOmega(-1.5f);
    }
    uncoupled.SetCoupling(0.0f);
    coupled.SetCoupling(1.0f);

    RunMs(uncoupled, 5000);
    RunMs(coupled, 5000);

    EXPECT_EQ(0UL, uncoupled.CouplingEvents()) << "couple 0 must transmit nothing";
    EXPECT_GT(coupled.CouplingEvents(), 20UL)
        << "5 s of merged containers should transmit many strikes";

    float worst = 0.0f;
    for (int ci = 0; ci < 2; ci++) {
        for (int i = 0; i < uncoupled.Get(ci).GetBallCount(); i++) {
            float dx = uncoupled.Get(ci).GetBall(i).x - coupled.Get(ci).GetBall(i).x;
            float dy = uncoupled.Get(ci).GetBall(i).y - coupled.Get(ci).GetBall(i).y;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > worst) {
                worst = d;
            }
        }
    }
    EXPECT_GT(worst, 2.0f) << "coupling barely moved the neighbouring container";
}

// The spark is what makes the coupling legible on screen, so it has to actually
// be raised — an invisible effect is the bug being fixed here.
TEST(Physics, CouplingRaisesAVisibleSpark) {
    PhysicsWorld w;
    w.SetProximity(1.0f);
    w.SetCoupling(1.0f);

    bool sawSpark = false;
    for (int i = 0; i < 5000 && !sawSpark; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        if (w.CoupleFlashActive()) {
            sawSpark = true;
            // It must be somewhere sane on screen, not at the origin.
            EXPECT_GT(w.CoupleX(), 0.0f);
            EXPECT_GT(w.CoupleY(), 0.0f);
        }
    }
    EXPECT_TRUE(sawSpark) << "coupling never raised a spark for the renderer";

    // And it fades rather than latching on for ever.
    for (int i = 0; i < 8; i++) {
        w.DecayCoupleFlash();
    }
    EXPECT_FALSE(w.CoupleFlashActive());
}

TEST(Physics, NoSparkWhenTheContainersAreApart) {
    PhysicsWorld w;
    w.SetProximity(0.0f);
    w.SetCoupling(1.0f);
    RunMs(w, 5000);
    EXPECT_EQ(0UL, w.CouplingEvents());
    EXPECT_FALSE(w.CoupleFlashActive());
}

TEST(Physics, NoCouplingWhenContainersAreApart) {
    // Coupling turned fully up but the containers separated: they must not
    // influence each other at all, or PROXIMITY 0 would not mean "independent".
    PhysicsWorld a, b;
    a.SetProximity(0.0f);
    a.SetCoupling(0.0f);
    b.SetProximity(0.0f);
    b.SetCoupling(1.0f);

    RunMs(a, 3000);
    RunMs(b, 3000);

    for (int ci = 0; ci < 2; ci++) {
        for (int i = 0; i < a.Get(ci).GetBallCount(); i++) {
            EXPECT_FLOAT_EQ(a.Get(ci).GetBall(i).x, b.Get(ci).GetBall(i).x);
            EXPECT_FLOAT_EQ(a.Get(ci).GetBall(i).y, b.Get(ci).GetBall(i).y);
        }
    }
}

// ── Catch-up guard ───────────────────────────────────────────────────────────

TEST(Physics, LongStallDoesNotRunAwayCatchingUp) {
    // A starved caller (slow I2C, display flush) must cost bounded work, not an
    // unbounded catch-up loop. Ten simulated seconds in one call is the abuse
    // case; the world should stay sane and contained.
    PhysicsWorld w;
    w.Advance(1000UL);
    w.Advance(10000000UL);

    const Container &c = w.Get(0);
    for (int i = 0; i < c.GetBallCount(); i++) {
        EXPECT_TRUE(InsideContainer(c, c.GetBall(i)));
        EXPECT_FALSE(std::isnan(c.GetBall(i).x));
        EXPECT_FALSE(std::isnan(c.GetBall(i).y));
    }
}

// ── Ball / peg count changes ─────────────────────────────────────────────────

TEST(Physics, AddingBallsPlacesThemInsideTheContainer) {
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetBallCount(1);
    RunMs(w, 500);

    c.SetBallCount(PHYS_MAX_BALLS); // the newly added ones must not appear at (0,0)
    for (int i = 0; i < c.GetBallCount(); i++) {
        EXPECT_TRUE(InsideContainer(c, c.GetBall(i))) << "newly spawned ball " << i;
    }
}

TEST(Physics, CountsAreClamped) {
    Container c;
    c.SetBallCount(999);
    EXPECT_EQ(PHYS_MAX_BALLS, c.GetBallCount());
    c.SetBallCount(-5);
    EXPECT_EQ(PHYS_MIN_BALLS, c.GetBallCount());
    c.SetPegCount(999);
    EXPECT_EQ(PHYS_MAX_PEGS, c.GetPegCount());
    c.SetPegCount(0);
    EXPECT_EQ(PHYS_MIN_PEGS, c.GetPegCount());
}

// ── Hit rate ─────────────────────────────────────────────────────────────────
// "Not silent" is not enough on its own: every peg hit retriggers the channel's
// envelope, so a container that machine-guns turns the GATE jack into a mess
// rather than triggers.
//
// This originally measured ~33 hits/sec across 3 balls, which turned the GATE
// jack into a continuous sawtooth: a ball settling against a rotating wall
// registered a contact every step, and rotation kept sliding a new peg under it
// so the short re-trigger window never applied. PEG_MIN_IMPACT_SPEED (only a
// real strike speaks) plus a higher energy floor (a settled ball makes proper
// slow bounces instead of vibrating) brought it to ~7 hits/sec.
//
// The bound is set as a runaway guard rather than a tightness check — musical
// density is a tuning decision — but it is now tight enough to catch a
// regression back into chatter. See Sequencer.GateReturnsToZeroBetweenHits for
// the audible consequence.
TEST(Physics, HitRateDoesNotRunAway) {
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetBallCount(3);
    c.SetPegCount(8);

    int hits = 0;
    const int seconds = 10;
    for (int i = 0; i < seconds * 1000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            hits++;
        }
    }
    float perSec = (float)hits / (float)seconds;
    EXPECT_LT(perSec, 20.0f) << "hits/sec with 3 balls at default settings";
    EXPECT_GT(perSec, 0.2f) << "hits/sec with 3 balls at default settings";
}


// ── Transmitted strikes ring the receiving container ─────────────────────────
// A strike that crosses the overlap arrives at a point on the other rim, and the
// peg there speaks. The note is entirely the receiving container's own — its peg
// ring, its scale, its muted pegs — so a transfer can never sound out of key.

namespace {
// Hits produced by each container over `seconds`, at a given proximity/couple.
void CountHits(float proximity, float coupling, int seconds, int outHits[2],
               unsigned long *events = nullptr) {
    PhysicsWorld w;
    w.SetProximity(proximity);
    w.SetCoupling(coupling);
    w.Get(0).SetBallCount(3);
    w.Get(1).SetBallCount(2);
    w.Get(0).SetPegCount(8);
    w.Get(1).SetPegCount(5);
    w.Get(0).SetOmega(0.8f);
    w.Get(1).SetOmega(-1.5f);
    outHits[0] = outHits[1] = 0;
    for (int i = 0; i < seconds * 1000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        for (int c = 0; c < 2; c++) {
            int peg = -1;
            float e = 0.0f;
            if (w.Get(c).ConsumeHit(peg, e)) {
                outHits[c]++;
            }
        }
    }
    if (events) {
        *events = w.CouplingEvents();
    }
}
} // namespace

TEST(Physics, TransmittedStrikesAddNotesToTheReceivingContainer) {
    int off[2], on[2];
    CountHits(1.0f, 0.0f, 10, off); // merged, but nothing transmits
    CountHits(1.0f, 1.0f, 10, on);  // merged and fully coupled

    // Both containers should gain notes: the transfer is symmetric.
    EXPECT_GT(on[0], off[0] + 10) << "container A gained no transmitted notes";
    EXPECT_GT(on[1], off[1] + 10) << "container B gained no transmitted notes";

    // ...but it must not double into a machine-gun. Measured ~10/s vs ~5/s.
    EXPECT_LT(on[0] / 10.0f, 20.0f) << "transmitted notes ran away on A";
    EXPECT_LT(on[1] / 10.0f, 20.0f) << "transmitted notes ran away on B";
}

TEST(Physics, WeakCouplingStaysPhysicsOnly) {
    // Transmitted energy is scaled by COUPLE before the impact test, so a light
    // coupling only nudges trajectories. That gradient is what makes COUPLE a
    // musical control rather than an on/off switch.
    int weak[2];
    unsigned long events = 0;
    CountHits(0.6f, 0.3f, 10, weak, &events);
    EXPECT_GT(events, 10UL) << "energy should still be transmitting at couple 30%";
    EXPECT_LT(weak[1] / 10.0f, 6.0f) << "weak coupling should not be ringing pegs";
}

TEST(Physics, TransmittedStrikesRespectMutedPegs) {
    // Muting a peg on the receiving container must absorb the transfer silently —
    // that is how you shape which transfers are audible.
    PhysicsWorld w;
    w.SetProximity(1.0f);
    w.SetCoupling(1.0f);
    w.Get(1).SetPegMask(0); // every peg on B muted
    w.Get(1).SetBallCount(0 + PHYS_MIN_BALLS);

    int bHits = 0;
    for (int i = 0; i < 10000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (w.Get(1).ConsumeHit(peg, e)) {
            bHits++;
        }
    }
    EXPECT_GT(w.CouplingEvents(), 20UL) << "energy should still be transmitting";
    EXPECT_EQ(0, bHits) << "muted pegs must absorb transmitted strikes silently";
}

TEST(Physics, TransmittedNotesUseTheReceivingContainersOwnPegRing) {
    // The peg index must come from the receiving container's ring, so it is
    // always a peg that container actually has.
    PhysicsWorld w;
    w.SetProximity(1.0f);
    w.SetCoupling(1.0f);
    w.Get(0).SetPegCount(16);
    w.Get(1).SetPegCount(3); // deliberately mismatched rings

    for (int i = 0; i < 10000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (w.Get(1).ConsumeHit(peg, e)) {
            EXPECT_GE(peg, 0);
            EXPECT_LT(peg, 3) << "a transmitted note used the sender's peg ring";
        }
    }
}


// ── Loop / phrase mode ───────────────────────────────────────────────────────
//
// The feature rests entirely on the simulation being reproducible: capture the
// state, run N steps, put the state back, and the same N steps have to produce
// the same notes. These tests pin that, because a loop that repeats *almost*
// exactly is worse than no loop at all — it sounds like a fault.

namespace {

struct LoopHit {
    int step; // milliseconds since the run started
    int container;
    int peg;
};

// Step a world one millisecond at a time, draining note events as the firmware
// does, and record every hit with the step it landed on.
std::vector<LoopHit> RunCapturingHits(PhysicsWorld &w, int ms, unsigned long startUs = 1000) {
    std::vector<LoopHit> hits;
    for (int i = 0; i < ms; i++) {
        w.Advance(startUs + (unsigned long)i * 1000UL);
        for (int c = 0; c < 2; c++) {
            int peg = -1;
            float energy = 0.0f;
            if (w.Get(c).ConsumeHit(peg, energy)) {
                hits.push_back({i, c, peg});
            }
        }
    }
    return hits;
}

// The hits that fell inside [from, from+len), re-based so two loops can be
// compared step for step.
std::vector<LoopHit> Slice(const std::vector<LoopHit> &all, int from, int len) {
    std::vector<LoopHit> out;
    for (const LoopHit &h : all) {
        if (h.step >= from && h.step < from + len) {
            out.push_back({h.step - from, h.container, h.peg});
        }
    }
    return out;
}

// Do two windows of hits agree note for note, step for step?
bool SamePhrase(const std::vector<LoopHit> &a, const std::vector<LoopHit> &b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i].step != b[i].step || a[i].container != b[i].container ||
            a[i].peg != b[i].peg) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST(Physics, LoopReplaysTheSamePhraseExactly) {
    // 1000 steps = 1000 ms = two beats at 120 BPM, which is what ApplyParams()
    // works out from LOOP BEATS 2.
    const int kPeriod = 1000;
    PhysicsWorld w;
    w.Get(0).SetBallCount(3);
    w.Get(1).SetBallCount(2);
    w.SetLoop(2, kPeriod, 1, 0, 0, 0);

    std::vector<LoopHit> all = RunCapturingHits(w, kPeriod * 4);

    std::vector<LoopHit> first = Slice(all, 0, kPeriod);
    ASSERT_GT(first.size(), 5u) << "the phrase has to contain notes to be worth looping";

    // Every later repeat must be note-for-note, step-for-step identical to it.
    for (int rep = 1; rep < 4; rep++) {
        std::vector<LoopHit> later = Slice(all, kPeriod * rep, kPeriod);
        ASSERT_EQ(first.size(), later.size()) << "repeat " << rep << " changed length";
        for (size_t i = 0; i < first.size(); i++) {
            EXPECT_EQ(first[i].step, later[i].step) << "repeat " << rep << " hit " << i;
            EXPECT_EQ(first[i].container, later[i].container) << "repeat " << rep;
            EXPECT_EQ(first[i].peg, later[i].peg) << "repeat " << rep << " hit " << i;
        }
    }
}

TEST(Physics, LoopReplaysThePhraseWhileCoupled) {
    // Coupling is the one path that reads state across containers, so a phrase
    // is only really deterministic if it repeats with PROXIMITY up too.
    const int kPeriod = 1000;
    PhysicsWorld w;
    w.SetProximity(0.9f);
    w.SetCoupling(1.0f);
    w.Get(0).SetBallCount(3);
    w.Get(1).SetBallCount(3);
    w.SetLoop(2, kPeriod, 1, 0, 0, 0);

    std::vector<LoopHit> all = RunCapturingHits(w, kPeriod * 3);
    std::vector<LoopHit> first = Slice(all, 0, kPeriod);
    ASSERT_GT(first.size(), 5u);
    EXPECT_TRUE(SamePhrase(first, Slice(all, kPeriod, kPeriod)));
    EXPECT_TRUE(SamePhrase(first, Slice(all, kPeriod * 2, kPeriod)));
}

TEST(Physics, LoopRewindRestoresTheBallsAndTheRotation) {
    // The audible test above still passes if a little state leaks across the
    // boundary. This one pins the state itself.
    const int kPeriod = 800;
    PhysicsWorld w;
    w.SetLoop(2, kPeriod, 1, 0, 0, 0);

    // Halfway through the first phrase.
    for (int i = 0; i < kPeriod / 2; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
    }
    float x[2], y[2], vx[2], rot[2];
    for (int c = 0; c < 2; c++) {
        x[c] = w.Get(c).GetBall(0).x;
        y[c] = w.Get(c).GetBall(0).y;
        vx[c] = w.Get(c).GetBall(0).vx;
        rot[c] = w.Get(c).Rotation();
    }

    // Halfway through the second phrase — the same point in the loop.
    for (int i = kPeriod / 2; i < kPeriod + kPeriod / 2; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
    }
    for (int c = 0; c < 2; c++) {
        EXPECT_FLOAT_EQ(x[c], w.Get(c).GetBall(0).x) << "container " << c;
        EXPECT_FLOAT_EQ(y[c], w.Get(c).GetBall(0).y) << "container " << c;
        EXPECT_FLOAT_EQ(vx[c], w.Get(c).GetBall(0).vx) << "container " << c;
        EXPECT_FLOAT_EQ(rot[c], w.Get(c).Rotation()) << "container " << c;
    }
}

TEST(Physics, LoopOffLetsThePhraseKeepEvolving) {
    // The counter-test: without the loop, two consecutive windows of the same
    // length must NOT agree, or the sim is not generating anything and the test
    // above is proving nothing.
    const int kPeriod = 1000;
    PhysicsWorld w;
    w.Get(0).SetBallCount(3);

    std::vector<LoopHit> all = RunCapturingHits(w, kPeriod * 2);
    EXPECT_FALSE(SamePhrase(Slice(all, 0, kPeriod), Slice(all, kPeriod, kPeriod)))
        << "free-running physics repeated itself exactly";
}

TEST(Physics, LoopSurvivesAnUnevenCallerCadence) {
    // The hardware never calls Advance() on a tidy 1 ms grid — the display, the
    // I2C writes and the encoder all steal time. The rewind is scheduled on STEP
    // count, not wall time, so a lumpy caller must produce the same phrase as a
    // smooth one.
    const int kPeriod = 600;
    PhysicsWorld smooth, lumpy;
    smooth.SetLoop(2, kPeriod, 1, 0, 0, 0);
    lumpy.SetLoop(2, kPeriod, 1, 0, 0, 0);

    // Both run to the same final timestamp — three phrases' worth — so they are
    // compared over exactly the same amount of simulated time. Only the size of
    // the caller's steps differs.
    const unsigned long kEndUs = (unsigned long)kPeriod * 3UL * 1000UL;
    for (unsigned long t = 1000UL; t <= kEndUs; t += 1000UL) {
        smooth.Advance(t);
    }
    for (unsigned long t = 3000UL; t <= kEndUs; t += 3000UL) {
        lumpy.Advance(t);
    }

    for (int c = 0; c < 2; c++) {
        EXPECT_FLOAT_EQ(smooth.Get(c).GetBall(0).x, lumpy.Get(c).GetBall(0).x)
            << "container " << c << " diverged with a lumpy caller";
        EXPECT_FLOAT_EQ(smooth.Get(c).GetBall(0).y, lumpy.Get(c).GetBall(0).y)
            << "container " << c;
    }
}

TEST(Physics, NapMutesWholeLoopsAndShiftOffsetsThem) {
    // Wake 1 / nap 1 with B shifted by one loop is the call-and-response the
    // page is designed around: exactly one container awake at a time.
    const int kPeriod = 200;
    PhysicsWorld w;
    w.SetLoop(1, kPeriod, 1, 1, 0, 1);

    bool sawAwake = false, sawAsleep = false;
    for (int loop = 0; loop < 6; loop++) {
        for (int i = 0; i < kPeriod; i++) {
            w.Advance(1000UL + (unsigned long)(loop * kPeriod + i) * 1000UL);
        }
        EXPECT_NE(w.LoopMuted(0), w.LoopMuted(1))
            << "loop " << loop << ": A and B should trade, never overlap";
        sawAwake = sawAwake || !w.LoopMuted(0);
        sawAsleep = sawAsleep || w.LoopMuted(0);
    }
    EXPECT_TRUE(sawAwake) << "A never woke up";
    EXPECT_TRUE(sawAsleep) << "A never napped";
}

TEST(Physics, NapOffKeepsBothContainersAwake) {
    const int kPeriod = 200;
    PhysicsWorld w;
    w.SetLoop(1, kPeriod, 1, 0, 0, 1); // nap 0 — shift must not matter

    for (int i = 0; i < kPeriod * 5; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        ASSERT_FALSE(w.LoopMuted(0));
        ASSERT_FALSE(w.LoopMuted(1));
    }
}

TEST(Physics, NappingContainersKeepBouncing) {
    // A nap is a rest in the OUTPUT, not a pause in the simulation. If the
    // physics stopped, the phrase would come back out of phase.
    const int kPeriod = 200;
    PhysicsWorld w;
    w.SetLoop(1, kPeriod, 1, 1, 0, 0);

    // Run to a loop where A is asleep.
    int i = 0;
    for (; i < kPeriod * 4 && !w.LoopMuted(0); i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
    }
    ASSERT_TRUE(w.LoopMuted(0)) << "A never napped";

    float before = w.Get(0).GetBall(0).y;
    for (int n = 0; n < 50; n++, i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
    }
    EXPECT_NE(before, w.Get(0).GetBall(0).y) << "the balls froze during a nap";
}

TEST(Physics, TurningTheLoopOffResumesFreeRunning) {
    const int kPeriod = 500;
    PhysicsWorld w;
    w.SetLoop(2, kPeriod, 1, 0, 0, 0);
    for (int i = 0; i < kPeriod * 2; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
    }
    EXPECT_TRUE(w.LoopActive());

    w.SetLoop(0, 0, 1, 0, 0, 0);
    unsigned long base = 1000UL + (unsigned long)(kPeriod * 2) * 1000UL;
    std::vector<LoopHit> all = RunCapturingHits(w, kPeriod * 2, base);
    EXPECT_FALSE(w.LoopActive());
    EXPECT_FALSE(SamePhrase(Slice(all, 0, kPeriod), Slice(all, kPeriod, kPeriod)))
        << "the phrase kept repeating after LOOP was set to OFF";
}

TEST(Physics, ChangingTheLengthCapturesANewPhrase) {
    // A new length has to mean a new phrase starting now, not the old snapshot
    // replayed against a different period.
    const int kPeriod = 400;
    PhysicsWorld w;
    w.SetLoop(1, kPeriod, 1, 0, 0, 0);
    for (int i = 0; i < kPeriod + kPeriod / 2; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
    }
    ASSERT_GT(w.LoopNumber(), 0UL) << "should have completed at least one repeat";

    w.SetLoop(2, kPeriod * 2, 1, 0, 0, 0);
    w.Advance(1000UL + (unsigned long)(kPeriod + kPeriod / 2) * 1000UL);
    EXPECT_EQ(0UL, w.LoopNumber()) << "a length change must re-arm the loop";
    EXPECT_EQ(2, w.LoopBeats());
}

TEST(Physics, ATempoChangeDoesNotRearmTheLoop) {
    // ApplyParams() recomputes the period every pass, so an external clock
    // wandering by a BPM re-sends a slightly different step count constantly. If
    // that re-armed the loop it would never repeat at all — the failure mode
    // this guards is "LOOP does nothing when synced to an external clock".
    const int kPeriod = 400;
    PhysicsWorld w;
    w.SetLoop(2, kPeriod, 1, 0, 0, 0);
    for (int i = 0; i < kPeriod * 3; i++) {
        // Jitter the requested period the way a live tempo reading would.
        w.SetLoop(2, (unsigned long)(kPeriod + (i % 5) - 2), 1, 0, 0, 0);
        w.Advance(1000UL + (unsigned long)i * 1000UL);
    }
    EXPECT_GT(w.LoopNumber(), 1UL) << "tempo jitter kept re-arming the loop";
}

TEST(Physics, ResetRearmsTheLoop) {
    // RESET (and Randomize, which calls it) replaces the balls, so the captured
    // phrase no longer describes anything that exists.
    const int kPeriod = 300;
    PhysicsWorld w;
    w.SetLoop(1, kPeriod, 1, 0, 0, 0);
    for (int i = 0; i < kPeriod * 3; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
    }
    ASSERT_GT(w.LoopNumber(), 0UL);

    w.Reset();
    EXPECT_EQ(0UL, w.LoopNumber());
}

TEST(Physics, LoopBeatCountsThroughThePhrase) {
    // The home screen's "L n/N" badge: it has to walk 1..N and never overrun.
    const int kPeriod = 400; // 4 beats of 100 steps
    PhysicsWorld w;
    w.SetLoop(4, kPeriod, 1, 0, 0, 0);

    bool seen[5] = {false, false, false, false, false};
    for (int i = 0; i < kPeriod * 2; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int b = w.LoopBeat();
        ASSERT_GE(b, 1);
        ASSERT_LE(b, 4);
        seen[b] = true;
    }
    for (int b = 1; b <= 4; b++) {
        EXPECT_TRUE(seen[b]) << "beat " << b << " never displayed";
    }
}

// ── Snapshot mechanics ───────────────────────────────────────────────────────

TEST(Physics, SnapshotRoundTripsTheContainerState) {
    Container c;
    c.SetBallCount(4);
    c.SetOmega(3.0f);
    Contact contacts[PHYS_MAX_BALLS];
    int count = 0;
    for (int i = 0; i < 500; i++) {
        count = 0;
        c.Step(1000UL + (unsigned long)i * 1000UL, contacts, count, PHYS_MAX_BALLS);
    }

    ContainerSnapshot snap;
    const unsigned long tSave = 501000UL;
    c.SaveSnapshot(snap, tSave);

    float x0 = c.GetBall(0).x, rot0 = c.Rotation();

    // Move on, then rewind — much later, so a snapshot that stored absolute hit
    // timestamps rather than ages would show up here.
    for (int i = 500; i < 1500; i++) {
        count = 0;
        c.Step(1000UL + (unsigned long)i * 1000UL, contacts, count, PHYS_MAX_BALLS);
    }
    ASSERT_NE(x0, c.GetBall(0).x) << "the container did not move; nothing was tested";

    const unsigned long tLoad = 1501000UL;
    c.RestoreSnapshot(snap, tLoad);
    EXPECT_FLOAT_EQ(x0, c.GetBall(0).x);
    EXPECT_FLOAT_EQ(rot0, c.Rotation());

    // Ages, not timestamps: the restored refractory clock has to sit the same
    // distance behind "now" as it did when it was captured.
    for (int i = 0; i < PHYS_MAX_BALLS; i++) {
        EXPECT_EQ(snap.balls[i].hitAgeUs, tLoad - c.GetBall(i).lastHitUs)
            << "ball " << i << " came back with a stale refractory clock";
    }
}

TEST(Physics, SnapshotRestoreReproducesTheNextSteps) {
    // The property the loop actually depends on: restore, and the same steps
    // produce the same motion.
    Container c;
    c.SetBallCount(3);
    c.SetOmega(2.0f);
    Contact contacts[PHYS_MAX_BALLS];
    int count = 0;

    for (int i = 0; i < 300; i++) {
        count = 0;
        c.Step(1000UL + (unsigned long)i * 1000UL, contacts, count, PHYS_MAX_BALLS);
    }

    ContainerSnapshot snap;
    c.SaveSnapshot(snap, 301000UL);

    float firstX[PHYS_MAX_BALLS];
    for (int i = 300; i < 600; i++) {
        count = 0;
        c.Step(1000UL + (unsigned long)i * 1000UL, contacts, count, PHYS_MAX_BALLS);
    }
    for (int b = 0; b < c.GetBallCount(); b++) {
        firstX[b] = c.GetBall(b).x;
    }

    c.RestoreSnapshot(snap, 301000UL);
    for (int i = 300; i < 600; i++) {
        count = 0;
        c.Step(1000UL + (unsigned long)i * 1000UL, contacts, count, PHYS_MAX_BALLS);
    }
    for (int b = 0; b < c.GetBallCount(); b++) {
        EXPECT_FLOAT_EQ(firstX[b], c.GetBall(b).x) << "ball " << b << " replayed differently";
    }
}
