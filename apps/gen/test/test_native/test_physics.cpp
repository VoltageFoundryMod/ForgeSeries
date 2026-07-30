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

// ── GRAVITY rescales time ────────────────────────────────────────────────────
// The note rate is set by how often a ball reaches a wall, and the container is
// only 36 px across — so while the energy floor was an absolute 60 px/s a ball
// crossed it in ~0.4 s at every gravity, and GRAVITY was very nearly useless as
// a density control. Scaling both speed constants with sqrt(g) turns it into a
// pure rescaling of time. See PHYS_REF_GRAVITY.
//
// The two literals below mirror PARAM_GRAVITY_MIN/MAX in params.hpp, which this
// file deliberately does not include — these tests exercise the simulation, not
// the parameter plumbing.

namespace {
// Hits from container 0 over `seconds` at one gravity, everything else default.
int HitsAtGravity(float gravity, int seconds) {
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetGravity(gravity);
    c.SetBallCount(3);
    c.SetPegCount(8);
    int hits = 0;
    for (int i = 0; i < seconds * 1000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            hits++;
        }
    }
    return hits;
}
} // namespace

TEST(Physics, TheReferenceGravityIsUnscaled) {
    // The backwards-compatibility claim rests on this one: at PHYS_REF_GRAVITY
    // every derived speed is exactly the constant it always was, so a patch
    // written before the rescaling sounds identical after it.
    Container c;
    c.SetGravity(PHYS_REF_GRAVITY);
    EXPECT_FLOAT_EQ(1.0f, c.SpeedScale());
    EXPECT_FLOAT_EQ(PHYS_MIN_BOUNCE_SPEED, c.MinBounceSpeed());
    EXPECT_FLOAT_EQ(PEG_MIN_IMPACT_SPEED, c.MinImpactSpeed());
}

TEST(Physics, LowGravityIsGenuinelySlower) {
    const int kSeconds = 20;
    int fast = HitsAtGravity(PHYS_REF_GRAVITY, kSeconds);
    int slow = HitsAtGravity(30.0f, kSeconds);

    ASSERT_GT(fast, 0);
    ASSERT_GT(slow, 0) << "a slow container still has to speak — silence is the "
                          "failure the energy floor exists to prevent";

    // sqrt(30/220) = 0.37, so the rate should fall by roughly that factor.
    // Asserted as "less than half" rather than as a tight ratio because the exact
    // number is a tuning decision; the point is that GRAVITY moves it at all,
    // which before this change it essentially did not.
    EXPECT_LT(slow * 2, fast) << "gravity 30 gave " << slow << " hits against " << fast
                              << " at the reference — gravity is not rescaling time";
}

TEST(Physics, HighGravityIsGenuinelyBusier) {
    const int kSeconds = 20;
    int reference = HitsAtGravity(PHYS_REF_GRAVITY, kSeconds);
    int busy = HitsAtGravity(900.0f, kSeconds);
    EXPECT_GT(busy, reference) << "the top of the range has to be denser than the "
                                  "middle, or the control is one-sided";
}

TEST(Physics, TheMinimumGravityStillPlays) {
    // The bottom of the parameter range is where the ambient settings live, so it
    // has to be sparse without being silent — and still contain its balls.
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetGravity(5.0f);
    c.SetBallCount(2);

    int hits = 0;
    const int kSeconds = 30;
    for (int i = 0; i < kSeconds * 1000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            hits++;
        }
    }
    EXPECT_GT(hits, 0) << "the calmest setting must still be a sequencer";
    EXPECT_LT((float)hits / (float)kSeconds, 2.0f) << "...and it must actually be calm";
    for (int i = 0; i < c.GetBallCount(); i++) {
        EXPECT_TRUE(InsideContainer(c, c.GetBall(i))) << "ball " << i << " escaped";
    }
}

TEST(Physics, ImpactEnergyIsReportedInReferenceUnits) {
    // ACCENT maps a fixed 30..150 impact window onto gate level. If a slow
    // container reported its raw (small) speeds, every hit down there would read
    // as feather-light and ACCENT would collapse to a constant — so energies are
    // normalised back to reference-gravity units before they leave the physics.
    const float gravities[] = {PHYS_REF_GRAVITY, 40.0f};
    for (float g : gravities) {
        PhysicsWorld w;
        Container &c = w.Get(0);
        c.SetGravity(g);
        c.SetBallCount(3);

        float sum = 0.0f;
        int n = 0;
        for (int i = 0; i < 30000; i++) {
            w.Advance(1000UL + (unsigned long)i * 1000UL);
            int peg = -1;
            float e = 0.0f;
            if (c.ConsumeHit(peg, e)) {
                sum += e;
                n++;
            }
        }
        ASSERT_GT(n, 10) << "gravity " << g;
        float mean = sum / (float)n;
        // The measured window is ~29..170 averaging ~85 at the reference, and the
        // normalised mean has to stay in that neighbourhood at every gravity.
        EXPECT_GT(mean, 40.0f) << "mean impact energy at gravity " << g;
        EXPECT_LT(mean, 160.0f) << "mean impact energy at gravity " << g;
    }
}

// ── The factory patch is two worked examples ─────────────────────────────────
// A and B ship deliberately far apart in character so the first patch cable
// demonstrates both ends of the module's range: A the busy sequencer it has
// always been, B a slow ambient voice built out of GRAVITY, DENSITY and SPACE.
//
// The numbers mirror presetManager.hpp's LoadDefaultParams() at 120 BPM.
// presetManager.hpp cannot be included here (it needs the display manager and
// main.cpp's globals), so what this pins is the *behaviour* the two settings are
// chosen for — the settings themselves are asserted in the Rack isolation test.

TEST(Physics, FactoryContainersSitAtOppositeEndsOfTheRange) {
    PhysicsWorld w;
    Container &a = w.Get(0);
    a.SetGravity(220.0f);
    a.SetRestitution(0.72f);
    a.SetSpinGrip(0.30f);
    a.SetBallCount(3);
    a.SetPegCount(8);
    a.SetOmega(1.571f); // SPIN 8

    Container &b = w.Get(1);
    b.SetGravity(20.0f);
    b.SetRestitution(0.45f);
    b.SetSpinGrip(0.30f);
    b.SetBallCount(1);
    b.SetPegCount(5);
    b.SetOmega(0.785f); // SPIN 16
    b.SetDensity(85);
    b.SetMinGapUs(1000000UL); // SPACE 2 beats at 120 BPM

    std::vector<unsigned long> times[2];
    const int kSeconds = 120;
    for (int i = 0; i < kSeconds * 1000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        for (int c = 0; c < 2; c++) {
            int peg = -1;
            float e = 0.0f;
            if (w.Get(c).ConsumeHit(peg, e)) {
                times[c].push_back(w.SimUs());
            }
        }
    }

    float rateA = (float)times[0].size() / (float)kSeconds;
    float rateB = (float)times[1].size() / (float)kSeconds;

    // A is the module's identity and must not have drifted.
    EXPECT_GT(rateA, 4.0f) << "container A: " << rateA << " notes/sec";
    EXPECT_LT(rateA, 8.0f) << "container A: " << rateA << " notes/sec";

    // B has to be unmistakably a different instrument, not merely a bit calmer.
    EXPECT_LT(rateB, 1.0f) << "container B: " << rateB << " notes/sec";
    EXPECT_GT(rateB, 0.15f) << "container B is so sparse it reads as broken: " << rateB;
    EXPECT_GT(rateA, rateB * 8.0f) << "the two examples are not far enough apart";

    // The whole point of B's SPACE setting: its envelope (120 ms attack + 750 ms
    // decay) has to fit inside the SOONEST possible gap, not the average one.
    ASSERT_GT(times[1].size(), 10u);
    unsigned long minGap = 0xFFFFFFFFUL;
    for (size_t i = 1; i < times[1].size(); i++) {
        unsigned long gap = times[1][i] - times[1][i - 1];
        if (gap < minGap) {
            minGap = gap;
        }
    }
    EXPECT_GE(minGap, 1000000UL) << "SPACE did not hold B's floor";
    EXPECT_GT(minGap, 870000UL) << "B's 870 ms envelope cannot finish in " << minGap
                                << " us — the gate stops being a gate";
}

// ── A rotating wall must not silence the container ───────────────────────────
// The bug this guards is the one every other test in this file walked straight
// past, because they all run with omega = 0.
//
// SPIN's grip drags a ball up to the rim's own velocity. `vn` — the quantity
// that decides whether a strike speaks — is measured RELATIVE to the wall, so a
// co-rotating ball registers nothing while looking perfectly lively on screen.
// The energy floor was the safety net, and it could not see the problem at all:
// it measures ABSOLUTE speed, so it looked at a ball travelling at 226 px/s and
// correctly declined to add energy to it.
//
// Measured before the fix: SPIN 1 produced 0.02 notes/sec at EVERY gravity,
// firing once and then riding the rim silently for the rest of the run. Scaling
// the speed constants with gravity then spread the same failure up into the
// slower spins — SPIN 4 was silent at any gravity below ~80, which is exactly
// the region the ambient settings live in.
//
// The sweep is the point of this test. Any single (spin, gravity) pair looks
// fine; the failure only shows up as a hole in the grid.

namespace {
struct SilenceReport {
    float hitsPerSec;
    float longestSilenceSec;
};

SilenceReport MeasureSilence(float gravity, float omega, int seconds) {
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetGravity(gravity);
    c.SetOmega(omega);
    c.SetBallCount(3);
    c.SetPegCount(8);

    int hits = 0, gap = 0, worst = 0;
    for (int i = 0; i < seconds * 1000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            hits++;
            gap = 0;
        } else if (++gap > worst) {
            worst = gap;
        }
    }
    return {(float)hits / (float)seconds, (float)worst / 1000.0f};
}
} // namespace

TEST(Physics, NoSpinAndGravityCombinationGoesSilent) {
    // Every SPIN the menu offers, at 120 BPM, across the gravity range.
    // omega = 2*pi / (beats * 60 / bpm), i.e. SpinRateBeats from clock.hpp.
    const float omegas[] = {25.0f, 12.566f, 6.283f, 3.142f, 1.571f, 0.785f};
    const char *names[] = {"1/2", "1", "2", "4", "8", "16"};
    const float gravities[] = {5.0f, 20.0f, 60.0f, 220.0f, 600.0f, 900.0f};

    for (int s = 0; s < 6; s++) {
        for (float g : gravities) {
            // Both directions: the grip is symmetric, but the peg indexing is not.
            for (float sign : {1.0f, -1.0f}) {
                SilenceReport r = MeasureSilence(g, omegas[s] * sign, 30);
                EXPECT_GT(r.hitsPerSec, 0.2f)
                    << "SPIN " << names[s] << (sign < 0 ? " REV" : "") << " at gravity "
                    << g << " produced " << r.hitsPerSec << " notes/sec";
                EXPECT_LT(r.longestSilenceSec, 6.0f)
                    << "SPIN " << names[s] << (sign < 0 ? " REV" : "") << " at gravity "
                    << g << " went silent for " << r.longestSilenceSec << " s";
            }
        }
    }
}

TEST(Physics, TheReviveDoesNotFireInsideANormalRhythm) {
    // PHYS_REVIVE_US is a floor under silence, not a metronome. It must sit
    // outside the natural per-ball gap or it would drag a container that is
    // speaking perfectly well back up to its own tempo.
    Container c;
    c.SetGravity(PHYS_REF_GRAVITY);
    EXPECT_EQ(PHYS_REVIVE_US, c.ReviveUs());

    // The reference container's natural gap with 3 balls is ~160 ms overall, so
    // ~500 ms per ball. The revive window has to be comfortably under that to be
    // useful, and comfortably over one ball's own bounce period to be safe.
    SilenceReport withSpin = MeasureSilence(PHYS_REF_GRAVITY, 1.571f, 30); // SPIN 8
    SilenceReport without = MeasureSilence(PHYS_REF_GRAVITY, 0.0f, 30);
    EXPECT_NEAR(withSpin.hitsPerSec, without.hitsPerSec, 2.0f)
        << "a spinning container should not be dramatically busier than a still "
           "one at the same gravity — the revive is firing inside the rhythm";
}

TEST(Physics, TimeConstantsStretchAsGravityFalls) {
    // Speeds multiply by the scale, times divide by it. Leaving the refractory
    // windows absolute caps every container at 83 Hz — invisible at the
    // reference, where the natural rate is ~6 Hz, and wildly out of scale in a
    // container whose natural rate is 1 Hz, where it lets anything that does
    // chatter run completely unchecked.
    Container ref, slow;
    ref.SetGravity(PHYS_REF_GRAVITY);
    slow.SetGravity(20.0f);

    EXPECT_EQ(PEG_REFRACTORY_US, ref.RefractoryUs());
    EXPECT_EQ(PEG_MIN_INTERVAL_US, ref.MinIntervalUs());

    EXPECT_GT(slow.RefractoryUs(), ref.RefractoryUs() * 2);
    EXPECT_GT(slow.MinIntervalUs(), ref.MinIntervalUs() * 2);
    EXPECT_GT(slow.ReviveUs(), ref.ReviveUs() * 2);
}

// ── DENSITY ──────────────────────────────────────────────────────────────────
// The chance that a strike which cleared everything else actually speaks. The
// point of it is that it is the only way to thin the notes without also changing
// how the container moves.

TEST(Physics, DensityThinsTheNotes) {
    PhysicsWorld full, thin;
    full.Get(0).SetBallCount(3);
    thin.Get(0).SetBallCount(3);
    thin.Get(0).SetDensity(25);

    int fullHits = 0, thinHits = 0;
    for (int i = 0; i < 30000; i++) {
        unsigned long t = 1000UL + (unsigned long)i * 1000UL;
        full.Advance(t);
        thin.Advance(t);
        int peg = -1;
        float e = 0.0f;
        if (full.Get(0).ConsumeHit(peg, e)) {
            fullHits++;
        }
        if (thin.Get(0).ConsumeHit(peg, e)) {
            thinHits++;
        }
    }
    ASSERT_GT(fullHits, 50);
    EXPECT_LT(thinHits, fullHits / 2) << "25 % density gave " << thinHits << " of " << fullHits;
    EXPECT_GT(thinHits, 0) << "25 % is thinning, not muting";
}

TEST(Physics, DensityDoesNotDisturbTheMotion) {
    // The whole reason DENSITY exists rather than "just use fewer balls": the
    // simulation stays bit-identical, so what you see is unchanged and only what
    // you hear is thinner.
    PhysicsWorld full, thin;
    full.Get(0).SetBallCount(4);
    thin.Get(0).SetBallCount(4);
    thin.Get(0).SetDensity(30);

    RunMs(full, 8000);
    RunMs(thin, 8000);

    for (int i = 0; i < 4; i++) {
        EXPECT_FLOAT_EQ(full.Get(0).GetBall(i).x, thin.Get(0).GetBall(i).x) << "ball " << i;
        EXPECT_FLOAT_EQ(full.Get(0).GetBall(i).y, thin.Get(0).GetBall(i).y) << "ball " << i;
    }
}

TEST(Physics, ZeroDensityIsSilentButStillMoving) {
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetBallCount(3);
    c.SetDensity(0);

    int hits = 0;
    for (int i = 0; i < 20000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            hits++;
        }
    }
    EXPECT_EQ(0, hits);
    EXPECT_GT(c.Activity(), 0.0f) << "the balls have to keep bouncing — DENSITY "
                                     "silences the voice, it does not stop the sim";
}

// ── SPACE ────────────────────────────────────────────────────────────────────
// A minimum gap between two notes from one container. A rate ceiling, not a
// grid: a note arriving later than the gap is not moved onto anything.

TEST(Physics, SpaceEnforcesAMinimumGap) {
    const unsigned long kGap = 500000UL; // 500 ms — one beat at 120 BPM
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetBallCount(4); // busy enough that the gap is doing real work
    c.SetMinGapUs(kGap);

    std::vector<unsigned long> times;
    for (int i = 0; i < 30000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            times.push_back(w.SimUs());
        }
    }

    ASSERT_GT(times.size(), 10u) << "SPACE must thin the stream, not stop it";
    for (size_t i = 1; i < times.size(); i++) {
        EXPECT_GE(times[i] - times[i - 1], kGap)
            << "notes " << (i - 1) << " and " << i << " landed closer than SPACE allows";
    }
}

TEST(Physics, SpaceOffIsTheOriginalBehaviour) {
    PhysicsWorld gated, open;
    gated.Get(0).SetBallCount(3);
    open.Get(0).SetBallCount(3);
    gated.Get(0).SetMinGapUs(0);

    int gatedHits = 0, openHits = 0;
    for (int i = 0; i < 20000; i++) {
        unsigned long t = 1000UL + (unsigned long)i * 1000UL;
        gated.Advance(t);
        open.Advance(t);
        int peg = -1;
        float e = 0.0f;
        if (gated.Get(0).ConsumeHit(peg, e)) {
            gatedHits++;
        }
        if (open.Get(0).ConsumeHit(peg, e)) {
            openHits++;
        }
    }
    EXPECT_EQ(openHits, gatedHits);
}

TEST(Physics, SpaceDoesNotStarveAfterDroppingNotes) {
    // Only a note that actually SPEAKS restarts the window. If a dropped note
    // reset it too, a container busy enough to have a strike inside every window
    // would gate itself into permanent silence after its first note.
    PhysicsWorld w;
    Container &c = w.Get(0);
    c.SetBallCount(PHYS_MAX_BALLS); // as busy as it gets
    c.SetMinGapUs(250000UL);

    int hits = 0;
    for (int i = 0; i < 20000; i++) {
        w.Advance(1000UL + (unsigned long)i * 1000UL);
        int peg = -1;
        float e = 0.0f;
        if (c.ConsumeHit(peg, e)) {
            hits++;
        }
    }
    // 20 s at a 250 ms floor allows at most 80. A working gate lands near that
    // ceiling with this many balls; a starving one stops at 1.
    EXPECT_GT(hits, 40) << "SPACE starved the container after " << hits << " notes";
    EXPECT_LE(hits, 80);
}

TEST(Physics, ThinnedPhrasesStillLoopExactly) {
    // DENSITY draws from the container's PRNG and SPACE keeps a timestamp, so
    // both had to join the loop snapshot. Had either stayed outside it, a
    // "repeating" phrase would quietly gain and drop notes on every pass.
    const int kPeriod = 1000;
    PhysicsWorld w;
    w.Get(0).SetBallCount(3);
    w.Get(1).SetBallCount(2);
    w.Get(0).SetDensity(45);
    w.Get(1).SetDensity(60);
    w.Get(1).SetMinGapUs(200000UL);
    w.SetLoop(2, kPeriod, 1, 0, 0, 0);

    std::vector<LoopHit> all = RunCapturingHits(w, kPeriod * 5);
    std::vector<LoopHit> first = Slice(all, 0, kPeriod);
    ASSERT_GT(first.size(), 2u) << "the thinned phrase still has to contain notes";

    for (int rep = 1; rep < 5; rep++) {
        EXPECT_TRUE(SamePhrase(first, Slice(all, kPeriod * rep, kPeriod)))
            << "repeat " << rep << " of a thinned phrase diverged";
    }
}
