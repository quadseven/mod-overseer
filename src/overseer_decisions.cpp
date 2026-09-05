/*
 * The definitions for src/overseer_decisions.h. See that header for why these
 * live outside mod_overseer.cpp at all.
 *
 * This file includes its own header FIRST and then nothing, which is the point
 * of it: if a core type ever gets into one of these decisions, this
 * translation unit stops compiling here rather than compiling anyway inside
 * the module's own. The comments explaining each decision are in the header,
 * next to the declaration a caller reads.
 */

#include "overseer_decisions.h"

namespace OverseerDecisions
{

std::map<std::string, uint32_t> QuestAimsAfterRead(
    std::map<std::string, uint32_t> const& previous,
    std::map<std::string, uint32_t> const& loaded, bool readSucceeded)
{
    return readSucceeded ? loaded : previous;
}

namespace
{

// THESE THREE ARE HAND-ROLLED RATHER THAN <cctype>'s, and that is not
// squeamishness about one more include. This translation unit includes its own
// header and nothing else on purpose - that is the property the header says it
// is protecting - and the moment a second include is normal here, the argument
// for refusing the third one is weaker. Character classification over ASCII is
// four comparisons; it is not worth spending the rule on.
//
// `std::tolower` would also have been the wrong tool anyway: it takes an int
// and is undefined for a negative char, which is exactly what a UTF-8 byte in a
// mangled environment variable arrives as.
char LowerAscii(char c)
{
    return (c >= 'A' && c <= 'Z') ? char(c + ('a' - 'A')) : c;
}

bool IsHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// Trimmed of surrounding whitespace and lowered. A YAML scalar picks up a
// trailing space for free and nobody ever sees it.
std::string Normalized(std::string const& raw)
{
    size_t begin = 0;
    size_t end = raw.size();
    auto isSpace = [](char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    };
    while (begin < end && isSpace(raw[begin]))
        ++begin;
    while (end > begin && isSpace(raw[end - 1]))
        --end;

    std::string out;
    out.reserve(end - begin);
    for (size_t i = begin; i < end; ++i)
        out.push_back(LowerAscii(raw[i]));
    return out;
}

// The value of `name` in `env`, or an empty string when the deployment did not
// set it. A lookup rather than `env.at`, because "not set" is the ordinary case
// here and not an error.
std::string Declared(std::map<std::string, std::string> const& env, char const* name)
{
    auto const found = env.find(name);
    return found == env.end() ? std::string() : found->second;
}

// Add a declared row, or add nothing. See BuildReport's header comment for why
// an undeclared fact is a gap rather than an empty string.
void AppendDeclared(std::vector<BuildFact>& facts, std::string const& name,
                    std::string const& value)
{
    std::string const trimmed = Normalized(value);
    if (trimmed.empty())
        return;
    // The VALUE is recorded as it was given, not as it was normalized: the
    // normalization exists to decide whether there is anything here at all, and
    // a reader deserves to see what the deployment actually wrote. The one
    // exception is the surrounding whitespace, which is invisible and would
    // only ever make two identical SHAs look different on the page.
    std::string kept = value;
    size_t const begin = kept.find_first_not_of(" \t\r\n\v\f");
    size_t const end = kept.find_last_not_of(" \t\r\n\v\f");
    kept = (begin == std::string::npos) ? std::string() : kept.substr(begin, end - begin + 1);
    facts.push_back(BuildFact{name, kept, SOURCE_DECLARED});
}

}  // namespace

bool TerrainRecoveryMayInspect(bool alive, bool teleporting, bool inFlight,
                               bool flying, bool falling, bool inWater,
                               bool onTransport, bool onVehicle)
{
    return alive && !teleporting && !inFlight && !flying && !falling &&
           !inWater && !onTransport && !onVehicle;
}

bool BelowTerrainNeedsRecovery(float currentZ, float surfaceAboveZ,
                               bool surfaceValid, bool hasLocalNavmesh,
                               float minimumGap)
{
    if (!surfaceValid || hasLocalNavmesh || minimumGap <= 0.f)
        return false;
    return surfaceAboveZ - currentZ >= minimumGap;
}

bool LargeSurfaceMismatchNeedsRecovery(float currentZ, float surfaceAboveZ,
                                       bool surfaceValid, bool hasLocalNavmesh,
                                       float overrideGap)
{
    if (!surfaceValid || overrideGap <= 0.f)
        return false;
    if (hasLocalNavmesh && surfaceAboveZ - currentZ < overrideGap)
        return false;
    return surfaceAboveZ - currentZ >= overrideGap;
}

namespace
{

// Squared comparison so this file keeps needing nothing but its own header:
// <cmath> for a square root would end the "includes its own header and nothing
// else" property that the header exists to protect, the same reason
// StepMayBridgeGap folds its sign by hand.
bool WithinRadius(float ax, float ay, float bx, float by, float radius)
{
    float const dx = bx - ax;
    float const dy = by - ay;
    return dx * dx + dy * dy <= radius * radius;
}

}  // namespace

TerrainRecoveryVerdict TerrainRecoveryStep(TerrainRecoveryState& state,
                                           TerrainReading const& reading,
                                           TerrainRecoveryLimits const& limits,
                                           time_t now)
{
    // THE EPISODE IS ABANDONED FIRST, BEFORE ANYTHING IS DECIDED, and on every
    // poll rather than only on a clean one. An episode that can only end when
    // the condition goes false cannot end at all where the condition never
    // does, and a cave is exactly such a place: on 2026-09-05 a rung survived
    // 37 minutes and two maps that way and spent itself on an unrelated
    // incident. Three separate things end it, and each catches a case the
    // others do not.
    if (limits.forgetSeconds <= 0)
    {
        // No memory was asked for. Every poll is a first occurrence.
        state = TerrainRecoveryState{};
    }
    else if (state.lastHeld && now - state.lastHeld >= limits.forgetSeconds)
    {
        // The character has been fine for long enough that the next thing to
        // go wrong is a new thing.
        state = TerrainRecoveryState{};
    }
    else if (state.anchored &&
             (state.mapId != reading.mapId ||
              (limits.episodeRadius > 0.f &&
               !WithinRadius(state.x, state.y, reading.x, reading.y,
                             limits.episodeRadius))))
    {
        // Somewhere else entirely. Whatever is wrong here, the ladder climbed
        // over there has nothing to say about it.
        state = TerrainRecoveryState{};
    }

    // The condition is exactly what the adapter asked before: the two
    // predicates above, unchanged, in the same order. Only what happens next
    // is new.
    bool const holds =
        BelowTerrainNeedsRecovery(reading.z, reading.surfaceAboveZ,
                                  reading.surfaceValid, reading.hasLocalNavmesh,
                                  limits.minimumGap) ||
        LargeSurfaceMismatchNeedsRecovery(reading.z, reading.surfaceAboveZ,
                                          reading.surfaceValid,
                                          reading.hasLocalNavmesh,
                                          limits.overrideGap);
    if (!holds)
        return TerrainRecoveryVerdict{};

    // The condition is live, so the forget window starts again from here and
    // the episode learns where it is. Both happen even on a poll that goes on
    // to issue nothing, because both are statements about the WORLD rather
    // than about what this module did.
    state.lastHeld = now;
    if (!state.anchored)
    {
        state.anchored = true;
        state.mapId = reading.mapId;
        state.x = reading.x;
        state.y = reading.y;
    }

    if (reading.hasLocalNavmesh)
    {
        // DETOUR FOUND WALKABLE GROUND AT THIS CHARACTER'S OWN FEET, so it is
        // standing on walkable ground and the gap above it is a roof. Nothing
        // gets moved here. The one warning is still worth making, because a
        // large gap over a live polygon is either architecture (and this rule
        // should stop asking about that place) or a misleading lower plane
        // (and a person needs to go and look). Silence would be the answer to
        // neither.
        if (state.saidOnGround)
            return TerrainRecoveryVerdict{};
        state.saidOnGround = true;
        state.lastAttempt = now;
        return TerrainRecoveryVerdict{TerrainRemedy::GiveUp, 0.f};
    }

    // NO POLYGON. The ladder, and it is short on purpose: the failure this
    // replaces was an unbounded series, so the bound is the fix and not a
    // tuning knob. `surfaceValid` is already true here - neither predicate
    // above returns true without it - so the lift height is a real reading
    // and never a sentinel.
    switch (state.attempts)
    {
        case 0:
            state.attempts = 1;
            state.lastAttempt = now;
            return TerrainRecoveryVerdict{
                TerrainRemedy::LiftToSurface,
                reading.surfaceAboveZ + limits.liftClearance};
        case 1:
            state.attempts = 2;
            state.lastAttempt = now;
            return TerrainRecoveryVerdict{TerrainRemedy::SendToBind, 0.f};
        case 2:
            state.attempts = 3;
            state.lastAttempt = now;
            return TerrainRecoveryVerdict{TerrainRemedy::GiveUp, 0.f};
        default:
            // Said and done. Staying quiet is the point of this rung.
            return TerrainRecoveryVerdict{};
    }
}

bool StepMayBridgeGap(float span, float verticalGap, float stepYards,
                      float maxGap)
{
    if (span > stepYards)
        return true;
    float const gap = verticalGap < 0.f ? -verticalGap : verticalGap;
    return gap <= maxGap;
}

bool TravelEndpointWithinTolerance(float routedEndZ, float requestedZ,
                                   float toleranceYards)
{
    // A negative tolerance is refused rather than folded to its magnitude:
    // see the header for why reading it charitably would loosen the rule.
    if (toleranceYards < 0.f)
        return false;
    float const gap = routedEndZ - requestedZ;
    return (gap < 0.f ? -gap : gap) <= toleranceYards;
}

bool RoutedPathGoesWhereAsked(bool noRouteAtAll, bool endIsOffTheMesh,
                              bool tooFewPoints, float actualEndZ,
                              float requestedZ, float toleranceYards)
{
    // Completeness is deliberately not among these: see the header for why the
    // core's own actual end position answers the question the flag does not.
    if (noRouteAtAll || endIsOffTheMesh || tooFewPoints)
        return false;
    return TravelEndpointWithinTolerance(actualEndZ, requestedZ, toleranceYards);
}

std::string RealmKind(std::string const& declared)
{
    std::string const value = Normalized(declared);
    if (value == REALM_PRODUCTION)
        return REALM_PRODUCTION;
    if (value == REALM_NON_PRODUCTION)
        return REALM_NON_PRODUCTION;
    return REALM_UNKNOWN;
}

std::string CoreRevision(std::string const& coreVersion)
{
    // The marker rather than a fixed offset, because the text in front of it is
    // AC_COMPANYNAME_STR and a fork is free to change it. Everything this needs
    // is between " rev. " and the first character that cannot be part of a
    // commit.
    static char const MARKER[] = " rev. ";
    size_t const at = coreVersion.find(MARKER);
    if (at == std::string::npos)
        return std::string();

    size_t i = at + (sizeof(MARKER) - 1);
    std::string revision;
    while (i < coreVersion.size() && IsHexDigit(coreVersion[i]))
    {
        revision.push_back(LowerAscii(coreVersion[i]));
        ++i;
    }
    // A revision has to be long enough to mean something. Anything shorter than
    // this is not an abbreviated commit, it is a coincidence - "AzerothCore
    // rev. de" would otherwise compare equal to every SHA starting `de`, and a
    // false match here reads as "your pins are correct".
    if (revision.size() < 7)
        return std::string();
    return revision;
}

std::string PinsVerdict(std::string const& coreVersion,
                        std::string const& declaredCoreSha)
{
    std::string const running = CoreRevision(coreVersion);
    std::string const declared = Normalized(declaredCoreSha);
    if (running.empty() || declared.empty())
        return PINS_UNKNOWN;

    // A declaration that is not a commit cannot be compared with one. Saying so
    // is right; guessing STALE would raise an alarm about the pins when the
    // actual fault is that somebody put a branch name in the field.
    for (char const c : declared)
        if (!IsHexDigit(c))
            return PINS_UNKNOWN;
    if (declared.size() < 7)
        return PINS_UNKNOWN;

    // The core abbreviates, the pins file does not, so the shorter of the two
    // has to be a prefix of the longer. Both directions are allowed because
    // which one is shorter is not this function's business.
    std::string const& shorter = declared.size() < running.size() ? declared : running;
    std::string const& longer = declared.size() < running.size() ? running : declared;
    return longer.compare(0, shorter.size(), shorter) == 0 ? PINS_MATCH : PINS_STALE;
}

std::vector<BuildFact> BuildReport(std::string const& coreVersion,
                                   std::map<std::string, std::string> const& env)
{
    std::vector<BuildFact> facts;

    // The two the binary knows about itself, first, because they are the two
    // that cannot be wrong.
    facts.push_back(BuildFact{"module", VERSION, SOURCE_COMPILED});
    // The core's whole sentence, not just the commit: the build date and the
    // build type in it are what tell a reader whether two realms on the same
    // commit are actually running the same binary.
    facts.push_back(BuildFact{"core", coreVersion, SOURCE_COMPILED});

    // WHO THIS REALM IS. `realm` is a name for a reader; `realm_kind` is the
    // one the page changes colour on, and it is always present.
    AppendDeclared(facts, "realm", Declared(env, ENV_REALM));
    facts.push_back(BuildFact{"realm_kind",
                              RealmKind(Declared(env, ENV_REALM_KIND)),
                              SOURCE_DERIVED});

    // WHAT IT WAS BUILT AGAINST, as declared. The core pin is kept even though
    // the running core is already recorded above, because the two disagreeing
    // is the entire point of the verdict below and a reader that can see only
    // the verdict cannot check the work.
    AppendDeclared(facts, "core_pin", Declared(env, ENV_PIN_CORE));
    AppendDeclared(facts, "mod-playerbots", Declared(env, ENV_PIN_PLAYERBOTS));
    AppendDeclared(facts, "mod-ollama-chat", Declared(env, ENV_PIN_OLLAMA_CHAT));
    AppendDeclared(facts, "mod-dungeon-clear", Declared(env, ENV_PIN_DUNGEON_CLEAR));
    AppendDeclared(facts, "mod-ah-bot-plus", Declared(env, ENV_PIN_AH_BOT));

    facts.push_back(BuildFact{"pins",
                              PinsVerdict(coreVersion, Declared(env, ENV_PIN_CORE)),
                              SOURCE_DERIVED});
    return facts;
}

bool DungeonRunBarrierMet(std::vector<DungeonRunMemberState> const& members,
                          float radiusYards)
{
    if (members.empty())
        return false;

    for (DungeonRunMemberState const& member : members)
    {
        if (member.inside)
        {
            if (!member.seen || !member.alive || member.inCombat)
                return false;
            continue;
        }
        if (!member.seen)
            return false;
        if (!member.alive)
            return false;
        if (member.inCombat)
            return false;
        if (member.distanceFromStage < 0.f || member.distanceFromStage > radiusYards)
            return false;
    }
    return true;
}

std::string DungeonRunBarrierBlockers(std::vector<DungeonRunMemberState> const& members,
                                      float radiusYards)
{
    std::string blockers;
    for (DungeonRunMemberState const& member : members)
    {
        std::string why;
        if (!member.seen)
            why = "not seen";
        else if (!member.alive)
            why = "dead";
        else if (member.inCombat)
            why = "in combat";
        // BEFORE "wrong map", because being inside IS a wrong map and is the
        // one wrong map that means something specific: the member is ahead of
        // the party rather than lost behind it. See DungeonRunMemberState.
        else if (member.inside)
            why = "already inside";
        else if (member.distanceFromStage < 0.f)
            why = "wrong map";
        else if (member.distanceFromStage > radiusYards)
            why = std::to_string(static_cast<int>(member.distanceFromStage)) + "y away";
        else
            continue;

        if (!blockers.empty())
            blockers += ", ";
        blockers += member.name + " (" + why + ")";
    }
    return blockers;
}

DungeonApproach DungeonPortalApproach(std::uint32_t leaderMapId,
                                      std::uint32_t portalOutsideMapId)
{
    // EQUAL, NOT "SAME CONTINENT" OR "REACHABLE". The travel layer's rule is
    // literally that the aim's map is the character's map, so this is literally
    // that comparison. Anything softer here would be a promise the resolver
    // underneath does not keep.
    return leaderMapId == portalOutsideMapId ? DungeonApproach::Walkable
                                             : DungeonApproach::OffOutsideMap;
}

namespace
{

// HAND-ROLLED FOR THE REASON THE THREE ASCII HELPERS AT THE TOP OF THIS FILE
// ARE. `<cmath>` would give both of these, and taking it would be the second
// include in a translation unit whose whole stated property is that it has
// none. The rule is worth more than the two functions: it is what makes "a core
// type cannot get in here" a build failure rather than a promise, and an
// exception granted for a square root is an exception granted.
float Magnitude(float value)
{
    return value < 0.f ? -value : value;
}

// Newton-Raphson on f(g) = g*g - value, in double so the iteration has room the
// float inputs do not. It converges quadratically from any positive start, and
// the loop is bounded by a COUNT rather than by a tolerance: the last step of a
// converged Newton iteration can oscillate between two adjacent doubles forever,
// and a fixed bound cannot spin on one. Sixty-four is far past what any distance
// on a 34,000-yard map needs - the spans this is asked about are single-digit to
// low-double-digit yards, which settle in about ten.
//
// Zero, a negative and a NaN all return zero, and the caller reads that as "no
// distance between these two points" - which for the one caller here is exactly
// the NoApproachAxis refusal it already has to make.
double SquareRoot(double value)
{
    if (!(value > 0.0))
        return 0.0;

    double guess = value > 1.0 ? value : 1.0;
    for (int i = 0; i < 64; ++i)
    {
        double const next = 0.5 * (guess + value / guess);
        if (next == guess)
            break;
        guess = next;
    }
    return guess;
}

// Inside the world grid, and a number at all. Written as a positive test rather
// than as `!(out of range)` on purpose: every comparison against a NaN is false,
// so a NaN fails this and is refused, where the negated form would have let it
// through.
bool WithinTheWorld(float value)
{
    return value > -MAP_EDGE_YARDS && value < MAP_EDGE_YARDS;
}

}  // namespace

StagingPointVerdict StagingPointCheck(float x, float y, float z)
{
    // ORDER MATTERS, AND THE ORIGIN COMES FIRST. (0, 0, 0) is inside the world
    // grid, so it passes every bounds test there is; it has to be named as its
    // own answer or it is silently the most plausible-looking wrong point this
    // module can produce. It is also the one that was actually measured.
    if (x == 0.f && y == 0.f)
        return StagingPointVerdict::Unresolved;

    if (!WithinTheWorld(x) || !WithinTheWorld(y) || !WithinTheWorld(z))
        return StagingPointVerdict::OffTheMap;

    return StagingPointVerdict::Usable;
}

bool StagingPointUsable(float x, float y, float z)
{
    return StagingPointCheck(x, y, z) == StagingPointVerdict::Usable;
}

std::string StagingPointRefusal(StagingPointVerdict verdict)
{
    switch (verdict)
    {
        case StagingPointVerdict::Usable:
            return "the staging point is usable";
        case StagingPointVerdict::Unresolved:
            return "the staging point is the origin of the map, which is what three "
                   "floats hold when nothing has resolved them - so this run never "
                   "worked out where to wait";
        case StagingPointVerdict::OffTheMap:
            return "the staging point is outside the world grid, so it is an "
                   "arithmetic accident rather than a place";
        case StagingPointVerdict::NoApproachAxis:
            return "the way back out lands on the door itself, so it names no "
                   "approach axis to stand off along";
    }
    // Unreachable while the enum and this switch agree, and a plain sentence
    // rather than an assertion because the caller's job with any of these is to
    // print it and refuse.
    return "the staging point cannot be used, for a reason this module has no "
           "words for yet";
}

StagingPoint DungeonStagingPoint(float doorX, float doorY,
                                 float backX, float backY, float backZ,
                                 float standoffYards)
{
    StagingPoint point;

    double const dx = double(backX) - double(doorX);
    double const dy = double(backY) - double(doorY);
    double const span = SquareRoot(dx * dx + dy * dy);

    // ONE YARD, and the number is not the interesting part - the refusal is. A
    // landing point on top of the door names no direction at all, and
    // normalising it would be a divide by something near zero dressed up as a
    // bearing. Inventing an axis instead is how a party got walked into rock
    // the first time this was written.
    if (!(span > 1.0))
    {
        point.verdict = StagingPointVerdict::NoApproachAxis;
        return point;
    }

    double const scale = double(standoffYards) / span;
    float const x = float(double(doorX) + dx * scale);
    float const y = float(double(doorY) + dy * scale);

    // THE ARITHMETIC IS CHECKED BY THE SAME VERDICT THE CALLER WILL CHECK, and
    // that is the point of it being one function. A derivation that produced an
    // unusable point used to be able to return it as a success; now the only
    // way to leave here with three floats set is to have passed the test the
    // consumer applies. See StagingPointVerdict for the run this rule is named
    // after.
    StagingPointVerdict const verdict = StagingPointCheck(x, y, backZ);
    if (verdict != StagingPointVerdict::Usable)
    {
        point.verdict = verdict;
        return point;
    }

    point.verdict = StagingPointVerdict::Usable;
    point.x = x;
    point.y = y;
    point.z = backZ;
    return point;
}

bool StagingGroundBelievable(float ground, float doorZ, float toleranceYards)
{
    return Magnitude(ground - doorZ) <= toleranceYards;
}

bool DungeonRunEntryReady(std::vector<DungeonRunEntryState> const& members,
                          float doorstepYards)
{
    if (members.empty())
        return false;

    for (DungeonRunEntryState const& member : members)
    {
        if (member.through)
            continue;
        if (!member.seen)
            return false;
        if (!member.alive)
            return false;
        if (member.inCombat)
            return false;
        if (member.distanceFromDoor < 0.f || member.distanceFromDoor > doorstepYards)
            return false;
    }
    return true;
}

bool DungeonRunAllThrough(std::vector<DungeonRunEntryState> const& members)
{
    if (members.empty())
        return false;

    for (DungeonRunEntryState const& member : members)
        if (!member.through)
            return false;
    return true;
}

std::string DungeonRunEntryBlockers(std::vector<DungeonRunEntryState> const& members,
                                    float doorstepYards)
{
    std::string blockers;
    for (DungeonRunEntryState const& member : members)
    {
        if (member.through)
            continue;

        std::string why;
        if (!member.seen)
            why = "not seen";
        else if (!member.alive)
            why = "dead";
        else if (member.inCombat)
            why = "in combat";
        else if (member.distanceFromDoor < 0.f)
            why = "wrong map";
        else if (member.distanceFromDoor > doorstepYards)
            why = std::to_string(static_cast<int>(member.distanceFromDoor)) + "y from the door";
        else
            why = "at the door, not through";

        if (!blockers.empty())
            blockers += ", ";
        blockers += member.name + " (" + why + ")";
    }
    return blockers;
}

bool RatchetProgressed(float reading, float best, RatchetLimits const& limits,
                       bool seen)
{
    switch (limits.reading)
    {
        case RatchetReading::DistanceToTarget:
            // `seen` is separate from the mark because zero is a real reading:
            // WorldObject::GetDistance2d clamps arrival-range distances to
            // zero, so a traveller standing on its target must not restart the
            // clock forever by looking "unmeasured" on every poll.
            return !seen || reading < best - limits.margin;
        case RatchetReading::CountAchieved:
        case RatchetReading::DistanceFromLastMark:
            // The same comparison for both, which is not a coincidence worth
            // tidying away: they differ in what the mark BECOMES on progress,
            // below, not in what beats it. A mark the caller moves is always
            // measured from zero.
            return reading > best + limits.margin;
    }
    return false;
}

RatchetVerdict Ratchet(RatchetState& state, float reading, time_t now,
                       RatchetLimits const& limits)
{
    RatchetVerdict verdict;
    verdict.progressed = RatchetProgressed(reading, state.best, limits, state.seen);
    state.seen = true;

    if (verdict.progressed)
    {
        state.best =
            limits.reading == RatchetReading::DistanceFromLastMark ? 0.f : reading;
        state.since = now;
        return verdict;
    }

    // A patience of zero means the caller counts, and a `since` of zero means
    // no clock has been started yet. Neither can stall, and neither is a
    // degenerate case to be papered over: they are two sites saying, in the
    // only place it can be said once, that they do not want this half.
    verdict.stalled = limits.patienceSeconds != 0 && state.since != 0 &&
                      now - state.since > limits.patienceSeconds;
    return verdict;
}

DungeonClearStallAction DungeonClearStallDecision(bool bossProgress,
                                                  bool partyBusy,
                                                  bool movementProgress,
                                                  bool stalled,
                                                  unsigned skips,
                                                  unsigned maximumSkips)
{
    // A run is only stalled when every legitimate source of progress is quiet.
    // In particular, being inside a dungeon or merely waiting between pulls is
    // not enough to extract it.
    if (bossProgress || partyBusy || movementProgress || !stalled)
        return DungeonClearStallAction::Nothing;

    // A zero bound is useful to callers that want extraction immediately, and
    // makes the policy explicit rather than relying on an underflow or a magic
    // special case at the call site.
    return skips < maximumSkips ? DungeonClearStallAction::Skip
                                : DungeonClearStallAction::Extract;
}

bool DungeonRunEnteredTheInstance(std::string const& outcome)
{
    // THE CLOSED PAIR, NAMED RATHER THAN DERIVED. These are the only two
    // outcomes this module writes at a point in the state machine that comes
    // before anybody has crossed the door: the instance would not reset, and
    // the party would not assemble. Everything else it writes - 'left',
    // 'stalled', 'wipe', 'emptied' - is written about a party that was on the
    // instance map, and so is an empty outcome, which is what the
    // cold-heartbeat close leaves behind on a row that only exists because
    // somebody was seen in there.
    return outcome != "reset_failed" && outcome != "staging_failed";
}

unsigned DungeonRunTrailingFailures(std::vector<std::string> const& outcomesNewestFirst)
{
    unsigned failures = 0;
    for (std::string const& outcome : outcomesNewestFirst)
    {
        if (DungeonRunEnteredTheInstance(outcome))
            break;
        ++failures;
    }
    return failures;
}

DungeonCampaignProgress DungeonCampaignAfterRun(std::string const& outcome,
                                                uint32_t attemptedRunNumber,
                                                uint32_t runsWanted,
                                                bool capKnown)
{
    DungeonCampaignProgress progress;
    progress.counted = DungeonRunEnteredTheInstance(outcome);

    // THE SLOT IS ONLY FILLED BY AN ATTEMPT THAT ENTERED. `attemptedRunNumber`
    // is which slot was being aimed at, not which slot is now full, and those
    // are the same number only when the attempt got inside. Guarded against 0
    // because an adopted run that was never stamped can reach here with no
    // number of its own, and an unsigned 0 - 1 is the whole campaign.
    if (progress.counted)
        progress.runsDone = attemptedRunNumber;
    else
        progress.runsDone = attemptedRunNumber ? attemptedRunNumber - 1 : 0;

    progress.campaignOver = capKnown && progress.runsDone >= runsWanted;
    progress.nextRunNumber = progress.campaignOver ? 0 : progress.runsDone + 1;
    return progress;
}

StagingNudge StagingWatchdog(StagingStallState& state, float distanceFromStage,
                             bool measurable, time_t now,
                             RatchetLimits const& limits)
{
    if (!measurable)
    {
        // Held, not read. `best` is deliberately left alone: a member that
        // fought its way forward and then came back out of combat nearer than
        // it has ever been should count that as progress, and a member that was
        // pushed backwards should not have its mark spoiled by the push.
        state.progress.since = now;
        return StagingNudge::Nothing;
    }

    RatchetVerdict const verdict = Ratchet(state.progress, distanceFromStage, now, limits);
    if (verdict.progressed)
    {
        // IT IS COMING. The clock has already been restarted by the ratchet;
        // what is undone here is the ladder, so a member that closes the gap
        // after two nudges is watched from the bottom again rather than being
        // one bad patch away from being given up on.
        state.escalated = 0;
        state.gaveUp = false;
        return StagingNudge::Nothing;
    }
    if (!verdict.stalled)
        return StagingNudge::Nothing;

    if (state.escalated >= STAGING_NUDGE_STEPS)
    {
        if (state.gaveUp)
            return StagingNudge::Nothing;
        state.gaveUp = true;
        return StagingNudge::GiveUp;
    }

    // THE CLOCK RESTARTS ON EVERY RUNG, so the rung just climbed is given a
    // whole patience window to work in before the next one is tried. Without
    // this the three of them and the give-up would all fire on consecutive
    // polls, which is not an escalation - it is one reaction spelled four ways.
    state.progress.since = now;
    unsigned const rung = state.escalated++;
    if (rung == 0)
        return StagingNudge::Restrategy;
    if (rung == 1)
        return StagingNudge::Reaim;
    return StagingNudge::ClearMovement;
}

namespace
{

bool Wants(std::vector<unsigned> const& wanted, unsigned skill)
{
    for (unsigned const id : wanted)
        if (id == skill)
            return true;
    return false;
}

bool Holds(std::vector<ProfessionHolding> const& held, unsigned skill)
{
    for (ProfessionHolding const& holding : held)
        if (holding.skill == skill)
            return true;
    return false;
}

// The three primaries that FEED other people's crafts rather than consuming
// their own supply. Spelled out here and nowhere else in this module, because
// nothing in the core answers it: SkillLineEntry has a category, and every one
// of these shares it with the eight crafting primaries, so there is no lookup
// to defer to. Three numbers with a reason beside them is the honest form of a
// fact the data does not carry.
//
// USED FOR ORDER AND NOTHING ELSE. Which professions a character ends up with
// is the roster's decision and this has no vote in it; this only decides which
// of two skills the roster ALREADY chose gets taken first, which is why being
// wrong here would cost a delay and not a profession.
bool Feeds(unsigned skill)
{
    return skill == 182     // herbalism
        || skill == 186     // mining
        || skill == 393;    // skinning
}

}  // namespace

ProfessionStep NextProfessionStep(std::vector<unsigned> const& wanted,
                                  std::vector<ProfessionHolding> const& held,
                                  unsigned maxPrimary)
{
    ProfessionStep step;

    if (wanted.empty())
        return step;                                    // rule 1: no opinion

    // Rules 2 and 4 in one pass. `take` is the skill a free slot would be
    // filled with, and a gatherer displaces a crafter for it once - the second
    // gatherer does not displace the first, so a `wanted` in a fixed order
    // always produces the same answer.
    unsigned missing = 0;
    unsigned take = 0;
    for (unsigned const id : wanted)
    {
        if (Holds(held, id))
            continue;
        ++missing;
        if (!take || (Feeds(id) && !Feeds(take)))
            take = id;
    }

    // RULE 2, AND THE WHOLE OF THE IDEMPOTENCE. Nothing the roster asked for is
    // absent, so there is nothing to make room for, so nothing can be
    // destroyed. A character that reaches its assigned pair leaves through here
    // on every poll for the rest of its life.
    if (!missing)
        return step;

    // RULE 3. Room before ruin.
    if (held.size() < static_cast<std::vector<ProfessionHolding>::size_type>(maxPrimary))
    {
        step.kind = ProfessionStepKind::Take;
        step.skill = take;
        return step;
    }

    // RULE 5. Cheapest first, and only among the ones the roster did not ask
    // for. The id is the tie-break purely so that two skills of equal value
    // cannot make two polls disagree about which one dies.
    for (ProfessionHolding const& holding : held)
    {
        if (Wants(wanted, holding.skill))
            continue;
        if (step.kind != ProfessionStepKind::Nothing &&
            (holding.value > step.cost ||
             (holding.value == step.cost && holding.skill > step.skill)))
            continue;

        step.kind = ProfessionStepKind::GiveUp;
        step.skill = holding.skill;
        step.cost = holding.value;
    }

    // Falls out as Nothing when every held skill is one the roster also wants:
    // the end state asks for more primaries than a character may hold, and the
    // answer to that is to do nothing loudly rather than to pick a victim.
    return step;
}

bool GiveHeldOff(GiveRefusalBook& book, std::string const& key, time_t now,
                 time_t backoffSeconds, time_t forgetSeconds, std::string& reason)
{
    bool held = false;

    for (auto it = book.begin(); it != book.end();)
    {
        // Cold entries go on the way past, whether or not they are the one
        // being asked about. This is the only walk of the book there is, so it
        // is the only place the sweep can happen.
        if (it->second.since == 0 || now - it->second.since >= forgetSeconds)
        {
            it = book.erase(it);
            continue;
        }

        if (it->first == key && now - it->second.since < backoffSeconds)
        {
            reason = it->second.reason;
            held = true;
        }
        ++it;
    }

    return held;
}

bool NoteGiveRefusal(GiveRefusalBook& book, std::string const& key,
                     std::string const& reason, time_t now)
{
    GiveRefusal& memory = book[key];
    // NEW means "not the same wall as last time": either nothing was
    // remembered here at all, or the give is being refused for a different
    // reason than it was, which is a change worth a line of log even though
    // the outcome is the same refusal.
    bool const worthSaying = memory.since == 0 || memory.reason != reason;
    memory.reason = reason;
    memory.since = now;
    return worthSaying;
}

// ------------------------------------------------------------- gear (#145) --
//
// The argument for every number below is in the header, next to the
// declarations. What is here is the arithmetic.

namespace
{

// The core's ItemModType ids (AzerothCore ItemTemplate.h:25-70), named locally
// so this file can weight them without including that header. Only the ones
// that appear on gear a party of this level will ever see are listed; anything
// else falls to the default weight, which is zero.
constexpr int MOD_MANA = 0;
constexpr int MOD_HEALTH = 1;
constexpr int MOD_AGILITY = 3;
constexpr int MOD_STRENGTH = 4;
constexpr int MOD_INTELLECT = 5;
constexpr int MOD_SPIRIT = 6;
constexpr int MOD_STAMINA = 7;
constexpr int MOD_DEFENSE_RATING = 12;
constexpr int MOD_DODGE_RATING = 13;
constexpr int MOD_PARRY_RATING = 14;
constexpr int MOD_BLOCK_RATING = 15;
constexpr int MOD_HIT_MELEE_RATING = 16;
constexpr int MOD_HIT_RANGED_RATING = 17;
constexpr int MOD_HIT_SPELL_RATING = 18;
constexpr int MOD_CRIT_MELEE_RATING = 19;
constexpr int MOD_CRIT_RANGED_RATING = 20;
constexpr int MOD_CRIT_SPELL_RATING = 21;
constexpr int MOD_HASTE_MELEE_RATING = 28;
constexpr int MOD_HASTE_RANGED_RATING = 29;
constexpr int MOD_HASTE_SPELL_RATING = 30;
constexpr int MOD_HIT_RATING = 31;
constexpr int MOD_CRIT_RATING = 32;
constexpr int MOD_RESILIENCE_RATING = 35;
constexpr int MOD_HASTE_RATING = 36;
constexpr int MOD_EXPERTISE_RATING = 37;
constexpr int MOD_ATTACK_POWER = 38;
constexpr int MOD_RANGED_ATTACK_POWER = 39;
constexpr int MOD_MANA_REGENERATION = 43;
constexpr int MOD_ARMOR_PENETRATION_RATING = 44;
constexpr int MOD_SPELL_POWER = 45;
constexpr int MOD_SPELL_PENETRATION = 47;
constexpr int MOD_BLOCK_VALUE = 48;

// The core's item classes and armour subclasses, same reason.
constexpr int CLASS_WEAPON = 2;
constexpr int CLASS_ARMOUR = 4;
constexpr int ARMOUR_MISC = 0;  // rings, necks, trinkets - no proficiency
constexpr int ARMOUR_CLOTH = 1;
constexpr int ARMOUR_LEATHER = 2;
constexpr int ARMOUR_MAIL = 3;
constexpr int ARMOUR_PLATE = 4;
constexpr int ARMOUR_SHIELD = 6;

// InventoryType. A cloak is armour with a subclass of cloth that every class
// wears regardless (the core exempts it from the proficiency rule, and so does
// RandomItemMgr.cpp:1081), and a tabard and a shirt are worn for the look.
constexpr int INV_CLOAK = 16;
constexpr int INV_TABARD = 19;
constexpr int INV_BODY = 4;

// Item level's whole remaining influence. See the header for why it is a
// tiebreak and not an answer.
constexpr float ITEM_LEVEL_TIEBREAK = 0.5f;

// A candidate must beat the incumbent by this much before anything moves.
constexpr float UPGRADE_MARGIN_FRACTION = 0.01f;
constexpr float UPGRADE_MARGIN_FLOOR = 0.5f;

// A SHIELD IS THE ONE PIECE OF ARMOUR YOU WEAR INSTEAD OF A WEAPON, and that
// makes the same number on it worth something quite different from the same
// number on a chest.
//
// THE NUMBERS, off this world: the tank's buckler carries 545 armour where his
// boots carry 56 and his legs 168. A shield is an order of magnitude heavier
// than anything else in the wardrobe, because a shield is not really an armour
// slot - it is a decision to hold something other than a weapon. At the melee
// weight, 545 armour would out-score every off-hand weapon and every two-hander
// in the game, and a retribution paladin would spend the rest of his life
// holding a shield. Only a tank is making that trade on purpose.
//
// So only a tank counts a shield's armour at its own rate. Everybody else
// counts it at the caster rate, which is the rate for "something is going to
// hit me eventually" rather than "this is what I am wearing gear for" - which
// still leaves a holy paladin holding a shield over a plain off-hand, and still
// lets a shield lose to a real weapon for anyone swinging one.
float ShieldArmourWeight(GearRole role);

float ArmourWeight(GearRole role)
{
    switch (role)
    {
        // A tank is the reason this file exists: armour is the stat it is
        // wearing gear FOR, so a point of it is worth a point.
        case GearRole::Tank: return 1.0f;
        // Standing in melee, taking incidental hits and the occasional add:
        // real, and a third of what it is worth to the one holding the boss.
        case GearRole::Melee: return 0.30f;
        case GearRole::Ranged: return 0.15f;
        // Something is hitting them eventually, and cloth is cloth either way -
        // enough weight to break a tie between two otherwise equal robes and
        // never enough to choose armour over intellect.
        case GearRole::Healer:
        case GearRole::Caster: return 0.10f;
        case GearRole::Unknown: return 0.20f;
    }
    return 0.20f;
}

float ShieldArmourWeight(GearRole role)
{
    return role == GearRole::Tank ? ArmourWeight(GearRole::Tank)
                                  : ArmourWeight(GearRole::Caster);
}

// Exchange rate, in armour points per point of the stat.
float StatWeight(GearRole role, int type)
{
    if (role == GearRole::Unknown)
        return 1.0f;  // no opinion, and the verdict says so

    switch (role)
    {
        case GearRole::Tank:
            switch (type)
            {
                case MOD_STAMINA: return 2.0f;
                case MOD_HEALTH: return 0.2f;
                case MOD_DEFENSE_RATING: return 3.0f;
                case MOD_DODGE_RATING:
                case MOD_PARRY_RATING: return 2.5f;
                case MOD_BLOCK_RATING: return 1.5f;
                case MOD_BLOCK_VALUE: return 1.0f;
                case MOD_STRENGTH: return 1.5f;
                case MOD_AGILITY: return 1.2f;
                case MOD_RESILIENCE_RATING: return 1.0f;
                case MOD_EXPERTISE_RATING: return 1.5f;
                case MOD_HIT_MELEE_RATING:
                case MOD_HIT_RATING: return 1.0f;
                case MOD_CRIT_MELEE_RATING:
                case MOD_CRIT_RATING: return 0.5f;
                case MOD_HASTE_MELEE_RATING:
                case MOD_HASTE_RATING: return 0.3f;
                case MOD_ATTACK_POWER: return 0.3f;
                default: return 0.f;
            }
        case GearRole::Melee:
            switch (type)
            {
                case MOD_STRENGTH:
                case MOD_AGILITY: return 2.0f;
                case MOD_STAMINA: return 1.0f;
                case MOD_ATTACK_POWER: return 1.0f;
                case MOD_HIT_MELEE_RATING:
                case MOD_HIT_RATING: return 1.5f;
                case MOD_CRIT_MELEE_RATING:
                case MOD_CRIT_RATING: return 1.5f;
                case MOD_EXPERTISE_RATING: return 1.5f;
                case MOD_HASTE_MELEE_RATING:
                case MOD_HASTE_RATING: return 1.2f;
                case MOD_ARMOR_PENETRATION_RATING: return 1.2f;
                case MOD_DEFENSE_RATING:
                case MOD_DODGE_RATING:
                case MOD_PARRY_RATING: return 0.3f;
                default: return 0.f;
            }
        case GearRole::Ranged:
            switch (type)
            {
                case MOD_AGILITY: return 2.5f;
                case MOD_STAMINA: return 1.0f;
                case MOD_ATTACK_POWER:
                case MOD_RANGED_ATTACK_POWER: return 1.0f;
                case MOD_HIT_RANGED_RATING:
                case MOD_HIT_RATING: return 1.5f;
                case MOD_CRIT_RANGED_RATING:
                case MOD_CRIT_RATING: return 1.5f;
                case MOD_HASTE_RANGED_RATING:
                case MOD_HASTE_RATING: return 1.2f;
                case MOD_ARMOR_PENETRATION_RATING: return 1.0f;
                case MOD_INTELLECT: return 0.3f;
                case MOD_STRENGTH: return 0.2f;
                default: return 0.f;
            }
        case GearRole::Healer:
            switch (type)
            {
                case MOD_INTELLECT: return 2.5f;
                case MOD_SPIRIT: return 2.0f;
                case MOD_MANA_REGENERATION: return 2.0f;
                case MOD_SPELL_POWER: return 1.5f;
                case MOD_STAMINA: return 1.0f;
                case MOD_HASTE_SPELL_RATING:
                case MOD_HASTE_RATING: return 1.0f;
                case MOD_CRIT_SPELL_RATING:
                case MOD_CRIT_RATING: return 0.8f;
                case MOD_MANA: return 0.05f;
                default: return 0.f;
            }
        case GearRole::Caster:
            switch (type)
            {
                case MOD_SPELL_POWER: return 2.0f;
                case MOD_INTELLECT: return 2.0f;
                case MOD_HIT_SPELL_RATING: return 2.0f;
                case MOD_CRIT_SPELL_RATING:
                case MOD_CRIT_RATING: return 1.5f;
                case MOD_HASTE_SPELL_RATING:
                case MOD_HASTE_RATING: return 1.5f;
                case MOD_STAMINA: return 1.0f;
                case MOD_SPIRIT: return 0.8f;
                case MOD_SPELL_PENETRATION: return 0.5f;
                case MOD_MANA: return 0.05f;
                default: return 0.f;
            }
        case GearRole::Unknown:
            return 1.0f;
    }
    return 0.f;
}

// Armour points per point of weapon damage per second. A weapon is most of
// what a melee character contributes and almost none of what a healer does, so
// the spread is wide on purpose; a caster's weapon is worth having for the
// stats on it, which are scored separately above.
float DpsWeight(GearRole role)
{
    switch (role)
    {
        case GearRole::Tank: return 4.0f;
        case GearRole::Melee:
        case GearRole::Ranged: return 8.0f;
        case GearRole::Healer:
        case GearRole::Caster: return 1.0f;
        case GearRole::Unknown: return 4.0f;
    }
    return 4.0f;
}

// Does this character hold the proficiency this armour subclass needs? The
// mapping is the core's own (ItemTemplate.h:782-796): cloth, leather, mail,
// plate and shield are the only armour subclasses that map to a skill, and
// everything else - rings, necks, trinkets, and the relics - maps to zero and
// therefore needs nothing. A cloak is filed under cloth and the core exempts
// it, as does upstream (RandomItemMgr.cpp:1081), so it is exempt here.
bool ArmourProficient(GearItem const& item, GearWearer const& who, std::string& why)
{
    if (item.inventoryType == INV_CLOAK || item.inventoryType == INV_TABARD ||
        item.inventoryType == INV_BODY || item.subClass == ARMOUR_MISC)
        return true;

    switch (item.subClass)
    {
        case ARMOUR_CLOTH:
            if (who.cloth)
                return true;
            why = "no cloth proficiency";
            return false;
        case ARMOUR_LEATHER:
            if (who.leather)
                return true;
            why = "no leather proficiency";
            return false;
        case ARMOUR_MAIL:
            if (who.mail)
                return true;
            why = "no mail proficiency";
            return false;
        case ARMOUR_PLATE:
            if (who.plate)
                return true;
            why = "no plate proficiency";
            return false;
        case ARMOUR_SHIELD:
            if (who.shield)
                return true;
            why = "no shield proficiency";
            return false;
        default:
            // A libram, idol, totem or sigil. None of them maps to an armour
            // skill in the core's own table, so none of them needs a
            // proficiency, and the class that may hold one is already settled
            // by the item's class mask before this is reached. They carry no
            // armour, so what is left of the score for them is their stats and
            // the item-level tiebreak, which is thin and is not wrong.
            return true;
    }
}

std::string ArmourClassName(GearItem const& item)
{
    if (item.itemClass != CLASS_ARMOUR)
        return "";
    switch (item.subClass)
    {
        case ARMOUR_CLOTH: return "cloth";
        case ARMOUR_LEATHER: return "leather";
        case ARMOUR_MAIL: return "mail";
        case ARMOUR_PLATE: return "plate";
        case ARMOUR_SHIELD: return "shield";
        default: return "";
    }
}

}  // namespace

GearVerdict GearScore(GearItem const& item, GearWearer const& who)
{
    GearVerdict verdict;

    // THE GATES THE CORE ITSELF APPLIES, in the order it applies them. Each one
    // is final: a `wearable = false` verdict has no score to compare, which is
    // the point - the priest carrying leather boots four item levels above her
    // sandals must never see a number at all, because any number invites a
    // comparison.
    //
    // A REFUSAL IS A JUDGEMENT, so `judged` is true on every one of these. It
    // is the answer being certain rather than the answer being good, and the
    // difference matters to the caller: an unjudged verdict is one this file
    // declines to have an opinion on and hands back for somebody else to
    // decide, whereas "she cannot wear leather" is decided. Exact, too: there
    // is no unread part of a refusal that might yet change the answer.
    verdict.judged = true;
    verdict.confidence = GearConfidence::Exact;

    if (!who.classAllowed)
    {
        verdict.why = "wrong class for this item";
        return verdict;
    }
    if (item.requiredLevel > who.level)
    {
        verdict.why = "requires level " + std::to_string(item.requiredLevel);
        return verdict;
    }
    if (item.itemClass == CLASS_ARMOUR && !ArmourProficient(item, who, verdict.why))
        return verdict;
    if (item.itemClass == CLASS_WEAPON && !who.weaponProficient)
    {
        verdict.why = "no proficiency with this weapon";
        return verdict;
    }

    verdict.wearable = true;

    float const armourWeight = item.subClass == ARMOUR_SHIELD && item.itemClass == CLASS_ARMOUR
                                   ? ShieldArmourWeight(who.role)
                                   : ArmourWeight(who.role);
    float score = armourWeight * static_cast<float>(item.armour);
    for (GearStat const& stat : item.stats)
    {
        if (!stat.value)
            continue;
        score += StatWeight(who.role, stat.type) * static_cast<float>(stat.value);
    }
    score += DpsWeight(who.role) * item.dps;
    score += ITEM_LEVEL_TIEBREAK * static_cast<float>(item.itemLevel);

    // A score can go negative on a piece whose only stats are ones this role
    // does not want. Nothing sensible follows from a negative, and an empty
    // slot is defined as zero, so the floor is zero: the worst an item can be
    // is worth exactly as much as wearing nothing.
    verdict.score = score < 0.f ? 0.f : score;

    // WHAT IT WILL NOT CLAIM TO HAVE JUDGED. Everything below is the score
    // being honest about its own coverage rather than a reason to refuse the
    // item - see GearVerdict::judged.
    verdict.judged = who.role != GearRole::Unknown && !item.hasEffect &&
                     !item.unresolvedRandomProperty &&
                     (item.itemClass == CLASS_ARMOUR || item.itemClass == CLASS_WEAPON);

    // THE SAME FACT, TOLD APART (#221). An unknown role, or a thing that is not
    // worn gear at all, leaves a number that is not a bound in either
    // direction - every stat weighted 1.0 orders items roughly and proves
    // nothing. An unread effect or an unresolved random property is the other
    // case entirely: both can only ADD, so what is left is a FLOOR, and a floor
    // is something a comparison can still use.
    if (who.role == GearRole::Unknown ||
        (item.itemClass != CLASS_ARMOUR && item.itemClass != CLASS_WEAPON))
        verdict.confidence = GearConfidence::Opinion;
    else if (item.hasEffect || item.unresolvedRandomProperty)
        verdict.confidence = GearConfidence::Floor;
    else
        verdict.confidence = GearConfidence::Exact;

    std::string const armourClass = ArmourClassName(item);
    verdict.why = armourClass.empty() ? std::string() : armourClass + ", ";
    if (item.armour)
        verdict.why += std::to_string(item.armour) + " armour, ";
    verdict.why += "item level " + std::to_string(item.itemLevel) + ", scores " +
                   std::to_string(static_cast<int>(verdict.score));
    if (!verdict.judged)
    {
        if (item.itemClass != CLASS_ARMOUR && item.itemClass != CLASS_WEAPON)
            verdict.why += " (not worn, so this does not judge it)";
        else if (who.role == GearRole::Unknown)
            verdict.why += " (no role, so not judged)";
        else if (item.hasEffect)
            verdict.why += " (carries an effect this does not read)";
        else
            verdict.why += " (random property unresolved)";
    }
    return verdict;
}

float GearIncumbent(float mainHandScore, float offHandScore, bool takesBothHands)
{
    if (!takesBothHands)
        return mainHandScore;
    return mainHandScore + offHandScore;
}

bool GearIsUpgrade(GearVerdict const& candidate, float incumbent)
{
    if (!candidate.wearable)
        return false;
    return candidate.score >
           incumbent * (1.f + UPGRADE_MARGIN_FRACTION) + UPGRADE_MARGIN_FLOOR;
}

GearIncumbentScore GearWorn(GearVerdict const& worn)
{
    // NOTHING WORN AND NOTHING WEARABLE ARE THE SAME NUMBER, and it is an exact
    // one. An empty slot is worth zero; a piece the character has lost the
    // proficiency for is worth zero to it as well, and both of those are
    // certain rather than a floor under something unknown.
    if (!worn.wearable)
        return GearIncumbentScore{0.f, GearConfidence::Exact};
    return GearIncumbentScore{worn.score, worn.confidence};
}

GearIncumbentScore GearIncumbentPair(GearIncumbentScore const& mainHand,
                                     GearIncumbentScore const& offHand)
{
    GearIncumbentScore pair;
    pair.score = mainHand.score + offHand.score;

    // THE WEAKER HALF DECIDES WHAT THE PAIR IS WORTH KNOWING. A sum is only
    // exactly known when both terms are; a sum with one floor in it is a floor;
    // and an opinion anywhere makes the whole thing an opinion, because
    // "roughly ordered" plus "exact" is still only roughly ordered.
    if (mainHand.confidence == GearConfidence::Opinion ||
        offHand.confidence == GearConfidence::Opinion)
        pair.confidence = GearConfidence::Opinion;
    else if (mainHand.confidence == GearConfidence::Floor ||
             offHand.confidence == GearConfidence::Floor)
        pair.confidence = GearConfidence::Floor;
    else
        pair.confidence = GearConfidence::Exact;
    return pair;
}

GearComparison GearCompare(GearVerdict const& candidate, GearIncumbentScore const& worn)
{
    // A refusal is final and has no number, exactly as it always was.
    if (!candidate.wearable)
        return GearComparison::NotBetter;

    // An opinion on either side settles nothing. The role is unknown, or the
    // thing is not worn gear, so the number does not bound anything and acting
    // on it is the confident-but-wrong move that put a cloth robe on the tank.
    if (candidate.confidence == GearConfidence::Opinion ||
        worn.confidence == GearConfidence::Opinion)
        return GearComparison::Undecided;

    // What is worn is itself only a floor, so its true worth is somewhere above
    // the number and nothing can be proved to beat it.
    if (worn.confidence == GearConfidence::Floor)
        return GearComparison::Undecided;

    // From here what is worn is exactly known, and the margin is the same one
    // GearIsUpgrade uses - the two must never disagree about where the line is.
    bool const clears = candidate.score >
                        worn.score * (1.f + UPGRADE_MARGIN_FRACTION) + UPGRADE_MARGIN_FLOOR;

    if (candidate.confidence == GearConfidence::Exact)
        return clears ? GearComparison::Better : GearComparison::NotBetter;

    // The candidate's score is a FLOOR (#221). A floor that already clears the
    // margin settles it - the unread part can only widen the gap, never close
    // it - which is the whole of why a rare with an on-equip effect now gets
    // worn instead of carried. A floor that does NOT clear proves nothing
    // either way, so it is said out loud rather than reported as a refusal.
    return clears ? GearComparison::Better : GearComparison::Undecided;
}

GearSwapIntent GearIntend(GearSlotMemory const& memory, unsigned candidateEntry,
                          unsigned wornEntry, bool wanted)
{
    GearSwapIntent intent;
    intent.memory = memory;

    if (!wanted)
        return intent;

    // IS THIS THE SAME SWAP, UNDONE? The drive put `chosen` on over `displaced`
    // and is now looking at `chosen` in the bags with `displaced` worn again.
    // Nothing this module does can produce that: its own swap is one-way, and
    // after it the candidate IS what is worn. So somebody else moved it.
    //
    // AN EMPTY SLOT IS NOT A DISPUTE, and `wornEntry == 0` is what an empty one
    // looks like. There is no rival item to be arm-wrestling over, an empty
    // slot is worth exactly zero so filling it is unambiguously an improvement,
    // and a slot that keeps ending up bare - a piece destroyed, a swap the
    // server refused - wants filling again rather than giving up on. Standing
    // down there would leave a character permanently missing a slot, which is
    // strictly worse than the churn this guard exists to stop.
    bool const reverted = candidateEntry != 0 && wornEntry != 0 &&
                          memory.chosen == candidateEntry && memory.displaced == wornEntry;
    if (!reverted)
    {
        // A different pair is a fresh question and gets a fresh budget.
        intent.swap = true;
        intent.memory.chosen = candidateEntry;
        intent.memory.displaced = wornEntry;
        intent.memory.reversals = 0;
        return intent;
    }

    if (memory.reversals >= GEAR_REVERSALS_ALLOWED)
    {
        // Already given up on this pair, and already said so. Silence from here.
        return intent;
    }

    intent.memory.reversals = memory.reversals + 1;
    if (intent.memory.reversals >= GEAR_REVERSALS_ALLOWED)
    {
        // THE ATTEMPT THAT EXHAUSTS THE BUDGET DOES NOT HAPPEN, and is the one
        // that speaks. Swapping and then giving up would leave the slot in this
        // module's preferred state by luck of ordering and say nothing useful;
        // standing down leaves it where the other writer put it and names both
        // items, which is what a reader needs in order to go and find the other
        // writer.
        intent.standDown = true;
        return intent;
    }

    intent.swap = true;
    return intent;
}

std::string GearNeedWinner(std::vector<GearContender> const& contenders)
{
    std::string winner;
    float bestTotal = 0.f;
    float bestGain = 0.f;

    for (GearContender const& contender : contenders)
    {
        if (contender.gain <= 0.f)
            continue;

        if (winner.empty() || contender.totalWorn < bestTotal ||
            (contender.totalWorn == bestTotal &&
             (contender.gain > bestGain ||
              (contender.gain == bestGain && contender.name < winner))))
        {
            winner = contender.name;
            bestTotal = contender.totalWorn;
            bestGain = contender.gain;
        }
    }
    return winner;
}

// -------------------------------------------------------------- sell (#18) --

namespace
{

// `<key>:<digits>` -> the digits, or false. Strict on purpose: a value with
// anything but digits, or nothing at all, is a malformed command and not a
// zero, because a zero guid would be "no item" and a zero count would be the
// core's "sell all" special case (ItemHandler.cpp:638), which a sender should
// ask for by leaving count out rather than by writing 0.
bool KeyedNumber(std::string const& token, char const* key, uint32_t& value)
{
    std::string::size_type const colon = token.find(':');
    if (colon == std::string::npos || token.substr(0, colon) != key)
        return false;
    std::string const digits = token.substr(colon + 1);
    if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos)
        return false;
    // Ten digits can overflow uint32; refuse rather than wrap, since a wrapped
    // guid would name a different item.
    if (digits.size() > 10)
        return false;
    unsigned long long parsed = 0;
    for (char c : digits)
        parsed = parsed * 10 + static_cast<unsigned>(c - '0');
    if (parsed > 0xFFFFFFFFull)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

}  // namespace

SellSpec ParseSellSpec(std::string const& command)
{
    SellSpec spec;

    std::vector<std::string> tokens;
    std::string::size_type start = 0;
    while (start <= command.size())
    {
        std::string::size_type const space = command.find(' ', start);
        std::string const token =
            command.substr(start, space == std::string::npos ? std::string::npos : space - start);
        if (!token.empty())
            tokens.push_back(token);
        if (space == std::string::npos)
            break;
        start = space + 1;
    }

    if (tokens.empty() || tokens.size() > 2)
        return spec;

    uint32_t guid = 0;
    if (!KeyedNumber(tokens[0], "guid", guid) || guid == 0)
        return spec;

    uint32_t count = 0;
    if (tokens.size() == 2)
    {
        if (!KeyedNumber(tokens[1], "count", count) || count == 0)
            return spec;
    }

    spec.valid = true;
    spec.guid = guid;
    spec.count = count;
    return spec;
}

int ChooseSellVendor(std::vector<SellVendorCandidate> const& candidates)
{
    int best = -1;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        SellVendorCandidate const& candidate = candidates[i];
        if (best < 0)
        {
            best = static_cast<int>(i);
            continue;
        }
        SellVendorCandidate const& incumbent = candidates[static_cast<size_t>(best)];
        // A buyer always beats a refuser, whatever the distances.
        if (incumbent.refusesSales && !candidate.refusesSales)
        {
            best = static_cast<int>(i);
            continue;
        }
        if (!incumbent.refusesSales && candidate.refusesSales)
            continue;
        if (candidate.distance < incumbent.distance)
            best = static_cast<int>(i);
    }
    return best;
}

SellRetry SellRefusalRetry(std::string const& detail)
{
    // The literals mod_overseer.cpp's DoSell returns, grouped by what would
    // have to change for the same row to succeed. A literal that is not here
    // is answered `Later` (see the header for why that is the safe default).
    static char const* const NEVER[] = {
        "malformed sell: want guid:<item_instance.guid>[ count:<n>]",
        "item not carried",
        "item is a quest item",
        "item cannot be sold",
        "count exceeds stack",
    };
    static char const* const ELSEWHERE[] = {
        "vendor not in range",
        "vendor refuses item",
    };

    for (char const* literal : NEVER)
        if (detail == literal)
            return SellRetry::Never;
    for (char const* literal : ELSEWHERE)
        if (detail == literal)
            return SellRetry::Elsewhere;
    return SellRetry::Later;
}

char const* SellRetryWord(SellRetry retry)
{
    switch (retry)
    {
        case SellRetry::Never:
            return "never";
        case SellRetry::Elsewhere:
            return "elsewhere";
        case SellRetry::Later:
            break;
    }
    return "later";
}

// ---------------------------------------------------------- the bank row --

namespace
{

// The words of the line, split on runs of blanks. Tabs count as blanks
// because a row typed by hand into a SQL client can carry them.
std::vector<std::string> Words(std::string const& text)
{
    std::vector<std::string> words;
    std::string word;
    for (char const c : text)
    {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
        {
            if (!word.empty())
                words.push_back(word);
            word.clear();
        }
        else
            word += c;
    }
    if (!word.empty())
        words.push_back(word);
    return words;
}

// `guid:<digits>` -> the number, or 0 for anything else, including a number
// that overflows uint32 (a guid counter is 32 bits in the core), a bare
// `guid:`, and the `entry:` form the other item verbs accept.
uint32_t GuidOf(std::string const& word)
{
    std::string const prefix = "guid:";
    if (word.compare(0, prefix.size(), prefix) != 0)
        return 0;
    std::string const digits = word.substr(prefix.size());
    if (digits.empty() || digits.size() > 10)
        return 0;
    uint64_t value = 0;
    for (char const c : digits)
    {
        if (c < '0' || c > '9')
            return 0;
        value = value * 10 + static_cast<uint64_t>(c - '0');
    }
    if (value > 0xFFFFFFFFull)
        return 0;
    return static_cast<uint32_t>(value);
}

}  // namespace

BankRequest ParseBankRequest(std::string const& command)
{
    BankRequest request;
    std::vector<std::string> const words = Words(command);

    if (words.empty())
    {
        request.error = "malformed bank: want deposit guid:<n>, withdraw guid:<n>, or buy slot";
        return request;
    }

    if (words[0] == "buy")
    {
        if (words.size() == 2 && words[1] == "slot")
        {
            request.verb = BankVerb::BuySlot;
            return request;
        }
        request.error = "malformed bank: buy takes exactly `slot`";
        return request;
    }

    if (words[0] == "deposit" || words[0] == "withdraw")
    {
        if (words.size() != 2)
        {
            request.error = words[0] == "deposit"
                                ? "malformed bank: want deposit guid:<item_instance.guid>"
                                : "malformed bank: want withdraw guid:<item_instance.guid>";
            return request;
        }
        uint32_t const guid = GuidOf(words[1]);
        if (!guid)
        {
            request.error = "malformed bank: item must be guid:<item_instance.guid>, not 0";
            return request;
        }
        request.verb = words[0] == "deposit" ? BankVerb::Deposit : BankVerb::Withdraw;
        request.itemGuid = guid;
        return request;
    }

    request.error = "malformed bank: unknown verb (want deposit, withdraw, or buy slot)";
    return request;
}

uint32_t NearestBanker(std::vector<BankerCandidate> const& candidates)
{
    uint32_t pick = 0;
    float pickDistance = 0.f;
    bool pickInteractable = false;

    for (BankerCandidate const& candidate : candidates)
    {
        if (!candidate.id)
            continue;

        bool better;
        if (!pick)
            better = true;
        else if (candidate.interactable != pickInteractable)
            better = candidate.interactable;
        else if (candidate.distance != pickDistance)
            better = candidate.distance < pickDistance;
        else
            better = candidate.id < pick;

        if (better)
        {
            pick = candidate.id;
            pickDistance = candidate.distance;
            pickInteractable = candidate.interactable;
        }
    }

    // A banker nobody may talk to is not an answer. The caller wants the one
    // to hand to the core, and the core would refuse this one anyway; better
    // it is named "no banker in reach" here than "the core refused" there.
    return pickInteractable ? pick : 0;
}

}  // namespace OverseerDecisions
