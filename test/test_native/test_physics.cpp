#include <gtest/gtest.h>

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

