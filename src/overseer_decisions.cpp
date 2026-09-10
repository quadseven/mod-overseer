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

bool MeasuredToBeFalling(bool guardMayInspect, bool coreWouldNotCharge,
                         bool baselineRebased)
{
    // A GUARD THAT DID NOT LOOK MEASURED NOTHING, and an unmeasured character
    // is not a falling one. The states that stop the guard looking - dead,
    // teleporting, on a taxi, in water, on a transport, in a vehicle - are
    // every one of them already a stand-down of TerrainRecoveryMayInspect's
    // own, so answering false here cannot open a gate those keep shut. It only
    // stops an absence of evidence being read as evidence, which is the
    // asymmetry #291 chose for the guard and the same one this needs.
    if (!guardMayInspect)
        return false;

    // The core will not charge this character for a fall, so the guard wrote
    // nothing and there is no measurement behind `baselineRebased` to read.
    if (coreWouldNotCharge)
        return false;

    // The guard put the baseline back under the character's feet, which it
    // does for everything that is not descending faster than a body on its
    // feet can. So a rebase is the measurement saying "not falling", and its
    // absence is the measurement saying "falling".
    return !baselineRebased;
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

bool StandingOnTheGround(bool hasLocalNavmesh, bool footingHolds)
{
    return hasLocalNavmesh && footingHolds;
}

bool FloorUnderfoot(float currentZ, float floorBelowZ, bool floorBelowValid,
                    float reach)
{
    if (!floorBelowValid || reach <= 0.f)
        return false;
    // Folded by hand rather than with std::fabs, for the reason WithinRadius
    // below squares its distance: this file includes its own header and
    // nothing else, and that property is half of what the decisions workflow
    // is testing.
    float const separation = currentZ - floorBelowZ;
    float const magnitude = separation < 0.f ? -separation : separation;
    return magnitude <= reach;
}

bool ReadingStandsOnTheGround(TerrainReading const& reading, float footingReach)
{
    return StandingOnTheGround(reading.hasLocalNavmesh, reading.footingHolds) ||
           FloorUnderfoot(reading.z, reading.floorBelowZ,
                          reading.floorBelowValid, footingReach);
}

bool SomeInstrumentFoundGround(TerrainReading const& reading,
                               float footingReach)
{
    // Detour's raw answer, with the footing correction taken off. The
    // correction is a statement about walking and this is a question about
    // ground; see the header for the two characters thirty yards apart that
    // separated them.
    return reading.hasLocalNavmesh ||
           FloorUnderfoot(reading.z, reading.floorBelowZ,
                          reading.floorBelowValid, footingReach);
}

bool NearTheVoidPlane(float currentZ, float catchYards)
{
    if (catchYards <= 0.f)
        return false;
    // No lower bound. A character already past the plane and not yet killed
    // is still worth lifting, and one that has been killed is declined by the
    // drive's own stand-down on a corpse long before this is asked.
    return currentZ <= VOID_PLANE_Z + catchYards;
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

    // AND THE GIVE-UP EXPIRES (#188). Separate from the three above rather
    // than a fourth branch of them, because it answers a different question:
    // those ask whether this is still the same incident, and this asks whether
    // this module is still entitled to refuse to act on it.
    //
    // IT IS MEASURED FROM THE LAST REMEDY AND NOT FROM THE LAST HOLD, which is
    // the whole reason the old window never opened. `lastHeld` is refreshed by
    // every poll the condition is true on, so a character genuinely under the
    // world pushed the forget window ahead of itself forever and the episode
    // could not end: "GIVING UP until it has been clear for 600s" was, for the
    // one population it was ever said about, giving up. `lastAttempt` does not
    // move once the module has stopped acting, so ten minutes of being out of
    // remedies really is ten minutes.
    //
    // ONLY THE LADDER COMES BACK, not the warning. `saidOnGround` is a note
    // about a character nothing is being done to - an arch, a bridge deck, a
    // tower floor - and repeating it every ten minutes would buy a reader
    // nothing and cost them 79 lines a night at one Northshire coordinate
    // alone. `attempts` is the remedy a character under the world needs
    // another go at. The anchor is left alone so this is the same episode
    // getting another rung and not a new one.
    if (limits.forgetSeconds > 0 && state.lastAttempt &&
        now - state.lastAttempt >= limits.forgetSeconds)
    {
        state.attempts = 0;
        state.lastAttempt = 0;
    }

    // OUT OF THE BAND RE-ARMS THE CATCH, and it is cleared here rather than
    // in the branch below because a character the catch WORKED on is fine on
    // the next poll and returns long before that branch is reached. See
    // TerrainRecoveryState::caughtBelow.
    if (!NearTheVoidPlane(reading.z, limits.voidCatchYards))
        state.caughtBelow = false;

    // WHETHER THIS CHARACTER IS ON THE GROUND, decided once and then read
    // three times, because all three readings are the same question. A polygon
    // Detour found inside its search box is only evidence about this
    // character's feet while some direction out of here can be walked; see
    // StandingOnTheGround for the pocket beside the Wailing Caverns ramp where
    // it was not, and where believing it held four characters for 24 minutes.
    //
    // AND A FLOOR UNDER THE FEET IS THE OTHER WAY TO BE ON THE GROUND (#296).
    // Detour saying nothing is not evidence a character is airborne - it is
    // what Detour says inside a city interior and anywhere the mesh is thin -
    // and until this second instrument existed that branch had no guard at all
    // and went straight to the lift. See ReadingStandsOnTheGround.
    bool const onTheGround =
        ReadingStandsOnTheGround(reading, limits.footingReach);

    // The condition is exactly what the adapter asked before: the two
    // predicates above, unchanged, in the same order. Only what happens next
    // is new.
    bool const holds =
        BelowTerrainNeedsRecovery(reading.z, reading.surfaceAboveZ,
                                  reading.surfaceValid, onTheGround,
                                  limits.minimumGap) ||
        LargeSurfaceMismatchNeedsRecovery(reading.z, reading.surfaceAboveZ,
                                          reading.surfaceValid, onTheGround,
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

    if (onTheGround)
    {
        // DETOUR FOUND WALKABLE GROUND AT THIS CHARACTER'S OWN FEET AND THE
        // CHARACTER CAN STILL WALK OFF IT, so it is standing on walkable
        // ground and the gap above it is a roof. Nothing
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

    // THE LAST STRETCH ABOVE THE KILL PLANE, WHERE THE BOUND STOPS APPLYING
    // (#188). Read this AFTER the on-the-ground branch above and not before
    // it: a character with a floor at its own feet at this depth is standing
    // on a legitimate deep interior and must not be moved by anything, and
    // that ordering is what keeps this rung from being a licence to displace.
    //
    // WHAT IT IS FOR. Six void deaths on 2026-09-08/09, all map 1, all at full
    // health, out of combat, `driver=idle`, every one of them at z -506 to
    // -528 with the previous sample already at -265 to -468. There is a window
    // in which the character is demonstrably below the world and still alive,
    // and this module was standing down through all of it: the ordinary
    // stand-down declines a measured descent, and even without it the
    // sixty-yard surface probe finds nothing overhead once a character is that
    // far down, so there was no height to lift to and no rung left to spend.
    // The adapter answers the first two; this answers the third.
    //
    // WHY IT MAY IGNORE THE LADDER. The ladder bounds displacement of
    // characters that are probably fine, and #188 measured 204 of those in six
    // hours. None of them was two hundred yards under the world. Inside this
    // band the alternative to a remedy is not "leave a character where it is",
    // it is a max-health kill past every immunity followed by a graveyard that
    // has already been observed to be on another continent - which is the
    // outcome the no-cross-map rule exists to prevent, arrived at by not
    // acting instead of by acting.
    //
    // AND IT IS STILL A LIFT. Same map, same x, same y. The remedy set is not
    // widened by one yard here; what changes is only whether the module is
    // allowed to use it.
    if (NearTheVoidPlane(reading.z, limits.voidCatchYards))
    {
        if (state.caughtBelow)
            return TerrainRecoveryVerdict{};
        state.caughtBelow = true;
        state.lastAttempt = now;
        return TerrainRecoveryVerdict{
            TerrainRemedy::LiftToSurface,
            reading.surfaceAboveZ + limits.liftClearance};
    }

    // NO POLYGON. The ladder, and it is short on purpose: the failure this
    // replaces was an unbounded series, so the bound is the fix and not a
    // tuning knob. `surfaceValid` is already true here - neither predicate
    // above returns true without it - so the lift height is a real reading
    // and never a sentinel.
    //
    // TWO RUNGS, AND THE SECOND ONE MOVES NOBODY (#188). There used to be a
    // bind-point teleport between them. On 2026-09-05 it took a character from
    // map 1 (1204.1, -708.5) to map 0 (-8902.6, -162.6) in eleven seconds and
    // he STILL read as below the world when he got there, so it relocated the
    // failure rather than resolving it, and it left the roster split across an
    // ocean with a dungeon to run on one side of it. A lift that did not stick
    // means this module cannot fix this character WHERE IT STANDS, and the
    // honest answer to that is to say so loudly. Escalating past a remedy that
    // failed in place, to a remedy that cannot address the condition at all,
    // is not an escalation.
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
            return TerrainRecoveryVerdict{TerrainRemedy::GiveUp, 0.f};
        default:
            // Said and done, FOR THE LENGTH OF THE FORGET WINDOW. Staying
            // quiet is still the point of this rung - a repeated identical
            // line helps nobody - but it is a cooldown now and not a
            // retirement: the re-arm above puts `attempts` back to zero ten
            // minutes after the give-up, and the character gets its lift
            // again. Four of the five characters that reached this rung on
            // 2026-09-09 were dead within the hour, and the rung's own
            // sentence had been promising them a 600 second window that could
            // never open (#188).
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

bool FootingSampleHolds(float fromZ, float toZ, float maxDrop, float maxRise)
{
    // The smaller bound, applied to the magnitude. A stride is walkable
    // exactly when the stride back is, so there is one number rather than two,
    // and the sign is folded by hand for the same reason WithinRadius squares
    // its comparison: this file includes its own header and nothing else.
    float const bound = maxDrop < maxRise ? maxDrop : maxRise;
    float const change = toZ - fromZ;
    return (change < 0.f ? -change : change) <= bound;
}

bool ProvenStepIsWorthTaking(bool wholeBearingHeld, float provedYards,
                             float minYards)
{
    // Nothing was truncated, so there is nothing for the floor to judge. See
    // the header: this is what keeps the rule strictly narrowing.
    if (wholeBearingHeld)
        return true;
    // A negative floor is refused rather than folded to its magnitude, for the
    // reason TravelEndpointWithinTolerance below refuses one: reading a
    // nonsense bound charitably would LOOSEN this rule, and every mistake this
    // predicate can make has to be the tight one.
    if (minYards < 0.f)
        return false;
    return provedYards >= minYards;
}

FootingRefusalVerdict FootingRefused(FootingRefusalState& state, uint32_t mapId,
                                     float x, float y, float episodeRadius,
                                     unsigned limit)
{
    // SOMEWHERE ELSE IS A NEW EPISODE. A character that has moved is asking a
    // different question about different ground, and the count that says "this
    // one is not getting out of here" has to be about one place or it means
    // nothing. Same shape as TerrainRecoveryStep's own anchor, including the
    // zero radius that turns the distance test off.
    bool const elsewhere =
        !state.anchored || state.mapId != mapId ||
        (episodeRadius > 0.f &&
         !WithinRadius(state.x, state.y, x, y, episodeRadius));
    if (elsewhere)
    {
        state.anchored = true;
        state.mapId = mapId;
        state.x = x;
        state.y = y;
        state.consecutive = 0;
    }
    ++state.consecutive;

    FootingRefusalVerdict out;
    out.consecutive = state.consecutive;
    // The FIRST poll of an episode is the one worth reading. Every poll after
    // it says the same thing about the same feet on the same ground.
    out.sayIt = state.consecutive == 1;
    out.giveUp = limit != 0 && state.consecutive >= limit;
    return out;
}

void FootingHeld(FootingRefusalState& state)
{
    // Unanchored as well as uncounted: a character that got a step is about to
    // be somewhere else, so keeping the old anchor would measure the next
    // episode from a place this character has left.
    state.consecutive = 0;
    state.anchored = false;
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

namespace
{

// The one place a member's two readings are turned into a gap, so the predicate
// and the line it prints can never disagree about where somebody is standing.
ApproachGap GapOf(DungeonRunMemberState const& member)
{
    ApproachGap gap;
    gap.horizontalYards = member.distanceFromStage;
    gap.verticalYards = member.verticalFromStage;
    gap.measured = member.distanceFromStage >= 0.f;
    return gap;
}

}  // namespace

bool DungeonRunBarrierMet(std::vector<DungeonRunMemberState> const& members,
                          ApproachLimits const& limits)
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
        // ARRIVED, NOT "INSIDE THE CIRCLE" (#217). Unmeasured and Overhead both
        // fail here, and Overhead is the one that is new: a member ten yards
        // out and a hundred and fifty yards up passed the circle test with room
        // to spare, so a party could be declared assembled on a clifftop and
        // then walked at a door it could not reach.
        if (ApproachShapeOf(GapOf(member), limits) != ApproachShape::Arrived)
            return false;
    }
    return true;
}

bool DungeonRunHoldsAtStage(DungeonRunMemberState const& member,
                            ApproachLimits const& limits)
{
    // THROUGH THE DOOR IS NOT AT THE DOOR. Asked first because every reading
    // below it is about a member standing outside, and an inside member has no
    // measured distance to the staging point at all - it would fall out at the
    // shape test anyway, but for the wrong reason and with the wrong sentence
    // in any log line built off this.
    if (member.inside)
        return false;
    if (!member.seen || !member.alive || member.inCombat)
        return false;
    // THE SAME SHAPE TEST THE BARRIER MAKES, ON THE SAME LIMITS, and it is one
    // call to one function rather than two readings that agree today. `Arrived`
    // is the only shape that may be held: `Closing` is a member still walking,
    // `Overhead` is a member on a ledge that has to find a route rather than
    // stand still on the wrong one, and `Unmeasured` is no reading at all.
    return ApproachShapeOf(GapOf(member), limits) == ApproachShape::Arrived;
}

std::string DungeonRunBarrierBlockers(std::vector<DungeonRunMemberState> const& members,
                                      ApproachLimits const& limits)
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
        else
        {
            ApproachGap const gap = GapOf(member);
            switch (ApproachShapeOf(gap, limits))
            {
                case ApproachShape::Arrived:
                    continue;
                // THE TWO REMAINING SHAPES GET THE SAME SENTENCE, and that is
                // the fix rather than a shortcut: ApproachWhere already carries
                // the height, so "80y out and 184y above it" and "80y out" are
                // told apart by the numbers in them rather than by a word this
                // would have to choose. A reader who sees the second half knows
                // the member is over the door; one who does not, knows it is
                // simply short of it.
                case ApproachShape::Overhead:
                case ApproachShape::Closing:
                case ApproachShape::Unmeasured:
                    why = ApproachWhere(gap);
                    break;
            }
        }

        if (!blockers.empty())
            blockers += ", ";
        blockers += member.name + " (" + why + ")";
    }
    return blockers;
}

DungeonApproach DungeonPortalApproach(std::uint32_t leaderMapId,
                                      std::uint32_t portalOutsideMapId,
                                      bool aCrossingExists)
{
    // EQUAL, NOT "SAME CONTINENT" OR "REACHABLE". The travel layer's rule is
    // literally that the aim's map is the character's map, so this is literally
    // that comparison. Anything softer here would be a promise the resolver
    // underneath does not keep.
    if (leaderMapId == portalOutsideMapId)
        return DungeonApproach::Walkable;
    // STILL NOT WALKABLE EITHER WAY. The difference is only what the caller
    // does next, and it is worth a value rather than a second `if` at the call
    // site because "the party is on the wrong continent and there is a boat"
    // and "the party is on the wrong continent" are different situations that
    // this function is the only place qualified to tell apart.
    return aCrossingExists ? DungeonApproach::NeedsCrossing
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

// IS THIS READING A NUMBER AT ALL? Written as the positive test for the reason
// WithinTheWorld above is: every comparison against a NaN is false, so a NaN
// fails this and is refused, where the negated form would have let it through.
//
// `value != value` alone catches the NaN; `value * 0` is a NaN for an infinity
// and zero for everything else, so the second half catches both infinities
// without naming a bound a distance might one day legitimately exceed. A gap is
// a DIFFERENCE rather than a coordinate, so WithinTheWorld is the wrong test for
// it - two legal points on one map are further apart than MAP_EDGE_YARDS.
// `<cmath>` would give std::isfinite; see the note at the top of this file for
// why the include is not taken for two comparisons.
//
// UNREACHABLE THROUGH THE ADAPTER TODAY, and guarded anyway. A staging point is
// put through StagingPointUsable before anything is measured against it and a
// position in the world is a real number, so neither half of a gap can be one
// of these. The guard costs one comparison; not having it costs an ARRIVAL
// declared on a reading nobody can interpret, which is the exact failure this
// whole section exists to stop.
bool FiniteReading(float value)
{
    return value == value && value * 0.f == 0.f;
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

ApproachShape ApproachShapeOf(ApproachGap const& gap, ApproachLimits const& limits)
{
    // NOT MEASURED, OR NOT A NUMBER, ARE THE SAME ANSWER. Both mean this poll
    // has nothing to say about where the subject is, and both must fail to the
    // verdict that leaves a caller waiting rather than to the one that advances
    // a phase. See FiniteReading.
    if (!gap.measured || !FiniteReading(gap.horizontalYards) ||
        !FiniteReading(gap.verticalYards))
        return ApproachShape::Unmeasured;

    float const horizontal = Magnitude(gap.horizontalYards);
    float const vertical = Magnitude(gap.verticalYards);

    // THE FLOOR IS ASKED FIRST, and it is the step bound's own statement read
    // backwards: a gap one step may bridge is a gap walking crosses, so it is
    // never a cliff however little ground is left. Both step numbers have to be
    // real for the rule to mean anything - a caller that supplied neither is
    // one this cannot answer for, and it gets the flat test it used to have.
    if (limits.stepYards > 0.f && limits.stepVerticalYards > 0.f &&
        vertical > limits.stepVerticalYards)
    {
        // MULTIPLIED OUT RATHER THAN DIVIDED, so a character standing directly
        // over the point is an ordinary comparison rather than a division by
        // zero. It also reads as what it is: the height left, against the
        // height the ground still to be walked could absorb at the one gradient
        // this module has measured.
        if (vertical * limits.stepYards > horizontal * limits.stepVerticalYards)
            return ApproachShape::Overhead;
    }

    // A NaN in either half fails this comparison and lands on Closing, which is
    // the answer that keeps a caller waiting rather than either acting on an
    // arrival or writing off an approach. Written as the positive test for that
    // reason; the negated form would have called it an arrival.
    return horizontal <= limits.arrivalYards ? ApproachShape::Arrived
                                             : ApproachShape::Closing;
}

float ApproachDistance(ApproachGap const& gap)
{
    if (!gap.measured || !FiniteReading(gap.horizontalYards) ||
        !FiniteReading(gap.verticalYards))
        return -1.f;

    double const horizontal = double(gap.horizontalYards);
    double const vertical = double(gap.verticalYards);
    return float(SquareRoot(horizontal * horizontal + vertical * vertical));
}

std::string ApproachWhere(ApproachGap const& gap)
{
    // The same three conditions as the verdict, and deliberately not "whatever
    // the verdict said": a caller may ask for the words without asking for the
    // shape, and the cast below is undefined on a NaN.
    if (!gap.measured || !FiniteReading(gap.horizontalYards) ||
        !FiniteReading(gap.verticalYards))
        return "no reading";

    std::string where =
        std::to_string(static_cast<int>(gap.horizontalYards)) + "y out";
    int const vertical = static_cast<int>(gap.verticalYards);
    if (vertical > 0)
        where += " and " + std::to_string(vertical) + "y above it";
    else if (vertical < 0)
        where += " and " + std::to_string(-vertical) + "y below it";
    return where;
}

ApproachLeg ApproachLegStep(ApproachRouteState& state, ApproachRoute const& route,
                            ApproachLimits const& limits)
{
    // A ROW WITH NO CORRIDOR IS THE THREE DOORS THAT ALREADY WORK, and they get
    // back exactly the behaviour they have. Asked first so that nothing below
    // can touch `state` on their behalf.
    if (!route.hasWaypoint)
        return ApproachLeg::Direct;

    // Walked once per run and not again. See ApproachRouteState.
    if (state.waypointPassed)
        return ApproachLeg::Direct;

    // A CORRIDOR WHOSE OWN LENGTH IS NOT A READING IS NOT A CORRIDOR. Negative
    // is ApproachDistance's "no reading", and it is refused rather than read
    // through an absolute value for the reason TravelEndpointWithinTolerance
    // gives about its tolerance: a sign that got in by accident must not
    // quietly become a rule nobody wrote. Refusing lands on the behaviour that
    // existed before this function did, which is the safe side.
    if (!(route.waypointToStagingYards >= 0.f))
        return ApproachLeg::Direct;

    // NO READING ON THE LEG MEANS NO LEG TO JUDGE. The leader is on another map
    // or was not found this poll; aiming him at a corridor whose distance from
    // him is unknown would be acting on nothing. Deliberately not sticky: the
    // next poll that can measure will decide.
    if (ApproachShapeOf(route.leaderToWaypoint, limits) == ApproachShape::Unmeasured)
        return ApproachLeg::Direct;

    // ALREADY PAST IT. Nearer the door than the corridor's start is, AND on
    // ground a walk can cover - the second half is what keeps the rim out, and
    // it is doing real work rather than belt and braces. The walkable surface
    // directly over the Wailing Caverns door is 145 yards from the staging
    // point and the corridor's start is 179, so the rim is THIRTY-FOUR YARDS
    // NEARER and passes the distance test outright. Only the shape says no.
    float const toStaging = ApproachDistance(route.leaderToStagingPoint);
    if (toStaging >= 0.f && toStaging <= route.waypointToStagingYards &&
        ApproachShapeOf(route.leaderToStagingPoint, limits) != ApproachShape::Overhead)
    {
        state.waypointPassed = true;
        return ApproachLeg::Direct;
    }

    // REACHED IT. Arrived is the same three-dimensional arrival the barrier and
    // the staging watchdog already use, so a leader who is ten yards out and a
    // hundred and fifty yards above the corridor's start has not reached it
    // either.
    if (ApproachShapeOf(route.leaderToWaypoint, limits) == ApproachShape::Arrived)
    {
        state.waypointPassed = true;
        return ApproachLeg::Direct;
    }

    return ApproachLeg::ToWaypoint;
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

DungeonWrongSide DungeonRunWrongSide(std::vector<DungeonRunEntryState> const& members)
{
    DungeonWrongSide wrongSide;
    for (DungeonRunEntryState const& member : members)
    {
        // Through already, or not in the world this poll. See the header for
        // why the second is not counted as being on this side rather than
        // counted and then not walked: a name that does not resolve is not on
        // the map either.
        if (member.through || !member.seen)
            continue;
        // Not on the door's map at all, so this member is somewhere else
        // entirely and is not on the wrong side of THIS door. The same negative
        // sentinel the crossing predicates read.
        if (member.distanceFromDoor < 0.f)
            continue;
        if (member.alive)
            wrongSide.walk.push_back(member.name);
        else
            wrongSide.wait.push_back(member.name);
    }
    return wrongSide;
}

char const* RejoinWalkName(RejoinWalk verdict)
{
    switch (verdict)
    {
        case RejoinWalk::Keep:              return "still walking back in";
        case RejoinWalk::BackWithTheFamily: return "back on its family's map";
        case RejoinWalk::Gone:              return "not in the world this poll";
        case RejoinWalk::GaveUp:            return "given up on";
    }
    return "not in the world this poll";
}

RejoinWalk ReadRejoinWalk(RejoinWalkFacts const& facts)
{
    // Asked first because it makes every other reading meaningless: a name that
    // does not resolve has no map to compare and no strategy to hand back.
    if (!facts.seen)
        return RejoinWalk::Gone;
    // Then the thing the walk was FOR, ahead of the clock. A walk that has
    // rejoined the family has succeeded however long it took, and reporting
    // that as a give-up would put a false line in the log about a walk that
    // worked.
    if (facts.withTheFamily)
        return RejoinWalk::BackWithTheFamily;
    // A backstop of zero or less is not a clock. See RejoinWalkFacts.
    if (facts.backstopSeconds > 0 && facts.walkingForSeconds >= facts.backstopSeconds)
        return RejoinWalk::GaveUp;
    return RejoinWalk::Keep;
}

bool RejoinWalkEnds(RejoinWalk verdict)
{
    // Written against the one enumerator that KEEPS rather than against the
    // three that end, for the reason SplitFollowerDrivesItself gives above: a
    // fifth answer added later is far more likely to be another way for a walk
    // to finish than another reason to carry on, and spelling it the other way
    // round would silently hold a lease open on a case nobody had considered.
    return verdict != RejoinWalk::Keep;
}

bool ArrivalReachesTrigger(float arrivalYards, float triggerRadiusYards)
{
    // A tolerance of zero or less is not a tolerance, and a trigger with no
    // radius is a BOX rather than a circle - the areatrigger table carries
    // both shapes - so neither is a door this question has an answer for.
    // Refusing both is deliberate rather than defensive: the caller's remedy is
    // to say so and aim nobody, which is better than a walk that reports
    // arriving somewhere it cannot open.
    if (arrivalYards <= 0.f || triggerRadiusYards <= 0.f)
        return false;
    return arrivalYards < triggerRadiusYards;
}

DoorAimHeight DoorAimOnTheFloor(float triggerZ, bool haveGround, float groundZ,
                                float arrivalYards, float triggerRadiusYards)
{
    DoorAimHeight out;
    // THE TRIGGER'S OWN Z IS THE ANSWER UNTIL SOMETHING BETTER IS PROVED, so
    // every refusal below is a plain return and none of them has to remember to
    // restore anything.
    out.z = triggerZ;

    // Nothing was found under the door. A probe that answers "no surface" is
    // the edge of the world, a hole, or an unloaded grid, and none of those is
    // a floor to move an aim onto.
    if (!haveGround)
        return out;

    // The same two refusals ArrivalReachesTrigger makes, for its own reasons,
    // and asked FIRST so a door this question has no answer for keeps its z
    // rather than being measured against a radius that is not one.
    if (arrivalYards <= 0.f || triggerRadiusYards <= 0.f)
        return out;

    float const correction = Magnitude(groundZ - triggerZ);

    // COMPARED AS SQUARES, so this needs no square root and no header to get
    // one. All three quantities are yards and none of them is negative here -
    // Magnitude has just made sure of the only one that could have been - so
    // squaring both sides is the same comparison and not an approximation of
    // it. The strictness is ArrivalReachesTrigger's own: strictly inside.
    if (arrivalYards * arrivalYards + correction * correction >=
        triggerRadiusYards * triggerRadiusYards)
        return out;

    out.z = groundZ;
    out.grounded = true;
    out.correctionYards = correction;
    return out;
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

bool DungeonClearBusyStillHolds(bool anyBusy, time_t advancedAt, time_t now,
                                time_t ceilingSeconds)
{
    // NOBODY BUSY IS NOT A SHORT HOLD, IT IS NO HOLD. Said first because it is
    // the only answer that does not depend on a clock: with nothing to hold the
    // patience down, the caller's ordinary ratchet is already the right judge
    // and this rule has nothing to add.
    if (!anyBusy)
        return false;

    // A ceiling of nothing is believed for nothing. See the header for why this
    // is the same reading `maximumSkips == 0` already has.
    if (ceilingSeconds == 0)
        return false;

    // NOTHING RECORDED YET MEANS THE CALLER IS ON ITS FIRST POLL. The safe
    // reading of "unknown" is the young one, because the expensive mistake here
    // is ending a run that is fighting a boss, and the caller stamps this in the
    // same pass it first marks the run.
    if (advancedAt == 0)
        return true;

    // STRICTLY GREATER, matching Ratchet's own patience test, so a ceiling of N
    // seconds is still believed at exactly N and the two clocks in this file
    // cannot disagree by one poll about what "past the bound" means.
    //
    // A `now` behind `advancedAt` - a clock stepped backwards under a running
    // worldserver - makes this subtraction negative and the hold simply stays
    // believed, which is the direction that costs a wait rather than a run.
    return now - advancedAt <= ceilingSeconds;
}

bool DungeonRunEnteredTheInstance(std::string const& outcome)
{
    // THE CLOSED SET, NAMED RATHER THAN DERIVED. These are the only outcomes
    // this module writes at a point in the state machine that comes before the
    // party is inside TOGETHER: the instance would not reset, the party would
    // not assemble outside the door, and (#384) the party crossed but the
    // census never reached everybody, so the run was never handed to the
    // clearing drive. Everything else it writes - 'left', 'stalled', 'wipe',
    // 'emptied' - is written about a party that was on the instance map with
    // the run under way, and so is an empty outcome, which is what the
    // cold-heartbeat close leaves behind on a row that only exists because
    // somebody was seen in there.
    return outcome != "reset_failed" && outcome != "staging_failed" &&
           outcome != "split_failed";
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

bool DungeonCampaignStopsOnFailures(unsigned trailingFailures, unsigned failureLimit)
{
    // Said first and on its own, because it is the branch a reader doubts: a
    // limit of zero is a bound that could not be read, and no campaign is
    // stopped on one.
    if (!failureLimit)
        return false;

    return trailingFailures >= failureLimit;
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

DungeonCompletion DungeonRunCompletion(uint32_t expectedMask, uint32_t completedMask)
{
    // NO BITS TO CREDIT MEANS NO ANSWER. Said first, because every other branch
    // below would report Complete for an empty expectation: zero bits are all
    // set, trivially and uselessly, and a run that ends the instant it starts is
    // worse than a run that never ends.
    if (!expectedMask)
        return DungeonCompletion::Unknowable;

    return (completedMask & expectedMask) == expectedMask ? DungeonCompletion::Complete
                                                          : DungeonCompletion::NotYet;
}

char const* DungeonRunExitOutcome(bool provedComplete, bool stalled)
{
    // PROOF OUTRANKS SUSPICION. A run can be both: the clearing watchdog can
    // have spent its skips on the last pull of a dungeon that then finished, and
    // the mask saying every encounter is credited is a fact where the stall is
    // an inference from not having moved.
    if (provedComplete)
        return "complete";
    return stalled ? "stalled" : "left";
}

CounterRole CounterRoleForAim(std::string const& aim)
{
    // The bridge's ECONOMY_ERRANDS, and the same three keywords the travel
    // roles carry, kept in step by the same discipline they already are: two
    // copies of a vocabulary that must agree, so a test compares them rather
    // than a comment asking somebody to remember.
    //
    // AND THE MATCH IS EXACT RATHER THAN A PREFIX. `travel_npc` also carries
    // "at:" and "trigger:" aims, and "profession trainer" contains neither of
    // these words but "class trainer" is one keyword away from being read
    // loosely by a future edit. An aim is a whole keyword or it is not this.
    if (aim == "vendor")
        return CounterRole::Vendor;
    if (aim == "banker")
        return CounterRole::Banker;
    if (aim == "repair")
        return CounterRole::Repairer;
    return CounterRole::None;
}

bool IsMaintenanceErrand(std::string const& aim)
{
    // ASKED OF THE ONE VOCABULARY (#378). This used to carry its own copy of
    // the three keywords above, which was fine while it was the only reader and
    // stopped being fine the moment the travel drive's arrival branch needed
    // the same answer plus the role.
    return CounterRoleForAim(aim) != CounterRole::None;
}

MaintenanceHold DungeonRunMaintenanceHold(std::string const& leaderAim,
                                          unsigned outstandingErrands,
                                          time_t heldForSeconds,
                                          time_t boundSeconds)
{
    bool const walking = IsMaintenanceErrand(leaderAim);
    bool const transacting = outstandingErrands > 0;
    if (!walking && !transacting)
        return MaintenanceHold::Open;

    // THE BOUND IS TESTED ONLY ONCE THERE IS SOMETHING TO BOUND, which is why
    // it is after the Open branch rather than before it. A campaign that is
    // simply running has no hold clock at all, and reporting Overdue for one
    // would open a run that was already free to open while claiming a fault.
    if (heldForSeconds > boundSeconds)
        return MaintenanceHold::Overdue;

    // WALKING BEFORE TRANSACTING when both are true. They regularly are: the
    // first member arrives and gets a row while the last is still on the road.
    // The two answers only differ in what the log says, and "still walking" is
    // the one an operator can act on, because it names a journey that may be
    // going wrong rather than a queue that is merely waiting for it.
    return walking ? MaintenanceHold::Walking : MaintenanceHold::Transacting;
}

StagingNudge StagingWatchdog(StagingStallState& state, ApproachGap const& gap,
                             bool measurable, time_t now,
                             RatchetLimits const& limits,
                             ApproachLimits const& approach)
{
    ApproachShape const shape = ApproachShapeOf(gap, approach);

    // STAGED, SO NOT WATCHED, AND THE LADDER GOES WITH IT. Inside the barrier
    // and on the same surface there is nothing left to close, and the leader in
    // particular is HELD there on purpose - a watchdog measuring him would find
    // a character that never gets nearer, because it is already there, and
    // start correcting the one member doing exactly what was asked.
    //
    // THE STATE IS RESET RATHER THAN LEFT, so a member that arrives, drifts
    // back out and returns is watched from the bottom of the ladder rather than
    // from the rung its last bad patch reached. This used to be the caller's
    // job and used to be asked with a flat radius, which is how a character on
    // the rim - ten yards out, a hundred and fifty yards up - was exempted from
    // being watched at all.
    if (shape == ApproachShape::Arrived)
    {
        state = StagingStallState();
        return StagingNudge::Nothing;
    }

    if (!measurable)
    {
        // Held, not read. `best` is deliberately left alone: a member that
        // fought its way forward and then came back out of combat nearer than
        // it has ever been should count that as progress, and a member that was
        // pushed backwards should not have its mark spoiled by the push.
        state.progress.since = now;
        return StagingNudge::Nothing;
    }

    // THE READING IS THE WHOLE GAP AND NOT ITS HORIZONTAL HALF (#217). A
    // descent counts as progress on this reading, which is what keeps a party
    // walking down a real ramp from being written off; a climb counts as going
    // backwards, which is what the horizontal span could never say.
    RatchetVerdict const verdict =
        Ratchet(state.progress, ApproachDistance(gap), now, limits);
    if (verdict.progressed)
    {
        // IT IS COMING. The clock has already been restarted by the ratchet;
        // what is undone here is the ladder, so a member that closes the gap
        // after two nudges is watched from the bottom again rather than being
        // one bad patch away from being given up on. `stranded` is undone with
        // it: a member that was over the door and has since found a way down is
        // not the case that diagnosis was about.
        state.escalated = 0;
        state.gaveUp = false;
        state.stranded = false;
        return StagingNudge::Nothing;
    }
    if (!verdict.stalled)
        return StagingNudge::Nothing;

    // ABOVE IT AND NO LONGER CLOSING, WHICH IS A DIFFERENT FACT FROM BEING
    // STUCK AND TAKES THE LADDER OUT OF USE RATHER THAN CLIMBING IT (#217).
    //
    // BOTH HALVES ARE NEEDED AND NEITHER IS ENOUGH. Overhead on its own is a
    // descent in progress: the Deadmines approach read 99/+88, 52/+83 and
    // 29/+43 on its way down to twelve yards out, and every one of those is
    // Overhead. Stalled on its own is the ordinary stuck-on-scenery case the
    // three rungs below exist for and fix. It is the conjunction - a large gap
    // straight up that has not shrunk for a whole patience window - that means
    // the route is wrong rather than the walking, and no rung reaches that.
    //
    // AND SPENDING THE RUNGS ON IT IS WORSE THAN USELESS. Two of the three
    // restart movement, and what they restart it into is a cliff edge: six of
    // the six deaths on the Wailing Caverns approach were falls, and the last
    // step's own footing refusal was firing throughout. Said once, and then
    // this stops touching the character at all.
    if (shape == ApproachShape::Overhead)
    {
        if (state.stranded)
            return StagingNudge::Nothing;
        state.stranded = true;
        return StagingNudge::Stranded;
    }

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

StagingAim StagingAimStep(bool legChanged, bool runStillOwns)
{
    // THE LEG FIRST, FOR THE REASON THE HEADER GIVES: both can be true on one
    // poll, and only one of them is news.
    if (legChanged)
        return StagingAim::NewLeg;
    return runStillOwns ? StagingAim::Hold : StagingAim::Rearm;
}

bool StagingAimRestartsMeasurement(StagingAim aim)
{
    return aim == StagingAim::NewLeg || aim == StagingAim::Rearm;
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

// --------------------------------------------- the command queue drain (#230) --
//
// The argument for both rules is in the header, next to the declarations. What
// is here is the arithmetic.

bool ClaimIsAbandoned(std::string const& claimedBy, std::string const& runToken,
                      time_t heldForSeconds, time_t leaseSeconds)
{
    // OURS IS NEVER ABANDONED. A row this run holds is being worked on inside
    // the very poll that would be asking, so the only thing ending it could
    // achieve is to cancel work that is about to report its own answer. Tested
    // first, and against a non-empty token, so that a run token that has not
    // been made yet cannot read as "everything unheld is mine" and disable the
    // lease for every row at once.
    if (!runToken.empty() && claimedBy == runToken)
        return false;

    // A row younger than the lease is somebody's in-flight work until proven
    // otherwise, including a `verifying` row deliberately sitting out its
    // read-back window.
    if (heldForSeconds < leaseSeconds)
        return false;

    return true;
}

CommandQueueVoice CommandQueueSay(CommandQueueVoiceState& state,
                                  CommandQueueSnapshot const& snapshot, time_t now,
                                  CommandQueueVoiceLimits const& limits)
{
    // THE LIVELOCK SIGNATURE, and it does not wait for an age to accumulate: a
    // poll that had rows in its hands and ran none of them is already the thing
    // that went unnoticed for twenty five minutes.
    bool const ranNothing = snapshot.held > 0 && snapshot.executed == 0;
    bool const notReached = snapshot.pending > 0 && limits.stuckSeconds != 0 &&
                            snapshot.oldestPendingAge >= limits.stuckSeconds;
    bool const stuck = ranNothing || notReached;
    bool const deep = limits.deepRows != 0 && snapshot.pending >= limits.deepRows;

    if (stuck || deep)
    {
        // Rate limited only while a complaint is ALREADY standing. The first
        // poll that goes wrong always speaks, whenever the last unrelated line
        // happened to be, because the beginning of an outage is the one moment
        // worth being loud at.
        if (state.complaining && limits.repeatSeconds != 0 &&
            now - state.lastSaid < limits.repeatSeconds)
            return CommandQueueVoice::Silent;

        state.complaining = true;
        state.lastSaid = now;
        // STUCK OUTRANKS DEEP. A queue that is both is stuck; saying it is
        // merely busy would be the reassuring half of a true statement.
        return stuck ? CommandQueueVoice::Stuck : CommandQueueVoice::Deep;
    }

    // Healthy. Said once, so the log has an end as well as a beginning, and
    // then nothing until something goes wrong again.
    if (state.complaining)
    {
        state.complaining = false;
        state.lastSaid = now;
        return CommandQueueVoice::Recovered;
    }

    return CommandQueueVoice::Silent;
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

GearResolvedProperty GearReadRandomProperty(std::vector<GearEnchantEffect> const& effects,
                                            bool everyEnchantmentRead)
{
    GearResolvedProperty resolved;

    // A SLOT THAT NAMED AN ENCHANTMENT NOBODY COULD LOOK UP is the one case
    // where the caller knows there is something and knows nothing about it.
    // Nothing to add, and the score stays a floor.
    resolved.unresolved = !everyEnchantmentRead || effects.empty();

    for (GearEnchantEffect const& effect : effects)
    {
        // NOT A STAT, SO NOT PRICED. An on-equip spell, a resistance, a damage
        // bonus - all real, none of them something this file weighs, and the
        // honest consequence is the same one an on-equip effect already has:
        // the number is a lower bound rather than the whole story.
        if (effect.type != ENCHANT_EFFECT_STAT)
        {
            resolved.unresolved = true;
            continue;
        }

        // A STAT WITH NO SIZE IS A SUFFIX THE CALLER DID NOT SCALE. The core
        // keeps a suffix's magnitude on the ITEM rather than in the
        // enchantment, so a zero here means the amount was never worked out.
        // Guessing one would be worse than saying so.
        if (!effect.amount)
        {
            resolved.unresolved = true;
            continue;
        }

        GearStat stat;
        stat.type = effect.stat;
        stat.value = effect.amount;
        resolved.stats.push_back(stat);
    }
    return resolved;
}

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


// ------------------------------------------ what an equip displaced (#372) --

void GearSlotCleared(GearSlotShadow& shadow, std::uint64_t stamp)
{
    // A clear of an already-bare slot carries no news and must not overwrite a
    // pending removal: the core clears the visible-item fields of a slot that
    // was already empty on more than one path, and letting that erase the
    // memory would lose exactly the item the next fill is about to be asked to
    // name.
    if (shadow.occupied)
    {
        shadow.removed = shadow.worn;
        shadow.removedValid = true;
        shadow.removedStamp = stamp;
    }

    shadow.observed = true;
    shadow.occupied = false;
    shadow.worn = GearSlotOccupant{};
}

void GearSlotSeen(GearSlotShadow& shadow, bool occupied, GearSlotOccupant const& item)
{
    shadow.observed = true;
    shadow.occupied = occupied;
    shadow.worn = occupied ? item : GearSlotOccupant{};

    // A direct reading of the world supersedes anything half-remembered. What
    // this seeds is "the slot is like this now", and a removal recorded before
    // it has already been either consumed or made irrelevant by it.
    shadow.removedValid = false;
    shadow.removed = GearSlotOccupant{};
    shadow.removedStamp = 0;
}

GearSlotBefore GearSlotDisplaced(GearSlotShadow const& shadow, std::uint64_t stamp)
{
    GearSlotBefore before;

    // AN OCCUPIED SLOT OVERWRITTEN IN PLACE. Not a path the pinned core takes
    // for equipment - it removes before it equips - but it is the honest answer
    // if it ever does, and asking it first means this function never depends on
    // that remaining true.
    if (shadow.occupied)
    {
        before.state = GearSlotState::Occupied;
        before.item = shadow.worn;
        return before;
    }

    // THE STRAIGHT SWAP. The clear and this fill are the same world update, so
    // they are the two halves of one Player::SwapItem and the item that came
    // out is what this one displaced. See the header for why the stamp is the
    // discriminator and not the wall clock.
    if (shadow.removedValid && shadow.removedStamp == stamp)
    {
        before.state = GearSlotState::Occupied;
        before.item = shadow.removed;
        return before;
    }

    // Looked at, and bare. This is the first item into the slot, or the first
    // since whatever left it empty in some earlier update.
    if (shadow.observed)
    {
        before.state = GearSlotState::Empty;
        return before;
    }

    // Never looked at. Says so, rather than guessing empty - see the header.
    return before;
}

void GearSlotFilled(GearSlotShadow& shadow, GearSlotOccupant const& item)
{
    shadow.observed = true;
    shadow.occupied = true;
    shadow.worn = item;

    // ONE CLEAR EXPLAINS AT MOST ONE FILL. Without this a swap in the main hand
    // would leave its displaced item pending, and a later fill of the same slot
    // in the same world update - which two-handed weapons and the core's own
    // AutoUnequipOffhandIfNeed do produce - would name it a second time.
    shadow.removedValid = false;
    shadow.removed = GearSlotOccupant{};
    shadow.removedStamp = 0;
}

char const* GearPriorWord(GearSlotState state)
{
    switch (state)
    {
        case GearSlotState::Occupied: return GearPrior::Item;
        case GearSlotState::Empty:    return GearPrior::Empty;
        case GearSlotState::Unobserved: break;
    }
    return GearPrior::Unknown;
}

std::string GearSwapDetail(unsigned slot, GearSlotBefore const& before)
{
    std::string detail = "slot " + std::to_string(slot);
    switch (before.state)
    {
        case GearSlotState::Occupied:
            // The name and not the entry, because this column is the half a
            // person reads; the entry is in prior_id for everything else.
            detail += " over " + (before.item.name.empty()
                                      ? std::string("an item with no template")
                                      : before.item.name);
            break;
        case GearSlotState::Empty:
            detail += " over nothing";
            break;
        case GearSlotState::Unobserved:
            detail += " over an unobserved slot";
            break;
    }

    // The column is VARCHAR(255) and an item name is VARCHAR(255) on its own,
    // so a long enough name would be truncated by MySQL rather than by this
    // module. Truncating here instead keeps the sentence readable and, more to
    // the point, keeps the truncation a decision this file can be tested on.
    if (detail.size() > 255)
        detail.resize(255);
    return detail;
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


// ------------------------------------------- the town trip: repair and buy --

char const* TownRetryWord(TownRetry retry)
{
    switch (retry)
    {
        case TownRetry::Never:
            return "never";
        case TownRetry::Elsewhere:
            return "elsewhere";
        case TownRetry::Later:
            break;
    }
    return "later";
}

namespace
{

// The words of the line, split on runs of blanks. The bank parser's own
// splitter, duplicated here rather than shared because both live in anonymous
// namespaces inside this one translation unit and a shared one would have to
// become part of the header's public surface for no caller outside it.
std::vector<std::string> TownWords(std::string const& text)
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

// `<key>:<digits>` -> true and the number, or false. Anything that is not
// digits, an empty value, or a value that would not fit in uint32 is a
// malformed word and not a zero: a wrapped number names a different item and a
// silent zero is the core's own "all" special case in both of these grammars.
bool TownKeyed(std::string const& word, char const* key, uint32_t& value)
{
    std::string const prefix = std::string(key) + ":";
    if (word.size() <= prefix.size() || word.compare(0, prefix.size(), prefix) != 0)
        return false;
    std::string const digits = word.substr(prefix.size());
    if (digits.size() > 10)
        return false;
    uint64_t parsed = 0;
    for (char const c : digits)
    {
        if (c < '0' || c > '9')
            return false;
        parsed = parsed * 10 + static_cast<uint64_t>(c - '0');
    }
    if (parsed > 0xFFFFFFFFull)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

}  // namespace

RepairRequest ParseRepairRequest(std::string const& command)
{
    RepairRequest request;
    std::vector<std::string> const words = TownWords(command);

    if (words.empty())
    {
        request.error = "malformed repair: want all, or item guid:<item_instance.guid>";
        return request;
    }

    if (words[0] == "all")
    {
        if (words.size() == 1)
        {
            request.verb = RepairVerb::All;
            return request;
        }
        request.error = "malformed repair: all takes no arguments";
        return request;
    }

    if (words[0] == "item")
    {
        if (words.size() != 2)
        {
            request.error = "malformed repair: want item guid:<item_instance.guid>";
            return request;
        }
        uint32_t guid = 0;
        if (!TownKeyed(words[1], "guid", guid) || guid == 0)
        {
            request.error = "malformed repair: item must be guid:<item_instance.guid>, not 0";
            return request;
        }
        request.verb = RepairVerb::One;
        request.itemGuid = guid;
        return request;
    }

    request.error = "malformed repair: unknown verb (want all, or item guid:<n>)";
    return request;
}

int ChooseRepairer(std::vector<RepairerCandidate> const& candidates)
{
    int best = -1;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (best < 0)
        {
            best = static_cast<int>(i);
            continue;
        }
        RepairerCandidate const& incumbent = candidates[static_cast<size_t>(best)];
        RepairerCandidate const& candidate = candidates[i];

        // Cheaper first. Every candidate is already inside interaction
        // distance, so the yards cost nothing and the discount costs gold.
        if (candidate.discount != incumbent.discount)
        {
            if (candidate.discount < incumbent.discount)
                best = static_cast<int>(i);
            continue;
        }
        if (candidate.distance < incumbent.distance)
            best = static_cast<int>(i);
    }
    return best;
}

TownRetry RepairRefusalRetry(std::string const& detail)
{
    // The literals mod_overseer.cpp's DoRepair returns, grouped by what would
    // have to change for the same row to succeed.
    static char const* const NEVER[] = {
        "malformed repair: want all, or item guid:<item_instance.guid>",
        "malformed repair: all takes no arguments",
        "malformed repair: want item guid:<item_instance.guid>",
        "malformed repair: item must be guid:<item_instance.guid>, not 0",
        "malformed repair: unknown verb (want all, or item guid:<n>)",
        "malformed repair request",
        "item not carried",
        "item has no template",
        "item cannot be damaged",
        "item is not damaged",
        "nothing is damaged",
    };
    static char const* const ELSEWHERE[] = {
        "repairer not in range",
    };

    for (char const* literal : NEVER)
        if (detail == literal)
            return TownRetry::Never;
    for (char const* literal : ELSEWHERE)
        if (detail == literal)
            return TownRetry::Elsewhere;
    return TownRetry::Later;
}

BuyRequest ParseBuyRequest(std::string const& command)
{
    BuyRequest request;
    std::vector<std::string> const words = TownWords(command);

    if (words.empty())
    {
        request.error = "malformed buy: want entry:<item_template.entry>[ count:<n>][ max:<copper>]";
        return request;
    }

    uint32_t entry = 0;
    if (!TownKeyed(words[0], "entry", entry) || entry == 0)
    {
        request.error = "malformed buy: first word must be entry:<item_template.entry>, not 0";
        return request;
    }

    bool haveCount = false;
    bool haveMax = false;
    uint32_t count = 1;
    uint32_t maxCopper = 0;

    for (size_t i = 1; i < words.size(); ++i)
    {
        uint32_t value = 0;
        if (TownKeyed(words[i], "count", value))
        {
            if (haveCount || value == 0)
            {
                request.error = haveCount ? "malformed buy: count given twice"
                                          : "malformed buy: count must be 1 or more";
                return request;
            }
            haveCount = true;
            count = value;
            continue;
        }
        if (TownKeyed(words[i], "max", value))
        {
            if (haveMax)
            {
                request.error = "malformed buy: max given twice";
                return request;
            }
            haveMax = true;
            maxCopper = value;
            continue;
        }
        request.error = "malformed buy: unknown word (want count:<n> or max:<copper>)";
        return request;
    }

    request.valid = true;
    request.entry = entry;
    request.count = count;
    request.capped = haveMax;
    request.maxCopper = maxCopper;
    return request;
}

int ChooseBuyVendor(std::vector<BuyVendorCandidate> const& candidates)
{
    // A vendor's rank, high is better: it has the thing (2), it sells the
    // thing but is out of it (1), it does not sell the thing (0). Named rather
    // than written as two nested conditionals so the tie-breaking below reads
    // as one comparison and not three.
    auto rank = [](BuyVendorCandidate const& c) -> int
    { return c.stocksItem ? (c.inStock ? 2 : 1) : 0; };

    int best = -1;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (best < 0)
        {
            best = static_cast<int>(i);
            continue;
        }
        BuyVendorCandidate const& incumbent = candidates[static_cast<size_t>(best)];
        BuyVendorCandidate const& candidate = candidates[i];

        int const incumbentRank = rank(incumbent);
        int const candidateRank = rank(candidate);
        if (candidateRank != incumbentRank)
        {
            if (candidateRank > incumbentRank)
                best = static_cast<int>(i);
            continue;
        }
        if (candidate.discount != incumbent.discount)
        {
            if (candidate.discount < incumbent.discount)
                best = static_cast<int>(i);
            continue;
        }
        if (candidate.distance < incumbent.distance)
            best = static_cast<int>(i);
    }
    return best;
}

TownRetry BuyRefusalRetry(std::string const& detail)
{
    static char const* const NEVER[] = {
        "malformed buy: want entry:<item_template.entry>[ count:<n>][ max:<copper>]",
        "malformed buy: first word must be entry:<item_template.entry>, not 0",
        "malformed buy: count must be 1 or more",
        "malformed buy: count given twice",
        "malformed buy: max given twice",
        "malformed buy: unknown word (want count:<n> or max:<copper>)",
        "malformed buy request",
        "no such item",
        "item is not for this class",
        "item is for the other faction",
        "item is not bought with gold",
        "price exceeds the cap the row set",
        "count exceeds what the packet carries",
        "count would overflow the purse",
    };
    static char const* const ELSEWHERE[] = {
        "vendor not in range",
        "vendor does not stock the item",
    };

    for (char const* literal : NEVER)
        if (detail == literal)
            return TownRetry::Never;
    for (char const* literal : ELSEWHERE)
        if (detail == literal)
            return TownRetry::Elsewhere;
    return TownRetry::Later;
}

char const* DeathDriverName(DeathDriver driver)
{
    switch (driver)
    {
        case DeathDriver::Unknown:      return "unknown";
        case DeathDriver::Recovery:     return "recovery";
        case DeathDriver::Errand:       return "errand";
        case DeathDriver::Following:    return "following";
        case DeathDriver::Fighting:     return "fighting";
        case DeathDriver::Thrown:       return "thrown";
        case DeathDriver::Idle:         return "idle";
        case DeathDriver::Unattributed: return "unattributed";
    }
    return "unknown";
}

char const* MoveGeneratorName(MoveGenerator generator)
{
    switch (generator)
    {
        case MoveGenerator::Unsampled: return "";
        case MoveGenerator::Idle:      return "idle";
        case MoveGenerator::Follow:    return "follow";
        case MoveGenerator::Point:     return "point";
        case MoveGenerator::Chase:     return "chase";
        case MoveGenerator::Flee:      return "flee";
        case MoveGenerator::Thrown:    return "effect";
        case MoveGenerator::Other:     return "other";
    }
    return "";
}

DeathDriver NameTheDriver(DeathAttribution const& a)
{
    if (!a.sampled || a.movement == MoveGenerator::Unsampled)
        return DeathDriver::Unknown;

    // This module's own remedy, and it outranks the generator on purpose - see
    // the header. A window of zero is a caller saying "never attribute a death
    // to a recovery", and a negative age is "there has never been one".
    if (a.recoveryWindow > 0 && a.recoverySeconds >= 0 &&
        a.recoverySeconds <= a.recoveryWindow)
        return DeathDriver::Recovery;

    switch (a.movement)
    {
        case MoveGenerator::Thrown:
            return DeathDriver::Thrown;
        case MoveGenerator::Chase:
        case MoveGenerator::Flee:
            return DeathDriver::Fighting;
        case MoveGenerator::Follow:
            return DeathDriver::Following;
        case MoveGenerator::Idle:
            // Nothing had hold of it. An aim it was not executing is worth
            // seeing, and the aim columns are on the same row, so this does
            // not overwrite the fact with the intention.
            return DeathDriver::Idle;
        case MoveGenerator::Point:
        case MoveGenerator::Other:
            break;
        case MoveGenerator::Unsampled:
            return DeathDriver::Unknown;
    }

    return (a.hasTravelTarget || a.hasQuestAim) ? DeathDriver::Errand
                                                : DeathDriver::Unattributed;
}

float YardsFallen(bool sampled, float lastZ, float deathZ)
{
    if (!sampled)
        return -1.f;
    float const dropped = lastZ - deathZ;
    return dropped > 0.f ? dropped : 0.f;
}

float FallDamageShare(float yardsDropped, float safeFallYards, float rate)
{
    // The core's gate is on the distance itself, before any rate is applied,
    // so no Rate.Damage.Fall makes a short drop hurt. Written as a negated
    // >= so a NaN drop falls out here rather than propagating a NaN share.
    if (!(yardsDropped >= FALL_DAMAGE_MIN_YARDS) || rate <= 0.f)
        return 0.f;

    float const share =
        (FALL_DAMAGE_SLOPE * (yardsDropped - safeFallYards) +
         FALL_DAMAGE_INTERCEPT) * rate;

    if (share <= 0.f)
        return 0.f;
    // The core clamps the damage at max health, so the share clamps at one.
    return share > 1.f ? 1.f : share;
}

float LethalFallYards(float safeFallYards, float rate)
{
    // Solve SLOPE * (yards - safeFall) + INTERCEPT >= 1 / rate. A rate of
    // zero disables fall damage outright, and nothing is lethal then.
    if (rate <= 0.f)
        return -1.f;

    float const yards =
        (1.f / rate - FALL_DAMAGE_INTERCEPT) / FALL_DAMAGE_SLOPE + safeFallYards;

    // The distance gate still applies underneath the arithmetic: a rate high
    // enough to make a one-yard drop lethal still never gets to charge for it.
    return yards < FALL_DAMAGE_MIN_YARDS ? FALL_DAMAGE_MIN_YARDS : yards;
}

FallAccount AccountForFall(float recordedYardsFallen, float safeFallYards,
                           float rate, float deathZ, float voidPlaneZ)
{
    // THE PLANE FIRST, BEFORE THE UNSAMPLED MARKER EVEN. A body below the kill
    // plane is the one answer that does not need the distance to be true, and
    // 9 of the 59 measured void deaths carried no sample at all - calling
    // those "unsampled" would hide the only fact about them anybody needs.
    // A plane at or above zero is the caller saying it is not asking.
    if (voidPlaneZ < 0.f && deathZ < voidPlaneZ)
        return FallAccount::VoidPlane;

    // Negative is YardsFallen's unsampled marker, and it is not zero: see the
    // header. Saying "it did not fall" about a row nobody sampled is exactly
    // the mistake this whole function exists to stop.
    if (recordedYardsFallen < 0.f)
        return FallAccount::Unsampled;
    if (recordedYardsFallen == 0.f)
        return FallAccount::NoDrop;
    if (recordedYardsFallen < FALL_DAMAGE_MIN_YARDS)
        return FallAccount::TooShortToHurt;

    return FallDamageShare(recordedYardsFallen, safeFallYards, rate) >= 1.f
               ? FallAccount::EnoughToKill
               : FallAccount::Survivable;
}

char const* FallAccountName(FallAccount account)
{
    switch (account)
    {
        case FallAccount::Unsampled:      return "unsampled";
        case FallAccount::NoDrop:         return "no drop";
        case FallAccount::TooShortToHurt: return "too short to hurt it";
        case FallAccount::Survivable:     return "could not have killed it";
        case FallAccount::EnoughToKill:   return "enough to kill it";
        // Deliberately not a distance and deliberately not the word "fall".
        // The reader this sentence is for is the one about to go and look at
        // the fall guard again.
        case FallAccount::VoidPlane:
            return "not a fall at all: it crossed the void plane and the core "
                   "killed it outright";
    }
    return "unsampled";
}

RevivalMoveVerdict RevivalMayCrossMaps(RevivalMove const& move)
{
    RevivalMoveVerdict verdict;

    // The party's map: the most common one among the OTHER members. Counted by
    // hand rather than with a map container so this file keeps needing nothing
    // but <string> and <vector>, the same reason WithinRadius folds its own
    // distance.
    size_t best = 0;
    for (size_t i = 0; i < move.partyMapIds.size(); ++i)
    {
        size_t count = 0;
        for (size_t j = 0; j < move.partyMapIds.size(); ++j)
            if (move.partyMapIds[j] == move.partyMapIds[i])
                ++count;
        // Strictly greater, so a tie keeps the map seen first.
        if (count > best)
        {
            best = count;
            verdict.partyMapKnown = true;
            verdict.partyMapId = move.partyMapIds[i];
        }
    }

    // 1. Nothing to split.
    if (!verdict.partyMapKnown)
    {
        verdict.mayMove = true;
        return verdict;
    }

    // 2. The bind is where the party already is.
    if (move.bindMapId == verdict.partyMapId)
    {
        verdict.mayMove = true;
        return verdict;
    }

    // 3. Somewhere on this map exists, so the ocean is not the only option and
    //    is therefore not an option.
    if (move.graveyardOnThisMap)
        return verdict;

    // 4. Nothing on this map at all. Move, and say what it costs.
    verdict.mayMove = true;
    verdict.splitsParty = true;
    return verdict;
}

char const* FollowGapName(FollowGap gap)
{
    switch (gap)
    {
        case FollowGap::SplitAcrossMaps: return "split across maps";
        case FollowGap::InFormation:     return "in formation";
        case FollowGap::Trailing:        return "trailing";
        case FollowGap::Stranded:        return "stranded";
    }
    return "split across maps";
}

FollowGap ReadFollowGap(bool sameMap, float distance2d,
                        FollowGapLimits const& limits)
{
    // Asked first and answered alone. A cross-map pair has no distance, so
    // nothing below may look at the one that was handed in.
    if (!sameMap)
        return FollowGap::SplitAcrossMaps;
    if (distance2d <= limits.formationYards)
        return FollowGap::InFormation;
    if (distance2d <= limits.catchUpYards)
        return FollowGap::Trailing;
    return FollowGap::Stranded;
}

bool FollowGapIsBehind(FollowGap gap)
{
    return gap == FollowGap::Trailing || gap == FollowGap::Stranded;
}

char const* SplitErrandName(SplitErrand errand)
{
    switch (errand)
    {
        case SplitErrand::Nothing:        return "nothing";
        case SplitErrand::NeedsTheFamily: return "needs the family";
        case SplitErrand::SelfContained:  return "self-contained";
    }
    return "nothing";
}

SplitErrand ReadSplitErrand(std::string const& target)
{
    // An empty column is not an errand. Answered first so that neither test
    // below has to think about the empty string.
    if (target.empty())
        return SplitErrand::Nothing;
    // The two shapes this module writes for itself. `rfind(s, 0) == 0` is the
    // starts-with this file already uses at every other aim-shape test, kept
    // the same here so a reader comparing them does not have to check whether
    // two spellings mean two things.
    if (target.rfind("at:", 0) == 0 || target.rfind("trigger:", 0) == 0)
        return SplitErrand::NeedsTheFamily;
    // A role keyword or a bare creature entry. Deliberately NOT matched against
    // the keyword table: that table lives in the module beside the NPC flags it
    // maps onto and is duplicated in the Python bridge, and a third copy here
    // would be a third thing to keep in step for no gain. An unknown keyword is
    // already refused by ResolveTravelTarget, which is where "there is no such
    // role" belongs; this decision is only about whether the destination was
    // picked relative to the family.
    return SplitErrand::SelfContained;
}

bool ErrandRunsAlone(std::string const& target)
{
    return ReadSplitErrand(target) == SplitErrand::SelfContained;
}

bool SplitFollowerDrivesItself(std::string const& target)
{
    // Written against the enumerator this REFUSES rather than the two it
    // allows, on purpose. The refusal is the whole safety property - an aim the
    // family owns must not be walked away from - and a new SplitErrand shape
    // added later is far more likely to be another thing a character deals with
    // alone than another thing the party picked for it. Spelled the other way
    // round, a new enumerator would silently freeze a character again, which is
    // exactly the failure this exists to end.
    return ReadSplitErrand(target) != SplitErrand::NeedsTheFamily;
}

bool WalkAlreadyInFlight(bool reissueForced, bool canAct, bool atSameDestination)
{
    // The watchdog's override first, because it is the one input that means
    // "whatever you think you can see, issue it anyway".
    if (reissueForced)
        return false;
    // Then the one this was written for. Asked BEFORE the destination, not
    // because the order changes the answer but because it is the reading a
    // future caller is most likely to forget it has to take: the destination
    // comes out of the bot's own state, and that state outlives the strategy
    // that was acting on it.
    if (!canAct)
        return false;
    return atSameDestination;
}

char const* AimedMoverName(AimedMover verdict)
{
    switch (verdict)
    {
        case AimedMover::Walks:             return "walks already";
        case AimedMover::HeldOnPurpose:     return "held on purpose";
        case AimedMover::GrantToLeader:     return "grant to the leader";
        case AimedMover::GrantToSteerer:    return "grant to the steerer";
        case AimedMover::RefuseInFormation: return "refuse, in formation";
        case AimedMover::RefuseCutOff:      return "refuse, cut off";
    }
    return "refuse, in formation";
}

AimedMover ReadAimedMover(AimedMoverFacts const& facts)
{
    // Asked first because it is the one input that makes every other one
    // irrelevant: a character that can already act on its aim is not a case
    // this decision exists for.
    if (facts.carriesStrategy)
        return AimedMover::Walks;
    // THEN THIS MODULE'S OWN HOLD, AHEAD OF EVERY ROLE BELOW. The hold is the
    // one reason the strategy can be missing that is not a fault, and it
    // outranks the roles rather than sitting beside them: a held LEADER is the
    // measured case, and granting it back would override a hold this module
    // placed three seconds earlier and still intends to lift itself.
    if (facts.heldAfterRevival || facts.heldStill)
        return AimedMover::HeldOnPurpose;
    // The leader before the steerer, because the two are not exclusive and the
    // leader is the stronger claim. A leader escorted by its own dungeon run is
    // both, and the answer worth acting on for it names the reason that does not
    // end when the escort does.
    if (facts.leadsItsParty)
        return AimedMover::GrantToLeader;
    if (facts.steersItself)
        return AimedMover::GrantToSteerer;
    // Both remaining answers are refusals, and they differ only in the remedy
    // they can offer, which is why they are two enumerators rather than one with
    // a flag hung off it. See SplitErrand above for why "aim the leader instead"
    // is the one thing that cannot work for a follower on another map.
    if (facts.cutOffFromLeader)
        return AimedMover::RefuseCutOff;
    return AimedMover::RefuseInFormation;
}

bool AimedMoverGrants(AimedMover verdict)
{
    return verdict == AimedMover::GrantToLeader ||
           verdict == AimedMover::GrantToSteerer;
}

char const* CrossingLegName(CrossingLeg leg)
{
    switch (leg)
    {
        case CrossingLeg::Unknown:     return "unknown";
        case CrossingLeg::OffRoute:    return "off route";
        case CrossingLeg::WalkToBerth: return "walk to the berth";
        case CrossingLeg::WaitForTransport: return "wait for the transport";
        case CrossingLeg::Aboard:      return "aboard";
        case CrossingLeg::Disembark:   return "disembark";
        case CrossingLeg::Ashore:      return "ashore";
    }
    return "unknown";
}

char const* CrossingActionName(CrossingAction action)
{
    switch (action)
    {
        case CrossingAction::Wait:      return "wait";
        case CrossingAction::Refuse:    return "refuse";
        case CrossingAction::Walk:      return "walk";
        case CrossingAction::Hold:      return "hold";
        case CrossingAction::Ride:      return "ride";
        case CrossingAction::Disembark: return "disembark";
        case CrossingAction::Done:      return "done";
    }
    return "wait";
}

CrossingStep ReadCrossing(CrossingWorld const& world,
                          std::vector<CrossingMember> const& members,
                          CrossingLimits const& limits)
{
    CrossingStep step;

    for (CrossingMember const& m : members)
    {
        if (!m.readable)
        {
            ++step.unreadable;
            continue;
        }
        ++step.readable;

        if (m.isLeader)
        {
            step.leaderReadable = true;
            step.leaderAboard = m.aboard;
            step.leaderOnOrigin = !m.aboard && m.mapId == world.originMap;
            step.leaderAtBerth = step.leaderOnOrigin &&
                                 m.berthDistance <= limits.berthArrivedYards;
        }

        // A PASSENGER IS COUNTED TWICE ON PURPOSE, ONCE AS A PASSENGER AND ONCE
        // BY WHERE IT IS. The first version stopped at the first count, so a
        // character that had arrived on the destination map while still standing
        // on the deck could never be seen as needing to get off: it was aboard,
        // and aboard skipped the map entirely. Both facts are true at once and
        // both are needed.
        if (m.aboard)
        {
            ++step.aboard;
            if (m.mapId == world.destinationMap)
                ++step.stillAboard;
            continue;
        }

        if (m.mapId == world.destinationMap)
            ++step.ashore;
        else if (m.mapId == world.originMap)
            ++step.waiting;
        else
            ++step.offRoute;
    }

    // 1. AN UNREADABLE MEMBER OUTRANKS EVERYTHING. Not "most of the party has
    //    landed": a party is five characters and this reading covers four.
    if (step.unreadable || !step.readable)
    {
        step.leg = CrossingLeg::Unknown;
        step.action = CrossingAction::Wait;
        return step;
    }

    // 2. THE CROSSING ITSELF, BEFORE ANY STEP ALONG IT. A crossing between one
    //    map and itself is a caller bug rather than a finished crossing, and
    //    answering Done would hide it.
    if (world.originMap == world.destinationMap)
    {
        step.leg = CrossingLeg::Unknown;
        step.action = CrossingAction::Refuse;
        return step;
    }

    // 3. SOMEBODY ON A THIRD MAP. Refused rather than waited on: no boat on
    //    this route calls there, so nothing about this crossing improves it.
    //    Ahead of Done and Disembark because it is the worse fact and must not
    //    be described in a vocabulary that does not fit it.
    if (step.offRoute)
    {
        step.leg = CrossingLeg::OffRoute;
        step.action = CrossingAction::Refuse;
        return step;
    }

    // 4. STILL ON THE DECK AT THE FAR END. Asked BEFORE Done, because a
    //    passenger standing on the destination map has not arrived anywhere: it
    //    is on a boat that is about to sail back, and calling that Done ends the
    //    crossing at the exact moment it is most likely to be undone.
    //
    //    THIS MODULE CANNOT WALK ANYBODY OFF, which is why the action is its own
    //    kind of doing nothing rather than an order. Getting off a deck needs
    //    the same thing getting on needs and does not have: a place the world
    //    agrees is standable, next to a boat. Until one is supplied this
    //    supervises and says so, which is strictly better than the previous
    //    behaviour of never looking again.
    if (step.stillAboard)
    {
        step.leg = CrossingLeg::Disembark;
        step.action = CrossingAction::Disembark;
        return step;
    }

    // 5. EVERYBODY ASHORE, AND OFF EVERY TRANSPORT, ENDS IT. Both halves: the
    //    `aboard` test above has already taken every passenger out of `ashore`,
    //    so this cannot be reached by somebody standing on a deck.
    if (step.ashore == step.readable)
    {
        step.leg = CrossingLeg::Ashore;
        step.action = CrossingAction::Done;
        return step;
    }

    // 6. THE LEADER IS ABOARD, so the transport owns the outcome and nothing is
    //    aimed. NOTE WHAT THIS IS NOT: it is not "somebody is aboard". A
    //    follower on the deck while the leader is still walking is an ordinary
    //    and expected state, and the previous version answered Ride to it, which
    //    released the leader's aim and left the boat to sail without him.
    if (step.leaderAboard)
    {
        step.leg = CrossingLeg::Aboard;
        step.action = CrossingAction::Ride;
        return step;
    }

    // 7. THE FACTS THE WALK NEEDS. Each is a refusal rather than a wait, because
    //    none of them arrives by waiting: a boat that does not serve both maps
    //    never will, and a berth nothing has validated is not made walkable by
    //    another poll.
    //
    //    `berthKnown` IS NO LONGER "A STOP FRAME WAS FOUND". It is "the world
    //    agreed a character may stand here", and while nothing can establish
    //    that, this refuses. That refusal is the deliverable, not a gap in it:
    //    the alternative is aiming a family at a ship's mooring.
    if (!world.transportFound || !world.berthKnown || !world.landingKnown ||
        world.berthGuarded || world.overdue)
    {
        step.leg = CrossingLeg::WalkToBerth;
        step.action = CrossingAction::Refuse;
        return step;
    }

    // 8. THE LEADER IS THE ONLY CHARACTER THIS EVER AIMS. A leader already on
    //    the far side is therefore a crossing this cannot drive: the followers
    //    left behind need their leader's aim and it is not on their map to be
    //    given. Said rather than worked around, because aiming a follower on its
    //    own is the scatter this repository keeps paying for.
    if (!step.leaderOnOrigin)
    {
        step.leg = CrossingLeg::WalkToBerth;
        step.action = CrossingAction::Refuse;
        return step;
    }

    // 9. ALREADY THERE. Answered before Walk, and the difference between the
    //    two is the whole of the loop this closes: the travel drive releases an
    //    `at:` errand once the traveller is within five yards of it, and a
    //    reading that still said Walk made the caller claim it straight back.
    //    Each of those reclaims looked like a brand new errand to the drive,
    //    which restamped the clock the death breaker measures its window from,
    //    so a leader waiting at a lethal pier had a window that was permanently
    //    zero and a death query that was never issued.
    //
    //    THE TOLERANCE IS DELIBERATELY THE LOOSER OF THE TWO. This reads "at the
    //    berth" from further out than the travel drive reads "arrived", so the
    //    crossing has stopped asking for the walk before the drive finishes it.
    //    The other order would leave a gap in which neither is true.
    if (step.leaderAtBerth)
    {
        step.leg = CrossingLeg::WaitForTransport;
        step.action = CrossingAction::Hold;
        return step;
    }

    step.leg = CrossingLeg::WalkToBerth;
    step.action = CrossingAction::Walk;
    return step;
}

std::string CrossingExplanation(CrossingStep const& step, CrossingWorld const& world)
{
    std::string const origin = std::to_string(world.originMap);
    std::string const destination = std::to_string(world.destinationMap);

    switch (step.action)
    {
        case CrossingAction::Wait:
            return "the world is not answering for " +
                   std::to_string(step.unreadable) + " of " +
                   std::to_string(step.unreadable + step.readable) +
                   " members, so nothing about this crossing is decided; "
                   "a member that cannot be read has not arrived anywhere";

        case CrossingAction::Done:
            return "every member read on map " + destination +
                   " and off every transport, so the crossing is over and "
                   "ordinary travel takes it from here";

        case CrossingAction::Disembark:
            return std::to_string(step.stillAboard) +
                   " member(s) are on map " + destination +
                   " but still standing on the transport, which is not the same "
                   "fact as being ashore and is not an arrival; this module has "
                   "no validated landing to walk them to, so it watches and "
                   "says so rather than ending the crossing on a deck";

        case CrossingAction::Ride:
            return "the leader is aboard between maps " + origin + " and " +
                   destination +
                   ", and the transport carries and teleports its own "
                   "passengers, so nothing is aimed until it has landed";

        case CrossingAction::Hold:
            return "the leader is at the berth on map " + origin +
                   " with nothing to do but wait for the transport; no errand is "
                   "claimed or released while it stands there, because a reclaim "
                   "would restart the clock the death breaker measures from";

        case CrossingAction::Walk:
            return "the leader is on map " + origin + " with " +
                   std::to_string(step.ashore) + " member(s) already on map " +
                   destination +
                   (step.leaderAtBerth
                        ? "; it is at the berth and waiting for the transport"
                        : "; it is walking to the berth") +
                   ", and " + std::to_string(step.aboard) +
                   " member(s) are already aboard";

        case CrossingAction::Refuse:
            break;
    }

    if (world.originMap == world.destinationMap)
        return "a crossing needs two maps, and this one names map " + origin +
               " twice";
    if (step.offRoute)
        return std::to_string(step.offRoute) +
               " member(s) on neither map " + origin + " nor map " +
               destination + ", and no transport on this route calls there";
    if (!world.transportFound)
        return "no transport is known that serves both map " + origin +
               " and map " + destination +
               " (a crossing transport is one whose own path names both maps)";
    if (!world.berthKnown)
        return "no boardable place on map " + origin +
               " has been established. A transport's stop frame is the SHIP's "
               "mooring, over water beside a pier, not somewhere a character "
               "may stand, and this module will not aim a family at one or "
               "derive a pier from it. There is also no way here to step onto "
               "a deck: upstream boards over the last sixty yards with a "
               "straight-line move that needs a master already aboard, and the "
               "leader has no master" +
               (world.mooringKnown ? " (the mooring itself is known, and is in "
                                     "the log line above this one)"
                                   : "");
    if (!world.landingKnown)
        return "no place on map " + destination +
               " has been established to walk ashore onto, and a crossing that "
               "cannot end is not one to start";
    if (world.overdue)
        return "this crossing has been under way too long and is given up on. "
               "The death breaker declines to call off an errand a run owns, on "
               "the grounds that the run answers for it, so a crossing without a "
               "backstop is an errand nothing can ever stop";
    if (world.berthGuarded)
        return "the berth on map " + origin +
               " stands in hostile ground, up to level " +
               std::to_string(world.berthGuardLevel) +
               ", and a destination the party cannot survive is not a "
               "destination however correct its coordinates are";
    if (!step.leaderOnOrigin)
        return "the leader is not on map " + origin + " with the " +
               std::to_string(step.waiting) +
               " member(s) still waiting there, and this crossing only ever "
               "aims the leader, so there is nothing here to aim";
    return "the crossing is refused";
}

namespace
{

// Does this relation list name that faction? Both lists are searched only
// under the caller's `other.faction` guard, which is the core's own and is
// what keeps the DBC's trailing zero padding from matching a faction of zero.
bool ListNames(std::vector<uint32_t> const& list, uint32_t faction)
{
    for (uint32_t entry : list)
        if (entry == faction)
            return true;
    return false;
}

// FACTION_TEMPLATE_FLAG_HATES_ALL_EXCEPT_FRIENDS (DBCEnums.h:332). Named here
// rather than included, because including DBCEnums.h is exactly the thing this
// file may not do.
constexpr uint32_t FACTION_TEMPLATE_HATES_ALL_EXCEPT_FRIENDS = 0x2000;

}  // namespace

bool FactionStanceHostileTo(FactionStance const& subject, FactionStance const& other)
{
    // ZERO IS NOT A FACTION, and this guard is why. The DBC pads both relation
    // lists to four entries with zeros, so a nameless side searched against
    // them would match the padding and read as an enemy of everything. The
    // core writes the same guard for the same reason.
    if (other.faction)
    {
        if (ListNames(subject.enemyFactions, other.faction))
            return true;
        if (ListNames(subject.friendFactions, other.faction))
            return false;
    }
    return (subject.hostileMask & other.ourMask) != 0;
}

bool FactionStanceFriendlyTo(FactionStance const& subject, FactionStance const& other)
{
    // The core's own first line, and it is not redundant with the masks below:
    // a template whose faction is its own faction is friendly to itself even
    // when its masks say nothing.
    if (subject.faction == other.faction)
        return true;

    if (other.faction)
    {
        if (ListNames(subject.enemyFactions, other.faction))
            return false;
        if (ListNames(subject.friendFactions, other.faction))
            return true;
    }
    return (subject.friendlyMask & other.ourMask) != 0 ||
           (subject.ourMask & other.friendlyMask) != 0;
}

Reaction FactionStanceReaction(FactionStance const& npc, FactionStance const& character)
{
    if (FactionStanceHostileTo(npc, character))
        return Reaction::Hostile;
    if (FactionStanceFriendlyTo(npc, character))
        return Reaction::Friendly;
    // BOTH DIRECTIONS ARE ASKED, and the second one is not a typo in the core.
    // A goblin town's template names no friends and no masks at all, so it is
    // friendly to nobody by its own reading; what keeps a neutral shop usable
    // is the fall-through below, not this line. The line matters for a
    // template the CHARACTER's side declares friendly.
    if (FactionStanceFriendlyTo(character, npc))
        return Reaction::Friendly;
    if (npc.flags & FACTION_TEMPLATE_HATES_ALL_EXCEPT_FRIENDS)
        return Reaction::Hostile;
    return Reaction::Neutral;
}

bool MayInteractAt(Reaction reaction)
{
    return static_cast<int>(reaction) > static_cast<int>(Reaction::Unfriendly);
}

RouteSampling PlanRouteSamples(float spanYards, float maxSpacingYards, float maxSpanYards)
{
    RouteSampling sampling;
    if (!(spanYards > 0.f) || !(maxSpacingYards > 0.f) || spanYards < maxSpacingYards)
        return sampling;

    float const read = (maxSpanYards > 0.f && spanYards > maxSpanYards) ? maxSpanYards : spanYards;

    // THE DIVISION IS DONE IN DOUBLE ON PURPOSE. In float, 6000 / 30 can land a
    // hair above 200 and a ceiling would then ask for 201 samples to cover a
    // length 200 already covers exactly. The two values either side of this are
    // both exactly representable, so in double the quotient of an exact
    // multiple is exactly the integer and the ceiling does not move it. This is
    // the boundary TheExactMultipleNeverLostASample pins.
    double const wanted = double(read) / double(maxSpacingYards);
    std::size_t count = std::size_t(wanted);
    if (double(count) < wanted)
        ++count;          // round UP; see the header for why the direction matters
    if (!count)
        count = 1;

    sampling.samples = count;
    sampling.spacingYards = read / float(count);
    sampling.readYards = read;
    return sampling;
}

float RouteSampleAt(RouteSampling const& sampling, std::size_t index)
{
    // The MIDDLE of the index-th spacing, which is what makes `samples`
    // spacings add up to `readYards` with nothing unspoken for at either end
    // and no sample standing past the destination.
    return (float(index) + 0.5f) * sampling.spacingYards;
}

RouteVerdict JudgeRoute(RouteReading const& reading, RouteLimits const& limits)
{
    RouteVerdict verdict;
    // NOT MEASURED IS NOT SAFE, IT IS UNCLAIMED. Both of these are what a
    // caller that decided the route was not worth reading hands in - a
    // candidate it never shortlisted, or one whose line is shorter than a
    // single sample - and neither is evidence about the ground.
    if (reading.worstLevelAtSample.empty() || reading.sampleSpacingYards <= 0.f)
        return verdict;

    uint32_t const unknownAt = reading.characterLevel + limits.unknownLevelDiff;
    std::size_t lethal = 0;
    std::size_t run = 0;
    std::size_t longest = 0;
    for (uint32_t level : reading.worstLevelAtSample)
    {
        if (level > verdict.worstLevel)
            verdict.worstLevel = level;
        if (level < unknownAt)
        {
            run = 0;
            continue;
        }
        ++lethal;
        ++run;
        if (run > longest)
            longest = run;
    }

    verdict.lethalYards = float(lethal) * reading.sampleSpacingYards;
    verdict.longestLethalRunYards = float(longest) * reading.sampleSpacingYards;
    // THE UNBROKEN STRETCH IS THE MEASURE, NOT THE TOTAL, and the difference is
    // the whole judgement. A party can be chased past one camp; it cannot run
    // six hundred yards through ground where everything reads `??`. Two hundred
    // yards of trouble in ten separate twenty-yard pieces is an ordinary walk
    // through a contested zone, and refusing that would strand a family
    // anywhere worth being. The total is carried anyway, because it is what an
    // operator wants beside the refusal.
    verdict.survivable = verdict.longestLethalRunYards <= limits.lethalRunYards;
    return verdict;
}

TravelTargetChoice ChooseTravelTarget(std::vector<TravelTargetCandidate> const& candidates)
{
    TravelTargetChoice choice;
    choice.considered = candidates.size();
    if (candidates.empty())
        return choice;

    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        TravelTargetCandidate const& candidate = candidates[i];
        if (!candidate.mayInteract)
        {
            ++choice.refused;
            if (choice.nearestRefused < 0 ||
                candidate.distance < candidates[std::size_t(choice.nearestRefused)].distance)
                choice.nearestRefused = int(i);
            continue;
        }
        // The second gate (#267). A spawn standing in ground this character
        // cannot survive is out for the same reason an unfriendly one is: the
        // errand cannot end there, and the walk is what kills people.
        if (candidate.guardCount)
        {
            ++choice.guarded;
            if (choice.nearestGuarded < 0 ||
                candidate.distance < candidates[std::size_t(choice.nearestGuarded)].distance)
                choice.nearestGuarded = int(i);
            continue;
        }
        // The third gate, and it is asked LAST for the same reason the second
        // is asked after the first: it is the most expensive question, so it is
        // never asked about a spawn that is already out. A destination this
        // character can use and can stand at is still not an answer when the
        // ground between here and it is above it - which is the half #267 named
        // and scoped out, and the half that has been killing the family since.
        if (!candidate.routeSurvivable)
        {
            ++choice.lethalRoutes;
            if (choice.nearestLethalRoute < 0 ||
                candidate.distance < candidates[std::size_t(choice.nearestLethalRoute)].distance)
                choice.nearestLethalRoute = int(i);
            continue;
        }
        if (choice.index < 0 ||
            candidate.distance < candidates[std::size_t(choice.index)].distance)
            choice.index = int(i);
    }

    if (choice.index >= 0)
        choice.verdict = TravelTargetVerdict::Chosen;
    // GUARDED OUTRANKS A LETHAL ROUTE, and the order is the same argument the two
    // below it settle. A guarded destination is a place that will kill this
    // character when it ARRIVES, which is a sharper fact and a smaller fix than
    // ground it has to cross to get anywhere at all; and the two counts cannot
    // overlap, because a candidate refused for its guards is never given a
    // route to read.
    else if (choice.guarded)
        choice.verdict = TravelTargetVerdict::EveryOneIsGuarded;
    else if (choice.lethalRoutes)
        choice.verdict = TravelTargetVerdict::EveryRouteIsLethal;
    else
        choice.verdict = TravelTargetVerdict::NoneWillDealWithUs;
    return choice;
}

std::string TravelTargetExplanation(TravelTargetChoice const& choice,
                                    std::vector<TravelTargetCandidate> const& candidates)
{
    // Distances are said as whole yards. A tenth of a yard changes nothing
    // about the decision and a log line full of float noise is harder to
    // compare between two polls than one that is not.
    auto const yards = [](float distance)
    {
        long rounded = long(distance < 0.f ? 0.f : distance + 0.5f);
        return std::to_string(rounded);
    };
    auto const nameOf = [&](int index)
    {
        TravelTargetCandidate const& candidate = candidates[std::size_t(index)];
        return "entry " + std::to_string(candidate.entry) + " at " +
               yards(candidate.distance) + " yards";
    };
    // A guarded spawn is named with its guard, because "entry 14964 at 498
    // yards" and "entry 14964 at 498 yards, guarded by 10 hostile spawn(s) up
    // to level 65" send an operator to two different places.
    auto const guardedNameOf = [&](int index)
    {
        TravelTargetCandidate const& candidate = candidates[std::size_t(index)];
        return nameOf(index) + ", guarded by " + std::to_string(candidate.guardCount) +
               " hostile spawn(s) up to level " + std::to_string(candidate.guardLevel);
    };

    // One refused for its route is named with the ground on the way, because "entry
    // 3495 at 2013 yards" and "entry 3495 at 2013 yards, across 598 yards of
    // unbroken ground held to level 65" are the difference between an errand
    // that looks arbitrary and one an operator can act on.
    auto const routeNameOf = [&](int index)
    {
        TravelTargetCandidate const& candidate = candidates[std::size_t(index)];
        return nameOf(index) + ", across " + yards(candidate.routeRunYards) +
               " yards of unbroken ground held to level " +
               std::to_string(candidate.routeLevel);
    };

    bool const haveRefused = choice.nearestRefused >= 0 &&
                             std::size_t(choice.nearestRefused) < candidates.size();
    bool const haveGuarded = choice.nearestGuarded >= 0 &&
                             std::size_t(choice.nearestGuarded) < candidates.size();
    bool const haveLethalRoute = choice.nearestLethalRoute >= 0 &&
                                 std::size_t(choice.nearestLethalRoute) < candidates.size();

    if (choice.verdict == TravelTargetVerdict::NoneWillDealWithUs)
    {
        std::string said = std::to_string(choice.refused) +
                           " of them are on this map and this character may interact with "
                           "none of them";
        if (haveRefused)
            said += " - the nearest is " + nameOf(choice.nearestRefused);
        return said;
    }

    // A THIRD FACT, NOT A SHADE OF THE SECOND (#267). "There are none here",
    // "there are some and none will serve you" and "there are some that would
    // serve you and every one stands in hostile ground" have three different
    // answers: aim somewhere else, aim at a different faction's town, and send
    // an escort or pick a different shop. Folding the third into the second is
    // what let a fatal destination read as a missing one.
    if (choice.verdict == TravelTargetVerdict::EveryOneIsGuarded)
    {
        std::string said = std::to_string(choice.guarded) +
                           " of them on this map are ones this character may use and every "
                           "one stands in hostile ground";
        if (haveGuarded)
            said += " - the nearest is " + guardedNameOf(choice.nearestGuarded);
        return said;
    }

    // A FOURTH FACT, FOR THE SAME REASON THERE WERE THREE. "Every one of them
    // stands in hostile ground" and "every one of them is across it" are
    // answered differently: the first says pick a different shop, the second
    // says this party is standing somewhere it cannot leave on foot, and only
    // one of those is about the shops.
    if (choice.verdict == TravelTargetVerdict::EveryRouteIsLethal)
    {
        std::string said = std::to_string(choice.lethalRoutes) +
                           " of them on this map are ones this character may use and stand "
                           "in safe ground, and the walk to every one of them crosses ground "
                           "it cannot survive";
        if (haveLethalRoute)
            said += " - the nearest is " + routeNameOf(choice.nearestLethalRoute);
        return said;
    }

    if (choice.verdict != TravelTargetVerdict::Chosen)
        return {};
    if (std::size_t(choice.index) >= candidates.size())
        return {};

    // ONLY WORTH SAYING WHEN THE GATE CHANGED THE ANSWER. A refused spawn
    // farther away than the chosen one cost nobody a walk, and counting it
    // would make an ordinary errand read like a near miss. So the number said
    // is how many were passed over, not how many were refused anywhere on the
    // map - the first is what this errand did and the second is a property of
    // the world.
    float const chosen = candidates[std::size_t(choice.index)].distance;
    std::size_t nearer = 0;
    std::size_t nearerGuarded = 0;
    std::size_t nearerLethalRoute = 0;
    for (TravelTargetCandidate const& candidate : candidates)
    {
        if (candidate.distance >= chosen)
            continue;
        if (!candidate.mayInteract)
            ++nearer;
        else if (candidate.guardCount)
            ++nearerGuarded;
        else if (!candidate.routeSurvivable)
            ++nearerLethalRoute;
    }
    if (!nearer && !nearerGuarded && !nearerLethalRoute)
        return {};

    // The two halves are said separately and only when each one cost a walk,
    // so a line that appears is always a line about this errand. A party that
    // walked past a shop it may not use and a party that walked past one it
    // would have died at are looking at different problems.
    std::string said = "chose " + nameOf(choice.index);
    if (nearer)
    {
        said += " over " + std::to_string(nearer) +
                " nearer one(s) this character may not interact with";
        if (haveRefused)
            said += " - the nearest of those is " + nameOf(choice.nearestRefused);
    }
    if (nearerGuarded)
    {
        said += nearer ? "; and over " : " over ";
        said += std::to_string(nearerGuarded) +
                " nearer one(s) standing in hostile ground";
        if (haveGuarded)
            said += " - the nearest of those is " + guardedNameOf(choice.nearestGuarded);
    }
    if (nearerLethalRoute)
    {
        said += (nearer || nearerGuarded) ? "; and over " : " over ";
        said += std::to_string(nearerLethalRoute) +
                " nearer one(s) it cannot reach alive";
        if (haveLethalRoute)
            said += " - the nearest of those is " + routeNameOf(choice.nearestLethalRoute);
    }
    return said;
}


char const* KillerKindName(KillerKind kind)
{
    switch (kind)
    {
        case KillerKind::Unattributed:  return "environment";
        case KillerKind::Creature:      return "creature";
        case KillerKind::Player:        return "player";
        case KillerKind::SelfInflicted: return "self";
    }
    return "environment";
}

KillerKind NameTheKiller(bool hookFired, std::string const& hookType,
                         std::string const& killerName,
                         std::string const& victimName)
{
    if (!hookFired)
        return KillerKind::Unattributed;

    std::string const type = Normalized(hookType);

    // A creature killer is taken at its word. Unit::Kill reaches the creature
    // hook only from the branch where the killer is not a Player at all, so
    // there is no self-damage case hiding in it.
    if (type == "creature")
        return KillerKind::Creature;

    if (type != "player")
        return KillerKind::Unattributed;

    // The whole point. An empty victim name cannot be matched against, and
    // saying "another player" on the strength of a blank would be the guess
    // this function exists to refuse - so an unnameable victim reads as
    // unattributed rather than as a PvP kill.
    std::string const victim = Normalized(victimName);
    if (victim.empty())
        return KillerKind::Unattributed;

    return Normalized(killerName) == victim ? KillerKind::SelfInflicted
                                            : KillerKind::Player;
}

void FallBaselineHandedOver(FallBaselineState& state, float z, time_t now)
{
    state.held = true;
    state.z = z;
    state.at = now;
}

bool FallBaselineMayInspect(bool alive, bool teleporting, bool inFlight,
                            bool inWater, bool onTransport, bool inVehicle)
{
    // A dead character cannot be charged for a fall, a character mid teleport
    // has no settled position to write down, and a taxi, a boat, a vehicle or
    // a swimmer is somewhere this module has no opinion about. None of these
    // is a flag that has been observed lying.
    return alive && !teleporting && !inFlight && !inWater && !onTransport &&
           !inVehicle;
}

FallBaselineVerdict FallBaselineStep(FallBaselineState& state, bool mayInspect,
                                     bool coreWouldNotCharge, float standingZ,
                                     time_t now, FallBaselineLimits const& limits)
{
    // THE MEASUREMENT IS TAKEN WHATEVER HAPPENS NEXT, and before any early
    // return, because a fall that spans several polls has to stay measurable
    // across all of them. Forgetting where the character was during the polls
    // this declines would make the second poll of a fall look like the first.
    bool const measurable = state.seen && now > state.seenAt;
    float const dropped = measurable ? state.lastSeenZ - standingZ : 0.f;
    float const seconds =
        measurable ? static_cast<float>(now - state.seenAt) : 0.f;

    bool const positionKnown = mayInspect;
    if (positionKnown)
    {
        state.seen = true;
        state.lastSeenZ = standingZ;
        state.seenAt = now;
    }
    else
    {
        // The next poll must not measure a rate across a gap it did not watch:
        // a character that spent ten seconds on a boat has not fallen the
        // difference. Forgetting the reference is the honest answer.
        //
        // AND FORGET THE POSITION TOO, not only the fact of having one. `seen`
        // alone is enough for the rule as written, and leaving a real height
        // behind it is a loaded gun for the next reader: a stale pair is the
        // one input that makes this rule lie, and it lies by inventing a fall
        // and standing the guard down, which is the failure the guard exists
        // to fix. The sentinel reads as an enormous climb instead, which is
        // not a fall, so the worst a mistake here can do is let the guard run.
        state.seen = false;
        state.lastSeenZ = FALL_BASELINE_NO_POSITION;
        state.seenAt = 0;
    }

    // NOTHING TO SAY. Kept rather than forgotten, so the poll after a landing
    // resumes with the character's own history intact.
    if (!mayInspect)
        return FallBaselineVerdict{};

    // THE CORE'S OWN GATE, ASKED THE WAY THE CORE ASKS IT. Player::HandleFall
    // declines to charge on HasHoverAura, HasFeatherFallAura or HasFlyAura and
    // on nothing else about how the character is moving. When it will not
    // charge, there is no stale baseline to be afraid of and no reason to
    // write one.
    if (coreWouldNotCharge)
        return FallBaselineVerdict{};

    // A REAL FALL IS NOT THIS MODULE'S TO ERASE, and this is the whole of what
    // stands between the rule and a roster that cannot be hurt by a drop.
    //
    // MEASURED, NOT ASKED. The flag that used to answer this was on while the
    // character stood still at full health, every poll, on every phantom death
    // sampled (#281). Two positions a second apart cannot be stuck. A body in
    // free fall is losing height faster than a body on its feet can, and the
    // limit sits between the two with margin at both ends - see
    // FallBaselineLimits for the arithmetic.
    //
    // The comparison is written as `dropped > seconds * rate` rather than as a
    // division so that a zero interval cannot divide, and `measurable` has
    // already excluded that case anyway.
    if (measurable && dropped > seconds * limits.fallingYardsPerSecond)
        return FallBaselineVerdict{};

    // Standing, or walking, or climbing. Whatever the core is holding, and
    // whatever wrote it - this module's own lift, an errand's teleport, or a
    // client packet from a hillside the character left four minutes ago - the
    // truth is under this character's feet, and this is the one place that
    // says so.
    state.held = true;
    state.z = standingZ;
    state.at = now;
    return FallBaselineVerdict{true, standingZ};
}

// ------------------------------------------------ the addon language (#269) --

GroupChatRoute GroupChatRouteFor(std::string const& channel)
{
    GroupChatRoute route;
    if (channel == "party")
        route.group = true;
    else if (channel == "raid")
        route.group = route.raid = true;
    else if (channel == "party_addon")
        route.group = route.addon = true;
    else if (channel == "raid_addon")
        route.group = route.raid = route.addon = true;
    return route;
}

// ------------------------------------ an errand that is killing its traveller --

int64_t ErrandDeathWindow(int64_t errandSeconds, ErrandDeathLimits const& limits)
{
    if (errandSeconds <= 0)
        return 0;
    return errandSeconds < limits.windowSeconds ? errandSeconds : limits.windowSeconds;
}

ErrandDeathVerdict ErrandDeathBreaker(ErrandDeathToll const& toll,
                                      ErrandDeathLimits const& limits)
{
    ErrandDeathVerdict verdict;

    bool const coolingOff =
        toll.sinceRefused >= 0 && toll.sinceRefused < limits.cooloffSeconds;

    // 1. A CLAIMED AIM IS NEVER SIMPLY TAKEN OFF ITS CLAIMANT, whichever of
    //    the two things below would otherwise happen. Answered first because it
    //    is the one branch that is about who may act at all rather than about
    //    what the table says, and because a coordinator that re-Claims a target
    //    this rule refused is not a bridge re-arming a bad errand - it is the
    //    run doing the job it was started for, and it would win the argument
    //    every five seconds anyway.
    //
    //    WHICH IS AN ARGUMENT ABOUT BACKSTOPS AND NOT ABOUT RUNS, and it was
    //    written when those were the same thing (#298). A run's claim is left
    //    alone because a timer ends the run. The catch-up walk claims through
    //    the same lease and has no timer at all, so declining to it is
    //    declining to nobody - and its remedy cannot be a release either,
    //    because the walk re-Claims from its own escort entry on the next party
    //    poll. The caller is told to end the WALK, which is the only thing that
    //    stops the aim coming back. See ErrandDeathToll::catchUp.
    if (toll.runOwned)
    {
        if (coolingOff || toll.deaths >= limits.deaths)
            verdict.remedy = toll.catchUp ? ErrandDeathRemedy::EndCatchUp
                                          : ErrandDeathRemedy::DeclineRunOwned;
        return verdict;
    }

    // 2. A RE-ISSUE IS ANSWERED BEFORE THE DEATHS ARE COUNTED, because by then
    //    the count is already wrong. Releasing an errand erases the memory of
    //    it, so the same target re-aimed a poll later arrives looking like a
    //    brand new errand: its window is a second wide and its toll is
    //    therefore zero, forever, however many bodies are behind it. The
    //    refusal is the only thing that still remembers, so it has to be asked
    //    here or it can never be reached at all.
    if (coolingOff)
    {
        verdict.remedy = ErrandDeathRemedy::RefuseReissue;
        verdict.coolOffRemaining = limits.cooloffSeconds - toll.sinceRefused;
        return verdict;
    }

    // 3. The rule itself. `>=` and not `>`: AGENTS.md says "exceed roughly
    //    three in five minutes", and the third death in five minutes IS the
    //    evidence - a rule that waits for a fourth body to be sure is a rule
    //    that costs a body to be sure.
    if (toll.deaths >= limits.deaths)
        verdict.remedy = ErrandDeathRemedy::Release;

    return verdict;
}

// ------------------------------- an errand that is eating the questing --

ErrandSpend ErrandSpendAfter(ErrandSpend const& before, time_t now, int64_t heldSeconds,
                             ErrandBudgetLimits const& limits)
{
    ErrandSpend after;
    after.markedAt = now;
    after.seconds = before.seconds;

    // A WINDOW THAT IS NOT A WINDOW MEASURES NOTHING, and a division by it is
    // worse than the rule being off. Answered before anything divides.
    if (limits.windowSeconds <= 0 || limits.spendSeconds <= 0)
    {
        after.seconds = 0;
        return after;
    }

    // FIRST SIGHT OF THIS CHARACTER IS NOT A GAP TO DRAIN. `markedAt` of zero
    // means this rule has never looked at it, and reading that as "1970" would
    // drain a bucket that is already empty by several decades of credit - and,
    // once the clamp below is reversed, hand out that much spending. So the
    // first mark only starts the clock.
    if (before.markedAt > 0 && now > before.markedAt)
    {
        int64_t const elapsed = static_cast<int64_t>(now - before.markedAt);
        // The drain, and the whole of the rule: `spendSeconds` of allowance per
        // `windowSeconds` of wall clock. Integer division rounds the drain
        // DOWN, which errs towards holding a character off rather than towards
        // letting the loop run, and is the direction to err in for a rule whose
        // failure mode on the other side is the bug it was written for.
        after.seconds -= elapsed * limits.spendSeconds / limits.windowSeconds;
    }

    // ONLY REAL TIME IS ADDED. A negative or zero hold is a caller whose two
    // clock reads came back out of order, not an errand that ran for less than
    // no time, and crediting it would let a bucket be emptied by asking often
    // enough.
    if (heldSeconds > 0)
        after.seconds += heldSeconds;

    if (after.seconds < 0)
        after.seconds = 0;
    // ONE BUCKET OF DEBT, AND NOT A SECOND ONE.
    //
    // Clamping at the line itself looks tidier and quietly hands out time. The
    // rule is asked once per poll, so an errand already running when the budget
    // is reached still costs the rest of that poll; if the total cannot go past
    // the line, that overshoot is discarded instead of repaid, and the
    // character is handed it again on every saturation. Measured over a
    // six-hour replay of the observed loop that leak was 600 seconds, taking a
    // 23.3% allowance out at 26.1%.
    //
    // Letting it run to twice the budget makes the overshoot a debt that the
    // next quiet stretch pays off, which is what makes the share hold. Bounded
    // there rather than unbounded for the reason the clamp existed at all: from
    // twice the budget one window of drain brings it exactly back to the line, so
    // a single bad stretch can never cost more than one window of hold-off.
    if (after.seconds > 2 * limits.spendSeconds)
        after.seconds = 2 * limits.spendSeconds;

    return after;
}

bool ErrandOverspent(ErrandSpend const& spend, ErrandBudgetLimits const& limits)
{
    if (limits.spendSeconds <= 0)
        return false;
    return spend.seconds >= limits.spendSeconds;
}

// ----------------------------------------------------------------- auction --

namespace
{

// A decimal field that fits a uint32, or false. Leading zeros are allowed;
// signs, spaces and anything past 4294967295 are not, because every number in
// this grammar is an id or a copper amount the core reads as uint32, and a
// value that wraps would be a different bid from the one the operator typed.
bool ParseDecimal(std::string const& text, uint32_t& value)
{
    if (text.empty() || text.size() > 10 ||
        text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    unsigned long long parsed = 0;
    for (char c : text)
        parsed = parsed * 10 + static_cast<unsigned long long>(c - '0');
    if (parsed > 4294967295ULL)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

std::vector<std::string> SplitWords(std::string const& text)
{
    std::vector<std::string> words;
    std::string::size_type pos = 0;
    while (pos < text.size())
    {
        std::string::size_type const start = text.find_first_not_of(" \t", pos);
        if (start == std::string::npos)
            break;
        std::string::size_type const end = text.find_first_of(" \t", start);
        words.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        pos = end;
    }
    return words;
}

}  // namespace

AuctionRequest ParseAuctionRequest(std::string const& command)
{
    AuctionRequest request;
    std::vector<std::string> const words = SplitWords(command);
    if (words.empty())
    {
        request.error = AuctionRefusal::Malformed;
        return request;
    }

    AuctionVerb verb = AuctionVerb::None;
    if (words[0] == "list")
        verb = AuctionVerb::List;
    else if (words[0] == "buy")
        verb = AuctionVerb::Buy;
    else if (words[0] == "bid")
        verb = AuctionVerb::Bid;
    else if (words[0] == "cancel")
        verb = AuctionVerb::Cancel;
    else
    {
        request.error = AuctionRefusal::Malformed;
        return request;
    }

    // Which keys each verb takes. Every key is required and none may repeat:
    // a `buy` carrying a bid, or a `list` missing its hours, is a row whose
    // author meant something this executor cannot guess at, so it is refused
    // rather than filled in.
    bool sawGuid = false, sawAuction = false, sawBid = false, sawBuyout = false, sawHours = false;
    for (std::size_t i = 1; i < words.size(); ++i)
    {
        std::string const& word = words[i];
        std::string::size_type const colon = word.find(':');
        if (colon == std::string::npos)
        {
            request.error = AuctionRefusal::Malformed;
            return request;
        }
        std::string const key = word.substr(0, colon);
        uint32_t value = 0;
        if (!ParseDecimal(word.substr(colon + 1), value))
        {
            request.error = AuctionRefusal::Malformed;
            return request;
        }

        bool* seen = nullptr;
        uint32_t* into = nullptr;
        if (key == "guid" && verb == AuctionVerb::List)
        {
            seen = &sawGuid;
            into = &request.itemGuid;
        }
        else if (key == "auction" && verb != AuctionVerb::List)
        {
            seen = &sawAuction;
            into = &request.auctionId;
        }
        else if (key == "bid" && (verb == AuctionVerb::List || verb == AuctionVerb::Bid))
        {
            seen = &sawBid;
            into = &request.bid;
        }
        else if (key == "buyout" && verb == AuctionVerb::List)
        {
            seen = &sawBuyout;
            into = &request.buyout;
        }
        else if (key == "hours" && verb == AuctionVerb::List)
        {
            seen = &sawHours;
            into = &request.hours;
        }
        else
        {
            request.error = AuctionRefusal::Malformed;
            return request;
        }

        if (*seen)
        {
            request.error = AuctionRefusal::Malformed;
            return request;
        }
        *seen = true;
        *into = value;
    }

    bool complete = false;
    switch (verb)
    {
        case AuctionVerb::List:
            complete = sawGuid && sawBid && sawBuyout && sawHours;
            break;
        case AuctionVerb::Buy:
        case AuctionVerb::Cancel:
            complete = sawAuction;
            break;
        case AuctionVerb::Bid:
            complete = sawAuction && sawBid;
            break;
        case AuctionVerb::None:
            break;
    }
    if (!complete)
    {
        request.error = AuctionRefusal::Malformed;
        return request;
    }

    // Ids are never zero: the core reads a zero item guid or auction id as a
    // malformed packet and returns silently (AuctionHouseHandler.cpp:147,
    // :433), so it is refused by name here instead.
    if ((verb == AuctionVerb::List && request.itemGuid == 0) ||
        (verb != AuctionVerb::List && request.auctionId == 0))
    {
        request.error = AuctionRefusal::Malformed;
        return request;
    }

    // The rules a client enforces in its own window before the packet is
    // ever built, named one at a time so the row says which one.
    if (verb == AuctionVerb::List)
    {
        if (request.bid == 0)
        {
            request.error = AuctionRefusal::NoStartBid;
            return request;
        }
        if (request.buyout != 0 && request.buyout < request.bid)
        {
            request.error = AuctionRefusal::BuyoutBelowBid;
            return request;
        }
        if (AuctionDurationMinutes(request.hours) == 0)
        {
            request.error = AuctionRefusal::InvalidDuration;
            return request;
        }
    }
    if (verb == AuctionVerb::Bid && request.bid == 0)
    {
        request.error = AuctionRefusal::ZeroBid;
        return request;
    }

    request.verb = verb;
    return request;
}

uint32_t AuctionDurationMinutes(uint32_t hours)
{
    switch (hours)
    {
        case 12:
        case 24:
        case 48:
            return hours * 60;
        default:
            return 0;
    }
}

AuctionBidVerdict AuctionBidAcceptable(uint32_t price, uint32_t startBid,
                                       uint32_t currentBid, uint32_t buyout,
                                       uint32_t outbidStep)
{
    // AuctionHouseHandler.cpp:488: `price <= auction->bid || price < auction->startbid`
    if (price <= currentBid || price < startBid)
        return AuctionBidVerdict::NotAboveCurrent;

    // AuctionHouseHandler.cpp:492-493: a bid that is not a buyout must clear
    // the standing bid by the outbid step. The addition is done in 64 bits so
    // a standing bid near the cap cannot wrap into a lower threshold.
    bool const isBuyout = buyout != 0 && price >= buyout;
    if (!isBuyout)
    {
        uint64_t const threshold = static_cast<uint64_t>(currentBid) + outbidStep;
        if (static_cast<uint64_t>(price) < threshold)
            return AuctionBidVerdict::BelowIncrement;
    }
    return AuctionBidVerdict::Ok;
}

uint32_t AuctionBidCost(uint32_t price, uint32_t currentBid, bool alreadyTopBidder)
{
    if (alreadyTopBidder && price > currentBid)
        return price - currentBid;
    return price;
}

bool AuctionRefusalRetryable(std::string const& reason)
{
    static char const* const retryable[] = {
        AuctionRefusal::NotInRange,
        AuctionRefusal::Dead,
        AuctionRefusal::InCombat,
        AuctionRefusal::Trading,
        AuctionRefusal::InFlight,
        AuctionRefusal::Stunned,
        AuctionRefusal::LoggingOut,
        AuctionRefusal::CannotAffordDeposit,
        AuctionRefusal::CannotAffordBuyout,
        AuctionRefusal::CannotAffordBid,
        AuctionRefusal::CannotAffordCut,
    };
    for (char const* candidate : retryable)
        if (reason == candidate)
            return true;
    return false;
}

// ----------------------------------------- a home somebody chose (#274) --

BindRequest ParseBindRequest(std::string const& command)
{
    BindRequest request;
    std::vector<std::string> const words = TownWords(command);

    // An EMPTY command is `here`. A bind has exactly one form and no
    // arguments, so an empty column is unambiguous rather than lazy, and the
    // sender that writes the row does not have to know a magic word to ask for
    // the only thing this verb does.
    if (words.empty())
    {
        request.verb = BindVerb::Here;
        return request;
    }

    if (words[0] == "here")
    {
        if (words.size() == 1)
        {
            request.verb = BindVerb::Here;
            return request;
        }
        request.error = "malformed bind: here takes no arguments";
        return request;
    }

    request.error = "malformed bind: unknown verb (want here, or nothing at all)";
    return request;
}

char const* BindOutcomeWord(BindOutcome outcome)
{
    switch (outcome)
    {
        case BindOutcome::Moved:
            return "moved";
        case BindOutcome::SameSpot:
            return "same-spot";
        case BindOutcome::Unchanged:
            return "unchanged";
        case BindOutcome::Unreadable:
            break;
    }
    return "unreadable";
}

BindOutcome BindReadBack(HomeBind const& before, HomeBind const& after,
                         HomeBind const& standing, float sameSpotYards)
{
    // ALL THREE OR NOTHING. A home read after but not before cannot be
    // compared, and a home compared against a place nobody read cannot say
    // whether standing still was the right answer. Reporting either as a
    // change or as a failure would be inventing the half that was missing.
    if (!before.known || !after.known || !standing.known)
        return BindOutcome::Unreadable;

    // Squared throughout, so these two files keep including nothing but the
    // five standard headers the decisions job compiles them with.
    float const tolerance = sameSpotYards * sameSpotYards;

    auto within = [tolerance](HomeBind const& a, HomeBind const& b)
    {
        if (a.mapId != b.mapId)
            return false;
        float const dx = a.x - b.x;
        float const dy = a.y - b.y;
        float const dz = a.z - b.z;  // an inn has floors
        return dx * dx + dy * dy + dz * dz <= tolerance;
    };

    if (!within(before, after))
        return BindOutcome::Moved;

    // The home did not move. That is good news only if it was already here.
    return within(after, standing) ? BindOutcome::SameSpot : BindOutcome::Unchanged;
}

int ChooseInnkeeper(std::vector<float> const& yards)
{
    int best = -1;
    for (size_t i = 0; i < yards.size(); ++i)
    {
        if (best < 0 || yards[i] < yards[static_cast<size_t>(best)])
            best = static_cast<int>(i);
    }
    return best;
}

TownRetry BindRefusalRetry(std::string const& detail)
{
    // The literals mod_overseer.cpp's DoBind returns, grouped by what would
    // have to change for the same row to succeed.
    static char const* const NEVER[] = {
        "malformed bind: here takes no arguments",
        "malformed bind: unknown verb (want here, or nothing at all)",
        "malformed bind request",
    };
    static char const* const ELSEWHERE[] = {
        // Standing somewhere else answers both of these. The second is not a
        // guess: WorldSession::SendBindPoint returns without doing anything at
        // all when the character's map is instanceable, which is precisely a
        // call that reports nothing and changes nothing, so it is refused on
        // this side before the packet rather than read back as a mystery.
        "innkeeper not in range",
        "character is inside an instance",
    };

    for (char const* literal : NEVER)
        if (detail == literal)
            return TownRetry::Never;
    for (char const* literal : ELSEWHERE)
        if (detail == literal)
            return TownRetry::Elsewhere;
    return TownRetry::Later;
}

// ------------------- a home the campaign's own dungeon can be reached from --

char const* CampaignHomeName(CampaignHome verdict)
{
    switch (verdict)
    {
        case CampaignHome::NotAsked:
            return "not-asked";
        case CampaignHome::Suits:
            return "suits";
        case CampaignHome::OffTheDungeonsMap:
            return "off-the-dungeons-map";
        case CampaignHome::TooFarFromTheTown:
            return "too-far";
        case CampaignHome::Unreadable:
            break;
    }
    return "unreadable";
}

CampaignHome ReadCampaignHome(HomeBind const& home, CampaignHomeAnchor const& anchor,
                              float townYards)
{
    // THE CAMPAIGN IS ASKED FIRST. A door that names no town has no opinion
    // about anybody's home, and answering "unreadable" for a character nobody
    // was judging would be a complaint about a reading nobody wanted.
    if (!anchor.known)
        return CampaignHome::NotAsked;

    if (!home.known)
        return CampaignHome::Unreadable;

    // THE MAP IS THE FATAL HALF AND IS ANSWERED ON ITS OWN. A home on another
    // continent is not "far": it is out of reach of everything this module has,
    // and folding it into a distance would mean subtracting two coordinate
    // systems and reporting the result as yards, which is the non-reading #241
    // was caught acting on.
    if (home.mapId != anchor.mapId)
        return CampaignHome::OffTheDungeonsMap;

    // Squared, so these two files keep including nothing but the five standard
    // headers the decisions job compiles them with.
    float const dx = home.x - anchor.x;
    float const dy = home.y - anchor.y;
    return dx * dx + dy * dy > townYards * townYards ? CampaignHome::TooFarFromTheTown
                                                     : CampaignHome::Suits;
}

bool CampaignHomeNeedsRebinding(CampaignHome verdict)
{
    switch (verdict)
    {
        case CampaignHome::OffTheDungeonsMap:
        case CampaignHome::TooFarFromTheTown:
            return true;
        // NAMED RATHER THAN DEFAULTED, all three of them. A verdict added to
        // this enum later has to be a compile error here rather than silently
        // joining the side that walks a character across a continent.
        case CampaignHome::Unreadable:
        case CampaignHome::NotAsked:
        case CampaignHome::Suits:
            break;
    }
    return false;
}

uint16_t FallGuardStandDownMask(bool alive, bool teleporting, bool inFlight,
                               bool flying, bool falling, bool inWater,
                               bool onTransport, bool inVehicle)
{
    uint16_t mask = FALL_GUARD_RAN;

    // NO `else`, ANYWHERE. Two of these being true at once is the finding, not
    // a tie to be broken: a character whose flags say both falling and flying
    // is in a different state from one whose flags say only one of them, and a
    // rule that reported whichever was tested first would erase the difference
    // it exists to show.
    if (!alive)
        mask |= FALL_GUARD_DEAD;
    if (teleporting)
        mask |= FALL_GUARD_TELEPORTING;
    if (inFlight)
        mask |= FALL_GUARD_IN_FLIGHT;
    if (flying)
        mask |= FALL_GUARD_FLYING;
    if (falling)
        mask |= FALL_GUARD_FALLING;
    if (inWater)
        mask |= FALL_GUARD_IN_WATER;
    if (onTransport)
        mask |= FALL_GUARD_TRANSPORT;
    if (inVehicle)
        mask |= FALL_GUARD_VEHICLE;

    return mask;
}

std::string FallGuardStandDownNames(uint16_t mask)
{
    if (mask == FALL_GUARD_RAN)
        return "ran";

    // Lowest bit first, so two rows carrying the same mask always read the
    // same way round and can be compared by eye as well as by value.
    static struct
    {
        uint16_t bit;
        char const* name;
    } const NAMES[] = {
        {FALL_GUARD_DEAD, "dead"},
        {FALL_GUARD_TELEPORTING, "teleporting"},
        {FALL_GUARD_IN_FLIGHT, "in-flight"},
        {FALL_GUARD_FLYING, "flying"},
        {FALL_GUARD_FALLING, "falling"},
        {FALL_GUARD_IN_WATER, "in-water"},
        {FALL_GUARD_TRANSPORT, "transport"},
        {FALL_GUARD_VEHICLE, "vehicle"},
    };

    std::string out;
    for (auto const& entry : NAMES)
    {
        if (!(mask & entry.bit))
            continue;
        if (!out.empty())
            out += '|';
        out += entry.name;
    }

    // A bit this build does not know about is still worth seeing. It cannot
    // happen today, because the mask is built from this file's own enum, and
    // saying so costs one branch and stops a future bit reading as "ran".
    if (out.empty())
        return "unknown";

    return out;
}

// -------------------------------------------------------------------- mail --

namespace
{

// A decimal field that fits a uint32, or false. Leading zeros are allowed;
// signs, spaces and anything past 4294967295 are not, because every number in
// this grammar is an id or a copper amount the core reads as uint32, and a
// value that wraps would be a different sum of money from the one the operator
// wrote down.
//
// Named for mail rather than shared: this file is appended to by one executor
// at a time in parallel branches, and two anonymous-namespace helpers with the
// same name in the same translation unit is a merge that does not compile.
bool MailParseDecimal(std::string const& text, uint32_t& value)
{
    if (text.empty() || text.size() > 10 ||
        text.find_first_not_of("0123456789") != std::string::npos)
        return false;
    unsigned long long parsed = 0;
    for (char c : text)
        parsed = parsed * 10 + static_cast<unsigned long long>(c - '0');
    if (parsed > 4294967295ULL)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

std::vector<std::string> MailSplitWords(std::string const& text)
{
    std::vector<std::string> words;
    std::string::size_type pos = 0;
    while (pos < text.size())
    {
        std::string::size_type const start = text.find_first_not_of(" \t", pos);
        if (start == std::string::npos)
            break;
        std::string::size_type const end = text.find_first_of(" \t", start);
        words.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
            break;
        pos = end;
    }
    return words;
}

std::string MailTrim(std::string const& text)
{
    std::string::size_type const start = text.find_first_not_of(" \t");
    if (start == std::string::npos)
        return std::string();
    std::string::size_type const end = text.find_last_not_of(" \t");
    return text.substr(start, end - start + 1);
}

// The one sequence HandleSendMail turns a letter away for without a word
// (MailHandler.cpp:76-80): "| |" makes the 3.3.5a client crash when it renders
// the mailbox list, so the core drops the packet on the floor rather than store
// it. Checked on both fields here. The core's own check reads `body` before
// `body` has been read off the wire, so upstream it only ever catches the
// subject; a body carrying it would be stored and would then be the thing that
// crashes a client. Both are checked here on purpose, and that is a deliberate
// difference from the handler rather than a copy of it.
bool MailTextIsRenderable(std::string const& text)
{
    return text.find("| |") == std::string::npos;
}

}  // namespace

MailRequest ParseMailRequest(std::string const& command)
{
    MailRequest request;

    // THE TEXT TAIL IS CUT OFF FIRST, before anything is split on spaces,
    // because a subject has spaces in it and a subject is allowed to contain
    // the word "money:" or a colon of its own. Everything from `subject:` on is
    // text; everything before it is the structured head.
    std::string head = command;
    std::string tail;
    bool haveSubject = false;

    // The first `subject:` that starts a word. Not just find("subject:"), so a
    // subject reading "resubject: no" cannot be split at its own middle.
    for (std::string::size_type at = command.find("subject:");
         at != std::string::npos; at = command.find("subject:", at + 1))
    {
        if (at != 0 && command[at - 1] != ' ' && command[at - 1] != '\t')
            continue;
        head = command.substr(0, at);
        tail = command.substr(at + 8);
        haveSubject = true;
        break;
    }

    std::vector<std::string> const words = MailSplitWords(head);
    if (words.empty())
    {
        request.error = MailRefusal::Malformed;
        return request;
    }

    MailVerb verb = MailVerb::None;
    if (words[0] == "send")
        verb = MailVerb::Send;
    else if (words[0] == "take-item")
        verb = MailVerb::TakeItem;
    else if (words[0] == "take-money")
        verb = MailVerb::TakeMoney;
    else if (words[0] == "return")
        verb = MailVerb::Return;
    else if (words[0] == "delete")
        verb = MailVerb::Delete;
    else
    {
        request.error = MailRefusal::Malformed;
        return request;
    }

    // Which keys each verb takes. None may repeat and none may belong to
    // another verb: a `delete` carrying an item, or a `take-item` short of its
    // item, is a row whose author meant something this executor cannot guess
    // at, so it is refused rather than filled in.
    bool sawMail = false, sawItem = false, sawMoney = false;
    for (std::size_t i = 1; i < words.size(); ++i)
    {
        std::string const& word = words[i];
        std::string::size_type const colon = word.find(':');
        if (colon == std::string::npos)
        {
            request.error = MailRefusal::Malformed;
            return request;
        }
        std::string const key = word.substr(0, colon);

        // Named before the number is parsed, so `cod:500` is answered with the
        // reason it is refused and not with "malformed". This executor does not
        // send cash on delivery and says so; see the migration for why.
        if (key == "cod")
        {
            request.error = MailRefusal::CodNotSupported;
            return request;
        }

        uint32_t value = 0;
        if (!MailParseDecimal(word.substr(colon + 1), value))
        {
            request.error = MailRefusal::Malformed;
            return request;
        }

        bool* seen = nullptr;
        uint32_t* into = nullptr;
        if (key == "mail" && verb != MailVerb::Send)
        {
            seen = &sawMail;
            into = &request.mailId;
        }
        else if (key == "item" && (verb == MailVerb::Send || verb == MailVerb::TakeItem))
        {
            seen = &sawItem;
            into = &request.itemGuid;
        }
        else if (key == "money" && verb == MailVerb::Send)
        {
            seen = &sawMoney;
            into = &request.money;
        }
        else
        {
            request.error = MailRefusal::Malformed;
            return request;
        }

        if (*seen)
        {
            request.error = MailRefusal::Malformed;
            return request;
        }
        *seen = true;
        *into = value;
    }

    bool complete = false;
    switch (verb)
    {
        case MailVerb::Send:
            // item and money are both optional; a letter with neither is a
            // letter, which is one of the things the operator asked for.
            complete = haveSubject;
            break;
        case MailVerb::TakeItem:
            complete = sawMail && sawItem;
            break;
        case MailVerb::TakeMoney:
        case MailVerb::Return:
        case MailVerb::Delete:
            complete = sawMail;
            break;
        case MailVerb::None:
            break;
    }
    if (!complete)
    {
        request.error = MailRefusal::Malformed;
        return request;
    }

    // Ids are never zero. The core reads a zero item guid as an invalid
    // attachment (MailHandler.cpp:233-238) and a mail id that matches nothing
    // as an internal error, both of which it answers only with a packet the bot
    // will never see, so they are refused by name here instead.
    if (verb != MailVerb::Send && request.mailId == 0)
    {
        request.error = MailRefusal::Malformed;
        return request;
    }
    if (sawItem && request.itemGuid == 0)
    {
        request.error = MailRefusal::Malformed;
        return request;
    }

    request.hasItem = sawItem;
    request.hasMoney = sawMoney;

    if (verb == MailVerb::Send)
    {
        // `money:0` is not "no money", it is a row that meant to enclose
        // something and did not. Omitting the key is how a letter carries no
        // money, and the two are told apart rather than treated alike.
        if (sawMoney && request.money == 0)
        {
            request.error = MailRefusal::ZeroMoney;
            return request;
        }

        // Split the tail: subject runs to the first " body:" after it, body
        // runs to the end. Stated in the header and pinned by a test, because
        // there is deliberately no way to escape a literal " body:" inside a
        // subject.
        std::string subject = tail;
        std::string body;
        std::string::size_type const bodyAt = tail.find(" body:");
        if (bodyAt != std::string::npos)
        {
            subject = tail.substr(0, bodyAt);
            body = tail.substr(bodyAt + 6);
        }
        request.subject = MailTrim(subject);
        request.body = MailTrim(body);

        if (request.subject.empty())
        {
            request.error = MailRefusal::NoSubject;
            return request;
        }
        if (request.subject.size() > MAIL_SUBJECT_MAX)
        {
            request.error = MailRefusal::SubjectTooLong;
            return request;
        }
        if (request.body.size() > MAIL_BODY_MAX)
        {
            request.error = MailRefusal::BodyTooLong;
            return request;
        }
        if (!MailTextIsRenderable(request.subject) || !MailTextIsRenderable(request.body))
        {
            request.error = MailRefusal::TextNotRenderable;
            return request;
        }
    }
    else if (haveSubject)
    {
        // Text on a verb that posts nothing. Refused rather than dropped: a row
        // that carried a message nobody will ever read is a row whose author
        // was writing a different command.
        request.error = MailRefusal::Malformed;
        return request;
    }

    request.verb = verb;
    return request;
}

bool MailTotalCost(uint32_t money, bool hasItem, uint32_t& cost)
{
    // MailHandler.cpp:163. The `items_count ? 30 * items_count : 30` is 30
    // either way at one item per letter, and it is written out rather than
    // folded to a constant so the line still reads as the core's line.
    uint64_t const postage = hasItem ? 30ULL * 1ULL : 30ULL;
    uint64_t const total = postage + static_cast<uint64_t>(money);

    // :168-172, the handler's own overflow guard, done in 64 bits so the test
    // is on the real sum rather than on the wrapped one.
    if (total > 4294967295ULL)
        return false;
    cost = static_cast<uint32_t>(total);
    return true;
}

uint32_t MailDeliveryDelaySeconds(bool hasItem, bool sameAccount, uint32_t configuredDelay)
{
    // MailHandler.cpp:350 sets needItemDelay only inside the `items_count > 0`
    // branch, and :362 turns it into the configured delay. Money and text never
    // wait, and nothing waits inside one account.
    if (!hasItem || sameAccount)
        return 0;
    return configuredDelay;
}

bool MailRefusalRetryable(std::string const& reason)
{
    static char const* const retryable[] = {
        MailRefusal::NotInWorld,
        MailRefusal::Dead,
        MailRefusal::InFlight,
        MailRefusal::Stunned,
        MailRefusal::LoggingOut,
        MailRefusal::InCombat,
        MailRefusal::Trading,
        MailRefusal::NoMailbox,
        MailRefusal::RecipientOffline,
        MailRefusal::RecipientFull,
        MailRefusal::CannotAffordPost,
        MailRefusal::MailNotDelivered,
        MailRefusal::NoRoom,
        MailRefusal::TooMuchGold,
    };
    for (char const* candidate : retryable)
        if (reason == candidate)
            return true;
    return false;
}

HearthRequest ParseHearthRequest(std::string const& command)
{
    HearthRequest request;
    std::vector<std::string> const words = TownWords(command);

    // An EMPTY command is `use`, for the reason ParseBindRequest gives for the
    // same shape: this verb has exactly one form and no arguments, so an empty
    // column is unambiguous rather than lazy, and a sender does not have to
    // know a magic word to ask for the only thing the verb does.
    if (words.empty())
    {
        request.verb = HearthVerb::Use;
        return request;
    }

    if (words[0] == "use")
    {
        if (words.size() == 1)
        {
            request.verb = HearthVerb::Use;
            return request;
        }
        request.error = "malformed hearth: use takes no arguments";
        return request;
    }

    request.error = "malformed hearth: unknown verb (want use, or nothing at all)";
    return request;
}

char const* HearthOutcomeWord(HearthOutcome outcome)
{
    switch (outcome)
    {
        case HearthOutcome::Arrived:
            return "arrived";
        case HearthOutcome::Stayed:
            return "stayed";
        case HearthOutcome::Elsewhere:
            return "elsewhere";
        case HearthOutcome::Unreadable:
            break;
    }
    return "unreadable";
}

namespace
{

// Squared throughout, so this file keeps including nothing but the five
// standard headers the decisions job compiles it with. Same rule, and the same
// reason, as BindReadBack above.
bool HearthWithin(HomeBind const& a, HomeBind const& b, float yards)
{
    if (a.mapId != b.mapId)
        return false;
    float const dx = a.x - b.x;
    float const dy = a.y - b.y;
    float const dz = a.z - b.z;  // an inn has floors, and so does a bank
    return dx * dx + dy * dy + dz * dz <= yards * yards;
}

}  // namespace

bool HearthWouldMoveNobody(HomeBind const& standing, HomeBind const& home,
                           float arrivedYards)
{
    // A reading nobody took is not a reason to refuse. The executor asks about
    // the readings themselves before it asks this, and answering `true` here
    // for an unread home would refuse every character whose home could not be
    // read, which is a different refusal wearing this one's words.
    if (!standing.known || !home.known)
        return false;
    return HearthWithin(standing, home, arrivedYards);
}

HearthOutcome HearthReadBack(HomeBind const& from, HomeBind const& home,
                             HomeBind const& now, float arrivedYards,
                             float movedYards)
{
    // ALL THREE OR NOTHING, the rule BindReadBack keeps for the same reason: a
    // verdict built out of two readings and a guess at the third is the half
    // that was missing, invented.
    if (!from.known || !home.known || !now.known)
        return HearthOutcome::Unreadable;

    // AND THE START LINE MUST NOT BE THE FINISH LINE. When the character began
    // the cast at its own home, `Arrived` and `Stayed` are the same reading and
    // this function cannot tell them apart. The executor refuses that row
    // before the packet (HearthWouldMoveNobody), so reaching here means the
    // home moved under a cast already in flight. Saying so is honest; picking
    // one of the two would be a coin toss reported as a measurement.
    if (HearthWithin(from, home, arrivedYards))
        return HearthOutcome::Unreadable;

    // HOME FIRST. It is the only outcome that is the thing that was asked for,
    // and after the guard above it cannot also be the start line.
    if (HearthWithin(now, home, arrivedYards))
        return HearthOutcome::Arrived;

    // Still on the start line. A tolerance of its own, and a tighter one than
    // the arrival's: a character that never left is standing on the spot, and
    // the width that has to absorb a bind point recorded at a doorway is not
    // the width that decides whether somebody moved.
    if (HearthWithin(now, from, movedYards))
        return HearthOutcome::Stayed;

    return HearthOutcome::Elsewhere;
}

uint32_t HearthVerifyWindowMs(uint32_t castMs, uint32_t marginMs, uint32_t floorMs)
{
    // Saturating. A cast time out of the DBC is not this module's number, and
    // an addition that wraps would turn an absurd one into a window shorter
    // than the floor - which is precisely the reading that judges a hearth as
    // `Stayed` while the character is still casting it.
    uint32_t const wanted = castMs > UINT32_MAX - marginMs ? UINT32_MAX : castMs + marginMs;
    return wanted < floorMs ? floorMs : wanted;
}

TownRetry HearthRefusalRetry(std::string const& detail)
{
    // The literals mod_overseer.cpp's DoHearth returns, grouped by what would
    // have to change for the SAME row to succeed.
    static char const* const NEVER[] = {
        "malformed hearth: use takes no arguments",
        "malformed hearth: unknown verb (want use, or nothing at all)",
        "malformed hearth request",
        // The item is the wall, which is what Never means here. Waiting does
        // not put a hearthstone in a bag and neither does walking; somebody has
        // to hand one over or an innkeeper has to replace it, and either way it
        // is not this row that succeeds afterwards.
        "character carries no hearthstone",
        // The item is in the bags and its template carries no ON_USE spell for
        // Player::CastItemUseSpell to find. A world database that says that is
        // not going to say something else while this row waits.
        "the hearthstone has no on-use spell",
        // The same class of answer one step either side of it: an item row with
        // no template at all, and a spell id the core's DBC does not carry.
        // Both are the server's own data being wrong rather than the character
        // being busy, and no amount of waiting or walking changes either. They
        // are named separately from the one above because they fail at
        // different points and an operator should not have to guess which.
        "the hearthstone has no template",
        "the core does not know that spell",
    };
    static char const* const ELSEWHERE[] = {
        // AN INSTANCE IS NOT ON THIS LIST, AND THE BIND VERB'S IS. That
        // difference is measured, not assumed. WorldSession::SendBindPoint
        // opens by returning when the map is instanceable, so #286 refuses a
        // bind there because the core would do nothing and say nothing. Nothing
        // on the hearthstone's path does that: SPELL_EFFECT_TELEPORT_UNITS has
        // no case in Spell::CheckCast's effect switch and SpellInfo::
        // CheckLocation only bars a spell carrying
        // SPELL_ATTR6_NOT_IN_RAID_INSTANCES, which this one does not. A
        // hearthstone cast from inside a dungeon is legal, it is the ordinary
        // way a player leaves one, and refusing it here would be this module
        // inventing a rule the game does not have.
        //
        // AN ARENA IS ON IT, because the core genuinely refuses there twice
        // over: Spell::CheckCast bars any spell with a recovery time of ten
        // minutes or more, and the handler bars the item before that with
        // EQUIP_ERR_NOT_DURING_ARENA_MATCH.
        "character is in an arena",
        // AND A TRANSPORT IS THIS MODULE'S OWN, which is worth saying plainly:
        // SPELL_FAILED_NOT_ON_TRANSPORT is declared in the core and used
        // nowhere, so the game would allow this. It is refused here because the
        // READ-BACK cannot survive it. The judgement below compares where the
        // character ends up against where it started, and a deck is a start
        // line that moves on its own, so `stayed` and `elsewhere` stop meaning
        // anything. A verdict that cannot be trusted is worse than a refusal
        // that can, and #279 already says nothing of ours can board a
        // transport, so this costs the family nothing today.
        "character is on a transport",
        // The one refusal that is about the destination rather than the
        // character: the home is already here, so there is nowhere to go. It is
        // `Elsewhere` and not `Never` because it is answered either by walking
        // away or by binding somewhere else, and both are things this family
        // does.
        "home is where the character already stands",
    };

    for (char const* literal : NEVER)
        if (detail == literal)
            return TownRetry::Never;
    for (char const* literal : ELSEWHERE)
        if (detail == literal)
            return TownRetry::Elsewhere;

    // Everything left is the character's own state, and every one of them ends
    // on its own: a fight, a flight, a corpse run, a walk, a cast already in
    // progress, an hour of cooldown, a mind control, a session that is going
    // away, a bot AI that has not attached yet. `Later` is also what an
    // unrecognised literal gets, which is the same call the bind, sell and
    // repair tables make.
    return TownRetry::Later;
}


// -------------------------------------------------------- a way round (#316)

char const* RoutePlanVerdictName(RoutePlanVerdict verdict)
{
    switch (verdict)
    {
        case RoutePlanVerdict::Planned:        return "planned";
        case RoutePlanVerdict::NoGraph:        return "no travel nodes on this map";
        case RoutePlanVerdict::NoEntryNode:    return "no travel node within reach";
        case RoutePlanVerdict::NoNearerNode:   return "nothing reachable on foot is nearer";
        case RoutePlanVerdict::BadLimits:      return "the limits asked for are not distances";
    }
    return "unknown";
}

namespace
{

// THE FILE ALREADY HAS A SQUARE ROOT AND THIS USES IT. `SquareRoot` above is
// hand-rolled precisely so this translation unit keeps needing nothing but its
// own header; a second one written here would spend that argument twice and
// leave two things to get wrong instead of one. A NaN or a negative comes back
// as zero from it, which reads here as "these two points are in the same place"
// and is refused by the caller's own gain and reach tests rather than by a
// separate branch.
float PlaneDistance(float ax, float ay, float bx, float by)
{
    double const dx = static_cast<double>(ax) - static_cast<double>(bx);
    double const dy = static_cast<double>(ay) - static_cast<double>(by);
    return static_cast<float>(SquareRoot(dx * dx + dy * dy));
}

// A binary heap over (cost, node index), smallest first. Hand-rolled for the
// same include reason: <queue> and <algorithm> are not on this file's list and
// the whole of what is needed here is push and pop.
struct CostHeap
{
    std::vector<float> cost;
    std::vector<std::uint32_t> at;

    bool Empty() const { return at.empty(); }

    void Push(float c, std::uint32_t index)
    {
        cost.push_back(c);
        at.push_back(index);
        std::size_t child = at.size() - 1;
        while (child > 0)
        {
            std::size_t const parent = (child - 1) / 2;
            if (cost[parent] <= cost[child])
                break;
            float const tc = cost[parent];
            cost[parent] = cost[child];
            cost[child] = tc;
            std::uint32_t const ta = at[parent];
            at[parent] = at[child];
            at[child] = ta;
            child = parent;
        }
    }

    void Pop(float& outCost, std::uint32_t& outIndex)
    {
        outCost = cost[0];
        outIndex = at[0];
        cost[0] = cost.back();
        at[0] = at.back();
        cost.pop_back();
        at.pop_back();
        std::size_t parent = 0;
        while (true)
        {
            std::size_t const left = parent * 2 + 1;
            std::size_t const right = left + 1;
            std::size_t smallest = parent;
            if (left < at.size() && cost[left] < cost[smallest])
                smallest = left;
            if (right < at.size() && cost[right] < cost[smallest])
                smallest = right;
            if (smallest == parent)
                break;
            float const tc = cost[parent];
            cost[parent] = cost[smallest];
            cost[smallest] = tc;
            std::uint32_t const ta = at[parent];
            at[parent] = at[smallest];
            at[smallest] = ta;
            parent = smallest;
        }
    }
};

// ONE DIJKSTRA OVER ONE MAP'S WALK LINKS, AND WHAT IT REACHED. Factored out of
// PlanFootRoute only because #326 runs it twice for the same journey - once
// allowed to cross guarded ground and once refusing to - and two hand-copied
// searches is how the two would come to disagree.
struct FootReach
{
    std::vector<float> best;
    std::vector<std::uint32_t> came;
    // The edge each node was reached BY, so the legs of a path can be read back
    // without a second lookup from a pair of node indices to a link.
    std::vector<std::uint32_t> cameEdge;
    std::vector<bool> reached;
};

void WalkOnFoot(std::vector<std::vector<std::uint32_t>> const& firstEdge,
                std::vector<std::uint32_t> const& edgeTo,
                std::vector<float> const& edgeCost,
                std::vector<bool> const& edgeGuarded, bool avoidGuarded,
                std::uint32_t entry, FootReach& out)
{
    std::size_t const count = firstEdge.size();
    out.best.assign(count, -1.f);
    out.came.assign(count, 0);
    out.cameEdge.assign(count, 0);
    out.reached.assign(count, false);
    CostHeap open;
    out.best[entry] = 0.f;
    out.came[entry] = entry;
    open.Push(0.f, entry);
    while (!open.Empty())
    {
        float cost = 0.f;
        std::uint32_t at = 0;
        open.Pop(cost, at);
        if (out.reached[at])
            continue;
        out.reached[at] = true;
        for (std::uint32_t e : firstEdge[at])
        {
            // A GUARDED LEG DOES NOT EXIST TO THIS PASS, which is how this
            // search has always treated a link that is not a walk: refused
            // rather than priced. Pricing was measured on the journey that
            // prompted #326 and moves nothing; see the header.
            if (avoidGuarded && edgeGuarded[e])
                continue;
            std::uint32_t const next = edgeTo[e];
            if (out.reached[next])
                continue;
            float const through = cost + edgeCost[e];
            if (out.best[next] < 0.f || through < out.best[next])
            {
                out.best[next] = through;
                out.came[next] = at;
                out.cameEdge[next] = e;
                open.Push(through, next);
            }
        }
    }
}

// How many legs of one reached path cross guarded ground. Walks the same chain
// the caller unwinds, so the count and the node list cannot disagree.
std::uint32_t GuardedLegsOn(FootReach const& reach,
                            std::vector<bool> const& edgeGuarded,
                            std::uint32_t entry, std::uint32_t goal)
{
    std::uint32_t guarded = 0;
    std::uint32_t at = goal;
    while (at != entry)
    {
        if (edgeGuarded[reach.cameEdge[at]])
            ++guarded;
        at = reach.came[at];
    }
    return guarded;
}

// What the guarded legs of one reached path are worth avoiding, summed (#400).
// Deliberately the same unwind as GuardedLegsOn beside it rather than a second
// walk of the chain: the count and the price are two readings of one path and
// must never be able to describe different paths.
float DetourWorthOn(FootReach const& reach,
                    std::vector<float> const& edgeDetour,
                    std::uint32_t entry, std::uint32_t goal)
{
    float worth = 0.f;
    std::uint32_t at = goal;
    while (at != entry)
    {
        float const leg = edgeDetour[reach.cameEdge[at]];
        // A NEGATIVE PRICE IS NOT A DISCOUNT. Nothing should ever write one,
        // and a sign typo that quietly SHRANK the budget would be invisible in
        // every log line this produces, so it is refused here rather than
        // trusted upstream. Same rule the link-cost loop already applies to
        // `link.yards`.
        if (leg > 0.f)
            worth += leg;
        at = reach.came[at];
    }
    return worth;
}

// ------------------------------ the ground's own price, in yards (#400) --

// HOW MUCH OF ONE SEGMENT LIES INSIDE ONE CIRCLE. The whole of the exposure
// measurement, and it is exact rather than sampled: the quadratic below is the
// standard segment-against-circle intersection, solved on the segment's own
// parameter and clamped to it, so a circle that swallows the segment scores its
// full length and one the segment merely clips scores the clipped part.
//
// Doubles inside for the same reason PlaneDistance uses them: the coordinates
// are world positions in the thousands and the radii are tens, so the
// difference of squares loses most of its significant figures in float.
float SegmentInsideCircle(RoutePoint const& from, RoutePoint const& to,
                          float cx, float cy, float radius)
{
    if (!(radius > 0.f))
        return 0.f;
    double const dx = static_cast<double>(to.x) - static_cast<double>(from.x);
    double const dy = static_cast<double>(to.y) - static_cast<double>(from.y);
    double const a = dx * dx + dy * dy;
    // A zero-length segment has no ground on it. Two identical waypoints are
    // ordinary in a surveyed leg and are not an error.
    if (!(a > 0.0))
        return 0.f;
    double const fx = static_cast<double>(from.x) - static_cast<double>(cx);
    double const fy = static_cast<double>(from.y) - static_cast<double>(cy);
    double const b = 2.0 * (fx * dx + fy * dy);
    double const c = fx * fx + fy * fy -
                     static_cast<double>(radius) * static_cast<double>(radius);
    double const disc = b * b - 4.0 * a * c;
    // Tangent counts as a miss: a path that grazes a circle at one point
    // crosses no ground inside it.
    if (!(disc > 0.0))
        return 0.f;
    double const root = SquareRoot(disc);
    double t0 = (-b - root) / (2.0 * a);
    double t1 = (-b + root) / (2.0 * a);
    if (t0 < 0.0)
        t0 = 0.0;
    if (t1 > 1.0)
        t1 = 1.0;
    if (t1 <= t0)
        return 0.f;
    return static_cast<float>((t1 - t0) * SquareRoot(a));
}

// How near one segment comes to one point. Only ever used for the log line's
// "how close did it get", so it answers in yards and not in squares.
float SegmentDistance(RoutePoint const& from, RoutePoint const& to,
                      float cx, float cy)
{
    double const dx = static_cast<double>(to.x) - static_cast<double>(from.x);
    double const dy = static_cast<double>(to.y) - static_cast<double>(from.y);
    double const a = dx * dx + dy * dy;
    if (!(a > 0.0))
        return PlaneDistance(from.x, from.y, cx, cy);
    double const fx = static_cast<double>(cx) - static_cast<double>(from.x);
    double const fy = static_cast<double>(cy) - static_cast<double>(from.y);
    double t = (fx * dx + fy * dy) / a;
    if (t < 0.0)
        t = 0.0;
    if (t > 1.0)
        t = 1.0;
    double const nx = static_cast<double>(from.x) + t * dx;
    double const ny = static_cast<double>(from.y) + t * dy;
    return PlaneDistance(static_cast<float>(nx), static_cast<float>(ny), cx, cy);
}

}  // namespace

float AggroRadiusYards(std::uint32_t creatureLevel, std::uint32_t playerLevel,
                       float detectionRange, float aggroRate)
{
    // Creature.cpp:3407 - the core answers 0 for a rate of 0 before it reads
    // anything else, and a negative rate is a configuration nobody meant.
    if (!(aggroRate > 0.f))
        return 0.f;
    // Creature.cpp:3421 - `if (aggroRadius < 1) return 0.0f;`, asked of the
    // detection range BEFORE the level term, which is why it is asked here and
    // not of the result.
    if (!(detectionRange >= 1.f))
        return 0.f;

    // Creature.cpp:3410-3416. See the header on the swapped local names
    // upstream: the quantity is player minus creature.
    std::int32_t levelDiff =
        static_cast<std::int32_t>(playerLevel) - static_cast<std::int32_t>(creatureLevel);
    if (levelDiff < -25)
        levelDiff = -25;

    float radius = detectionRange - static_cast<float>(levelDiff);
    // Creature.cpp:3434, MAX_AGGRO_RADIUS at Unit.h:44.
    if (radius > 45.f)
        radius = 45.f;
    // Creature.cpp:3439-3442. The pet floor of 10 is not reproduced: nothing
    // this module routes past is a pet, and a branch with no caller is a branch
    // nobody maintains.
    if (radius < 5.f)
        radius = 5.f;
    return radius * aggroRate;
}

std::uint32_t GreyLevel(std::uint32_t playerLevel)
{
    // Formulas.h `Acore::XP::GetGrayLevel`, branch for branch.
    if (playerLevel <= 5)
        return 0;
    if (playerLevel <= 39)
        return playerLevel - 5 - playerLevel / 10;
    if (playerLevel <= 59)
        return playerLevel - 1 - playerLevel / 5;
    return playerLevel - 9;
}

ConBand ConBandOf(std::uint32_t playerLevel, std::uint32_t creatureLevel)
{
    // SIGNED THROUGHOUT, because `playerLevel - 2` on an unsigned level 1
    // character is four billion and would read every creature in the world as
    // yellow. The core writes this against uint8 where the same trap exists and
    // is dodged only by the order of its branches; this does not rely on that.
    std::int32_t const pl = static_cast<std::int32_t>(playerLevel);
    std::int32_t const mob = static_cast<std::int32_t>(creatureLevel);

    // Not in Formulas.h. See the header: this is the client's `??`, at the same
    // gap CON_COLOR_UNKNOWN_LEVEL_DIFF has meant in this module since #326.
    if (mob >= pl + 10)
        return ConBand::Skull;
    // Formulas.h `GetColorCode`, edges included.
    if (mob >= pl + 5)
        return ConBand::Red;
    if (mob >= pl + 3)
        return ConBand::Orange;
    if (mob >= pl - 2)
        return ConBand::Yellow;
    if (mob > static_cast<std::int32_t>(GreyLevel(playerLevel)))
        return ConBand::Green;
    return ConBand::Grey;
}

char const* ConBandName(ConBand band)
{
    switch (band)
    {
        case ConBand::Grey:   return "grey";
        case ConBand::Green:  return "green";
        case ConBand::Yellow: return "yellow";
        case ConBand::Orange: return "orange";
        case ConBand::Red:    return "red";
        case ConBand::Skull:  return "skull";
    }
    return "unknown";
}

float ConBandWeight(ConBand band)
{
    switch (band)
    {
        // THE ONE THAT MATTERS. Grey is free, so a party that has outgrown a
        // stretch of ground pays nothing to walk it and the planner is returned
        // to pure distance without a special case saying so.
        case ConBand::Grey:   return 0.f;
        case ConBand::Green:  return 0.25f;
        case ConBand::Yellow: return 1.f;
        case ConBand::Orange: return 2.f;
        case ConBand::Red:    return 4.f;
        case ConBand::Skull:  return 8.f;
    }
    return 0.f;
}

bool SpawnCanAggro(DangerSpawn const& spawn)
{
    // #302's three unit_flags, already answered by the adapter's CanBeFought so
    // this rule and the shipped guarded-ground reading cannot drift apart.
    if (!spawn.canBeFought)
        return false;
    // CREATURE_FLAG_EXTRA_TRIGGER, CreatureData.h:53.
    if (spawn.trigger)
        return false;
    // CREATURE_FLAG_EXTRA_CIVILIAN, CreatureData.h:47. `Creature::CanStartAttack`
    // opens with this refusal, ahead of faction, level, distance and line of
    // sight, so a civilian is not a quiet threat: it is not a threat.
    if (spawn.civilian)
        return false;
    return true;
}

GroundDanger ScoreGroundDanger(std::vector<DangerSpawn> const& spawns,
                               std::vector<RoutePoint> const& path,
                               std::vector<std::uint32_t> const& partyLevels,
                               DangerLimits const& limits)
{
    GroundDanger out;
    // NONSENSE LIMITS SCORE NOTHING RATHER THAN BEING CLAMPED, the same refusal
    // PlanFootRoute makes of its own: a sign typo must not quietly become a rule
    // more permissive than the one written, and here "scores nothing" means the
    // route keeps today's distance-only answer, which is the safe direction.
    if (!(limits.aggroRate > 0.f) || !(limits.yardsPerExposedYard >= 0.f) ||
        !(limits.maxDetourYards >= 0.f))
        return out;
    // There is no ground between one point and itself.
    if (path.size() < 2 || spawns.empty())
        return out;

    // THE LOWEST MEMBER, AND A ZERO IS NOT A MEMBER. See the header: the lowest
    // carries both halves of the question, and a level 0 is a row that has not
    // been read.
    std::uint32_t lowest = 0;
    for (std::uint32_t level : partyLevels)
        if (level && (!lowest || level < lowest))
            lowest = level;
    if (!lowest)
        return out;

    for (DangerSpawn const& spawn : spawns)
    {
        if (!SpawnCanAggro(spawn))
            continue;

        ConBand const band = ConBandOf(lowest, spawn.level);
        float const weight = ConBandWeight(band);
        // Grey costs nothing and is skipped before the geometry rather than
        // after it, so the common case of a road lined with wildlife the party
        // has outgrown is also the cheap case.
        if (!(weight > 0.f))
            continue;

        float const radius = AggroRadiusYards(spawn.level, lowest,
                                              spawn.detectionRange, limits.aggroRate);
        if (!(radius > 0.f))
            continue;

        float inside = 0.f;
        float closest = -1.f;
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            inside += SegmentInsideCircle(path[i], path[i + 1], spawn.x, spawn.y, radius);
            float const gap = SegmentDistance(path[i], path[i + 1], spawn.x, spawn.y);
            if (closest < 0.f || gap < closest)
                closest = gap;
        }
        // A spawn whose circle the path never enters is not on this ground. It
        // may be five yards past the end of the leg and it is the next leg's
        // business, which is the whole reason legs are marked one at a time.
        if (!(inside > 0.f))
            continue;

        ++out.spawns;
        out.exposedYards += inside;
        out.detourYards += inside * weight;
        if (spawn.level > out.worstLevel)
            out.worstLevel = spawn.level;
        if (band > out.worstBand)
            out.worstBand = band;
        if (out.closestYards < 0.f || closest < out.closestYards)
            out.closestYards = closest;
    }

    out.detourYards *= limits.yardsPerExposedYard;
    if (out.detourYards > limits.maxDetourYards)
        out.detourYards = limits.maxDetourYards;
    return out;
}

RoutePlan PlanFootRoute(std::vector<RouteNode> const& nodes,
                        std::vector<RouteLink> const& links,
                        std::uint32_t mapId, float fromX, float fromY,
                        float toX, float toY, RoutePlanLimits const& limits)
{
    RoutePlan plan;
    if (!(limits.entryNodeYards > 0.f) || !(limits.minGainYards >= 0.f) ||
        !(limits.roundHandoverYards >= 0.f))
    {
        plan.verdict = RoutePlanVerdict::BadLimits;
        return plan;
    }

    // ONE MAP, AND THE INDEX IS BUILT ONCE. `local` maps a node id to its slot
    // in `here`, so the link pass below is a lookup rather than a scan.
    std::vector<RouteNode> here;
    std::map<std::uint32_t, std::uint32_t> local;
    for (RouteNode const& node : nodes)
    {
        if (node.mapId != mapId)
            continue;
        // A DUPLICATE ID IS DROPPED AND NOT MERGED. The id is what the links
        // name; two nodes claiming it would make every link about them
        // ambiguous, and picking one silently is how a route ends up walking to
        // a place nobody meant.
        if (local.find(node.id) != local.end())
            continue;
        local.insert(std::make_pair(node.id, static_cast<std::uint32_t>(here.size())));
        here.push_back(node);
    }
    if (here.empty())
    {
        plan.verdict = RoutePlanVerdict::NoGraph;
        return plan;
    }

    // The way in: the nearest node, and it must be near. A route from a node
    // the character cannot get to is not a route.
    std::uint32_t entry = 0;
    float entryDistance = -1.f;
    for (std::uint32_t i = 0; i < here.size(); ++i)
    {
        float const d = PlaneDistance(here[i].x, here[i].y, fromX, fromY);
        if (entryDistance < 0.f || d < entryDistance)
        {
            entryDistance = d;
            entry = i;
        }
    }
    if (entryDistance > limits.entryNodeYards)
    {
        plan.verdict = RoutePlanVerdict::NoEntryNode;
        return plan;
    }

    // Walk links only, both endpoints on this map, laid out per node so the
    // search below never scans the whole link list again.
    std::vector<std::vector<std::uint32_t>> firstEdge(here.size());
    std::vector<std::uint32_t> edgeTo;
    std::vector<float> edgeCost;
    std::vector<bool> edgeGuarded;
    std::vector<float> edgeDetour;
    // Nothing marked means nothing to go round, and then the second search
    // below is not run at all. That is not only an economy: it is what makes
    // "a journey with no guarded leg gets today's answer, for today's cost"
    // a property of the code rather than a claim about it.
    bool anyGuarded = false;
    for (RouteLink const& link : links)
    {
        if (!link.onFoot)
            continue;
        // A NEGATIVE OR ABSENT COST IS NOT A FREE LINK. Upstream prices a link
        // it wants refused at less than zero; treated as a distance that would
        // be a link the search prefers to every real one.
        if (!(link.yards >= 0.f))
            continue;
        std::map<std::uint32_t, std::uint32_t>::const_iterator const from = local.find(link.from);
        std::map<std::uint32_t, std::uint32_t>::const_iterator const to = local.find(link.to);
        if (from == local.end() || to == local.end())
            continue;
        firstEdge[from->second].push_back(static_cast<std::uint32_t>(edgeTo.size()));
        edgeTo.push_back(to->second);
        edgeCost.push_back(link.yards);
        edgeGuarded.push_back(link.guardedGround);
        // WHAT THIS LEG IS WORTH AVOIDING (#400), carried beside the flag and
        // read only where the flag already selected the leg. A price on an
        // unflagged leg is not an error and is simply never reached, because
        // the budget below is summed over the guarded legs of the plan.
        edgeDetour.push_back(link.detourWorthYards);
        anyGuarded = anyGuarded || link.guardedGround;
    }

    FootReach straight;
    WalkOnFoot(firstEdge, edgeTo, edgeCost, edgeGuarded, false, entry, straight);

    // THE GOAL IS CHOSEN OUT OF WHAT WAS REACHED. See the header: choosing it
    // by distance first and asking for a path second answers "no route" for
    // every journey measured on this issue, all of which have one.
    float const standing = PlaneDistance(fromX, fromY, toX, toY);
    std::uint32_t goal = entry;
    float goalDistance = -1.f;
    for (std::uint32_t i = 0; i < here.size(); ++i)
    {
        if (!straight.reached[i])
            continue;
        float const d = PlaneDistance(here[i].x, here[i].y, toX, toY);
        if (goalDistance < 0.f || d < goalDistance)
        {
            goalDistance = d;
            goal = i;
        }
    }
    if (goalDistance < 0.f || goal == entry ||
        standing - goalDistance < limits.minGainYards)
    {
        plan.verdict = RoutePlanVerdict::NoNearerNode;
        return plan;
    }

    // AND NOW THE ONE RULE #326 ADDS. Everything above is what this planner has
    // always done, `chosen` is that answer, and it stays that answer unless a
    // way round is found that is NOT FARTHER.
    //
    // REACH IS LEGS PLUS LEFTOVER, and the second half is not a refinement. The
    // nearest node to the Wailing Caverns door can only be reached through the
    // guards; the nearest one that cannot is 469 yards further out. Comparing
    // only the legs walked would adopt a way round that leaves the party a
    // continent short of its errand, and comparing only the leftover would
    // never adopt one at all.
    FootReach const* chosen = &straight;
    // `round` OUTLIVES THE BRANCH BELOW ON PURPOSE, because `chosen` may end up
    // pointing at it and the unwind that reads it is past the closing brace.
    // Declared inside, this compiled and ran correctly on one compiler and
    // segfaulted on another, which is what a dangling pointer is entitled to do.
    FootReach round;
    std::uint32_t guardedOnPlan = GuardedLegsOn(straight, edgeGuarded, entry, goal);
    if (anyGuarded && guardedOnPlan > 0)
    {
        // AND THE ONE RULE #400 CHANGES: THE GUARDS COST SOMETHING NOW.
        //
        // Until this line the comparison below was `round <= through`, in pure
        // yards, so a way round was taken only when it was NOT ONE YARD LONGER
        // than the way through the guard post. That is a real rule and it is
        // backwards: it treats a yard of open Barrens and a yard inside five
        // level 40 guards as the same yard, and this family walked the second
        // kind repeatedly because it was the shorter kind.
        //
        // The budget is what the guarded legs of the way through are worth
        // avoiding, in yards, summed off RouteLink::detourWorthYards, which the
        // caller measured with ScoreGroundDanger against the party's own
        // levels. So the trade is now: GO ROUND WHEN GOING ROUND COSTS LESS
        // THAN GOING THROUGH PLUS WHAT GOING THROUGH COSTS THE PARTY. A level
        // 40 guard post is worth thousands of yards to a party of 28 and
        // exactly zero to a party of 60, because every band of it is grey to
        // the second one, and at zero this whole expression collapses back to
        // the comparison that was here before.
        //
        // WHICH IS ALSO WHY NOTHING ELSE IN THIS FUNCTION MOVES. An unmeasured
        // graph prices every leg at zero, the budget is zero, and the plan is
        // byte for byte the plan this planner produced before #400 existed.
        // That is a property of the arithmetic rather than a claim about it.
        float const budget = DetourWorthOn(straight, edgeDetour, entry, goal);
        plan.detourBudgetYards = budget;
        float const reach = straight.best[goal] + goalDistance + budget;
        WalkOnFoot(firstEdge, edgeTo, edgeCost, edgeGuarded, true, entry, round);
        std::uint32_t roundGoal = entry;
        float roundDistance = -1.f;
        for (std::uint32_t i = 0; i < here.size(); ++i)
        {
            if (!round.reached[i] || i == entry)
                continue;
            float const d = PlaneDistance(here[i].x, here[i].y, toX, toY);
            if (standing - d < limits.minGainYards)
                continue;
            // AND IT MAY NOT DUMP THE JOURNEY ON THE GREEDY STEPPER. See
            // RoutePlanLimits::roundHandoverYards: without this the reach
            // comparison below will take fewer total yards by leaving four
            // thousand of them to a cone that cannot walk them.
            if (d > limits.roundHandoverYards)
                continue;
            // NOT FARTHER, written as a refusal of the ones that ARE, so a way
            // round that is exactly as far as today's plan is kept rather than
            // dropped by the rounding of a sum.
            if (round.best[i] + d > reach)
                continue;
            if (roundDistance < 0.f || d < roundDistance)
            {
                roundDistance = d;
                roundGoal = i;
            }
        }
        if (roundDistance >= 0.f)
        {
            chosen = &round;
            goal = roundGoal;
            goalDistance = roundDistance;
            guardedOnPlan = 0;
            plan.wentRound = true;
        }
    }

    // Unwound from the goal, so the caller reads it entry first.
    std::vector<std::uint32_t> backwards;
    std::uint32_t at = goal;
    while (true)
    {
        backwards.push_back(here[at].id);
        if (at == entry)
            break;
        at = chosen->came[at];
    }
    plan.nodes.reserve(backwards.size());
    for (std::size_t i = backwards.size(); i > 0; --i)
        plan.nodes.push_back(backwards[i - 1]);
    plan.verdict = RoutePlanVerdict::Planned;
    plan.yards = chosen->best[goal];
    plan.endsFromAimYards = goalDistance;
    plan.guardedLegs = guardedOnPlan;
    return plan;
}

RouteAim RouteLegStep(RouteCursor& cursor, std::vector<RoutePoint> const& route,
                      float x, float y, RouteLegLimits const& limits)
{
    RouteAim aim;
    if (route.empty())
        return aim;
    if (!(limits.lookaheadYards > 0.f) || !(limits.arrivedYards >= 0.f))
        return aim;
    if (cursor.at >= route.size())
        cursor.at = static_cast<std::uint32_t>(route.size()) - 1;

    // THE ROUTE IS SPENT WHEN ITS LAST POINT IS REACHED, and that is asked
    // FIRST. A character standing on the end of the route has a nearest point
    // and a lookahead like any other, and answering those before this one would
    // hand it its own feet every poll for the rest of the errand.
    RoutePoint const& last = route[route.size() - 1];
    if (PlaneDistance(last.x, last.y, x, y) <= limits.arrivedYards)
    {
        cursor.at = static_cast<std::uint32_t>(route.size()) - 1;
        aim.arrived = true;
        return aim;
    }

    // FORWARD ONLY. The scan starts where the cursor already is and never looks
    // behind it, so a route cannot be walked backwards however the stepper
    // wanders.
    std::uint32_t nearest = cursor.at;
    float nearestDistance = PlaneDistance(route[cursor.at].x, route[cursor.at].y, x, y);
    for (std::uint32_t i = cursor.at + 1; i < route.size(); ++i)
    {
        float const d = PlaneDistance(route[i].x, route[i].y, x, y);
        if (d < nearestDistance)
        {
            nearest = i;
            nearestDistance = d;
            continue;
        }
        // Running away by more than a lookahead means the rest of the route is
        // further off than anything this poll could aim at, so there is nothing
        // left to find. A route that doubles back is the case this bounds.
        if (d > nearestDistance + limits.lookaheadYards)
            break;
    }
    cursor.at = nearest;

    // ...and the aim is the furthest point still inside the lookahead, and no
    // further ahead than the caller allows. See RouteLegLimits::maxPointsAhead:
    // a measured corridor's points are each written down because the line past
    // them does not work, so its caller passes one and gets the next point
    // rather than the furthest one it could see.
    std::uint32_t ahead = nearest;
    while (ahead + 1 < route.size() &&
           PlaneDistance(route[ahead + 1].x, route[ahead + 1].y, x, y) <= limits.lookaheadYards)
    {
        // Counted from the cursor, and the test is on how far `ahead` has
        // ALREADY come rather than on where it is about to go: a bound of one
        // has to permit the first step or it would aim at the point the
        // character is standing on, which is the deadlock this whole limit
        // exists to avoid.
        if (limits.maxPointsAhead && ahead - nearest >= limits.maxPointsAhead)
            break;
        ++ahead;
    }

    aim.hasAim = true;
    aim.index = ahead;
    aim.x = route[ahead].x;
    aim.y = route[ahead].y;
    aim.z = route[ahead].z;
    return aim;
}


char const* StagingCorridorVerdictName(StagingCorridorVerdict verdict)
{
    switch (verdict)
    {
        case StagingCorridorVerdict::Joined:       return "joined";
        case StagingCorridorVerdict::NoCorridor:   return "no measured corridor for this door";
        case StagingCorridorVerdict::NotThisAim:   return "this walk does not go anywhere on that corridor";
        case StagingCorridorVerdict::TooFarToJoin: return "nothing on the corridor is near enough to step straight onto";
        case StagingCorridorVerdict::LegTooLong:   return "two of its points stand more than one lookahead apart";
        case StagingCorridorVerdict::BadLimits:    return "the limits asked for are not distances";
    }
    return "unknown";
}

StagingCorridorPlan PlanStagingCorridor(std::vector<RoutePoint> const& corridor,
                                        float aimX, float aimY,
                                        float fromX, float fromY,
                                        StagingCorridorLimits const& limits)
{
    StagingCorridorPlan plan;
    if (!(limits.endsAtYards >= 0.f) || !(limits.joinYards >= 0.f) ||
        !(limits.maxLegYards > 0.f))
    {
        plan.verdict = StagingCorridorVerdict::BadLimits;
        return plan;
    }
    if (corridor.empty())
    {
        plan.verdict = StagingCorridorVerdict::NoCorridor;
        return plan;
    }

    // WHOSE CORRIDOR IS THIS. Asked before anything else is measured, because
    // every other answer below is about a corridor that has already been shown
    // to belong to this walk. The aim has to stand on ONE of its points, and
    // which one is where the route will stop; see the header for why that is
    // any point rather than only the last.
    std::size_t end = 0;
    float endDistance = -1.f;
    for (std::size_t i = 0; i < corridor.size(); ++i)
    {
        float const d = PlaneDistance(corridor[i].x, corridor[i].y, aimX, aimY);
        if (endDistance < 0.f || d < endDistance)
        {
            endDistance = d;
            end = i;
        }
    }
    if (endDistance > limits.endsAtYards)
    {
        plan.verdict = StagingCorridorVerdict::NotThisAim;
        return plan;
    }

    // The whole corridor, not only the part about to be walked. See the header:
    // a malformed table is a fact worth failing on wherever a character happens
    // to join it.
    for (std::size_t i = 0; i + 1 < corridor.size(); ++i)
    {
        float const d = PlaneDistance(corridor[i].x, corridor[i].y,
                                      corridor[i + 1].x, corridor[i + 1].y);
        if (d > plan.longestLegYards)
            plan.longestLegYards = d;
    }
    if (plan.longestLegYards > limits.maxLegYards)
    {
        plan.verdict = StagingCorridorVerdict::LegTooLong;
        return plan;
    }

    std::size_t join = 0;
    float joinDistance = -1.f;
    for (std::size_t i = 0; i < corridor.size(); ++i)
    {
        float const d = PlaneDistance(corridor[i].x, corridor[i].y, fromX, fromY);
        if (joinDistance < 0.f || d < joinDistance)
        {
            joinDistance = d;
            join = i;
        }
    }
    // SAID BEFORE THE BOUND IS ASKED, so the refusal carries the reading it was
    // made on (#356). Which point is nearest is a fact about the corridor and
    // the character, and it is the same fact whether or not the distance passes;
    // stamping it here rather than after the test is what lets a caller that is
    // too far off walk TOWARD the corridor instead of abandoning it. See the
    // header on TooFarToJoin for the journey that was being thrown away.
    plan.joinIndex = join;
    plan.joinYards = joinDistance;
    if (joinDistance > limits.joinYards)
    {
        plan.verdict = StagingCorridorVerdict::TooFarToJoin;
        return plan;
    }

    // WALKING TOWARD THE AIM, WHICHEVER WAY ALONG THE CORRIDOR THAT IS (#356).
    // The points between the join and the aim are the walk either way round;
    // see the header for why the direction they were measured in is not one of
    // the things the row measures. Equal is fine and means "standing at the aim
    // already": the route is the one point, and RouteLegStep answers `arrived`
    // for it.
    plan.reversed = join > end;
    std::size_t const span = (plan.reversed ? join - end : end - join) + 1;
    plan.route.reserve(span);
    for (std::size_t i = 0; i < span; ++i)
        plan.route.push_back(corridor[plan.reversed ? join - i : join + i]);
    plan.verdict = StagingCorridorVerdict::Joined;
    plan.endIndex = end;
    return plan;
}

char const* TeleportFlightWord(TeleportFlight flight)
{
    switch (flight)
    {
        case TeleportFlight::Landed:
            return "landed";
        case TeleportFlight::InFlight:
            return "in-flight";
        case TeleportFlight::Stranded:
            return "stranded";
        case TeleportFlight::Gone:
            break;
    }
    return "gone";
}

TeleportFlight ReadTeleportFlight(bool inNameMap, bool inWorld, bool stillTeleporting,
                                  uint32_t waitedMs, uint32_t ceilingMs)
{
    // THE NAME MAP IS THE ONLY THING THAT ANSWERS "IS IT STILL LOGGED IN".
    // ObjectAccessor's name map outlives a teleport and does not outlive a
    // logout, which is exactly the distinction the hearth's read-back could not
    // make - it asked with checkInWorld defaulted to true and got null for both.
    if (!inNameMap)
        return TeleportFlight::Gone;

    // ASKED BEFORE `inWorld`, AND THAT ORDER IS THE WHOLE FIX. A character in
    // the middle of a far teleport is not in the world and is not gone, and
    // reading the world for it produces the position it is LEAVING. Every
    // caller has to be told to wait rather than handed a stale coordinate.
    if (stillTeleporting)
        return waitedMs < ceilingMs ? TeleportFlight::InFlight : TeleportFlight::Stranded;

    // In the name map, not teleporting, and not in the world: a logout that has
    // begun. Nothing to read and nothing to wait for.
    if (!inWorld)
        return TeleportFlight::Gone;

    return TeleportFlight::Landed;
}

SummonRequest ParseSummonRequest(std::string const& command)
{
    SummonRequest request;
    std::vector<std::string> const words = TownWords(command);

    // EMPTY IS A REFUSAL WITH WORDS. See the header: this verb takes an
    // argument, and there is no safe default for "which member of the party".
    if (words.empty())
    {
        request.error = "malformed summon: name the character to summon";
        return request;
    }

    if (words[0] == "use")
    {
        if (words.size() == 2)
        {
            request.verb = SummonVerb::Use;
            request.who = words[1];
            return request;
        }
        if (words.size() < 2)
        {
            request.error = "malformed summon: use wants the character to summon";
            return request;
        }
        request.error = "malformed summon: use takes one name and no more";
        return request;
    }

    // A BARE NAME IS THE SAME REQUEST. The verb has one form, so requiring the
    // word `use` in front of it would only be ceremony - and a character called
    // `use` is not a thing this realm can produce, because the branch above
    // claims that word first and a one-word `use` is refused by name.
    if (words.size() == 1)
    {
        request.verb = SummonVerb::Use;
        request.who = words[0];
        return request;
    }

    request.error = "malformed summon: want one name, or use and one name";
    return request;
}

char const* SummonOutcomeWord(SummonOutcome outcome)
{
    switch (outcome)
    {
        case SummonOutcome::Arrived:
            return "arrived";
        case SummonOutcome::Stayed:
            return "stayed";
        case SummonOutcome::Elsewhere:
            return "elsewhere";
        case SummonOutcome::Unreadable:
            break;
    }
    return "unreadable";
}

bool SummonWouldMoveNobody(HomeBind const& summoned, HomeBind const& at,
                           float arrivedYards)
{
    // A reading nobody took is not a reason to refuse, exactly as in
    // HearthWouldMoveNobody: the executor asks about the readings themselves
    // first, and answering `true` here for an unread position would refuse
    // every character whose position could not be read.
    if (!summoned.known || !at.known)
        return false;
    // HearthWithin, and not a second copy of it. It is the anonymous-namespace
    // helper above in this same translation unit, it compares three dimensions
    // within one map, and that is the comparison this verb needs too. Copying
    // it to give it a summon-shaped name would be two places to get the map
    // check wrong instead of one.
    return HearthWithin(summoned, at, arrivedYards);
}

SummonOutcome SummonReadBack(HomeBind const& from, HomeBind const& at, HomeBind const& now,
                             float arrivedYards, float movedYards)
{
    // ALL THREE OR NOTHING, the rule HearthReadBack and BindReadBack keep for
    // the same reason: a verdict built out of two readings and a guess at the
    // third is the half that was missing, invented.
    if (!from.known || !at.known || !now.known)
        return SummonOutcome::Unreadable;

    // AND THE START LINE MUST NOT BE THE FINISH LINE. When the character was
    // already at the summon point, `Arrived` and `Stayed` are the same reading.
    // The executor refuses that row before the packet
    // (SummonWouldMoveNobody), so reaching here means the summoner moved while
    // the ritual was settling - which it may, because the summon point is the
    // summoner's own position and a bot walks. Saying so is honest; picking one
    // of the two would be a coin toss reported as a measurement.
    if (HearthWithin(from, at, arrivedYards))
        return SummonOutcome::Unreadable;

    // THE SUMMON POINT FIRST. It is the only outcome that is the thing that was
    // asked for, and after the guard above it cannot also be the start line.
    if (HearthWithin(now, at, arrivedYards))
        return SummonOutcome::Arrived;

    // Still on the start line, on a tolerance of its own and a tighter one than
    // the arrival's, for the reason HearthReadBack gives: the width that has to
    // absorb a bot walking away from where it landed is not the width that
    // decides whether anybody moved at all.
    if (HearthWithin(now, from, movedYards))
        return SummonOutcome::Stayed;

    return SummonOutcome::Elsewhere;
}

uint32_t SummonVerifyWindowMs(uint32_t settleMs, uint32_t marginMs, uint32_t floorMs)
{
    // Saturating, for the reason HearthVerifyWindowMs saturates: an addition
    // that wraps turns an absurd settle time into a window shorter than the
    // floor, which is precisely the reading that judges a summon as `Stayed`
    // while the ritual is still running.
    uint32_t const wanted = settleMs > UINT32_MAX - marginMs ? UINT32_MAX : settleMs + marginMs;
    return wanted < floorMs ? floorMs : wanted;
}

TownRetry SummonRefusalRetry(std::string const& detail)
{
    // The literals mod_overseer.cpp's DoSummon returns, grouped by what would
    // have to change for the SAME row to succeed.
    static char const* const NEVER[] = {
        "malformed summon: name the character to summon",
        "malformed summon: use wants the character to summon",
        "malformed summon: use takes one name and no more",
        "malformed summon: want one name, or use and one name",
        "malformed summon request",
        // A row that names its own summoner is not a request that becomes valid
        // later, anywhere, under any state. Neither is one whose helper is the
        // summoner: GameObject::Use refuses the ritual's owner by name, because
        // the spell effect already counted it.
        "a character cannot summon itself",
        "the second clicker cannot be the summoner",
        // The world database is the wall. Waiting does not add a meeting stone
        // to a map and neither does walking to another one, because there is
        // only ever one stone per dungeon and this refusal means the module
        // could not find the template at all.
        "the core does not know that meeting stone",
        "the core does not know that summoning portal",
    };
    static char const* const ELSEWHERE[] = {
        // The stone is a place. Every one of these is answered by standing
        // somewhere else, and by nothing else: the sweep found no stone, the
        // character is outside the interaction distance the handler itself
        // enforces, or there is no second party member standing close enough to
        // the portal to be the clicker the ritual needs.
        "no meeting stone within reach of the summoner",
        "no meeting stone within reach of the second clicker",
        "no second party member is at the stone",
        // AND THE TWO THE APPROACH ADDS (#355). The verb now walks a clicker
        // the last few yards, and it refuses to walk over ground that does not
        // hold - which is #262's rule and the reason this module steps rather
        // than aims. That refusal is about a PLACE and not about the character:
        // the same clicker standing on the other side of the stone would be
        // walked without an argument, and waiting where it is changes nothing,
        // which is exactly what `Elsewhere` means here.
        "the ground between the summoner and the meeting stone does not hold",
        "the ground between the second clicker and the meeting stone does not hold",
        // Being on the other side of the ocean is the point of this verb, so
        // "already here" is not a failure of the character's state - it is a
        // statement about where it is standing, and it stops being true the
        // moment anything moves.
        "the character to summon is already at the summon point",
        // The core's own SPELL_EFFECT_SUMMON_PLAYER check: a summoner inside a
        // dungeon may only summon somebody the instance would let in. Another
        // stone, outside, answers differently.
        "the character to summon cannot enter the instance the summoner is in",
    };

    for (char const* literal : NEVER)
        if (detail == literal)
            return TownRetry::Never;
    for (char const* literal : ELSEWHERE)
        if (detail == literal)
            return TownRetry::Elsewhere;

    // Everything left is somebody's own state, and every one of them ends on
    // its own: a fight, a corpse, a flight, a walk, a cast already in progress,
    // a party that has not formed yet, a level that is still climbing, a summon
    // already pending, a mind control, a session going away, a bot AI that has
    // not attached. A walk that ran out of time is in here too and belongs here
    // rather than in ELSEWHERE (#355): the clickers ended the row nearer the
    // stone than they started it, so the next row walks a shorter distance from
    // a better place, which is the definition of `Later` and not of "go and
    // stand somewhere else". `Later` is also what an unrecognised literal gets,
    // which is the same call the bind, hearth, sell and repair tables make.
    return TownRetry::Later;
}

char const* SummonApproachWord(SummonApproach approach)
{
    switch (approach)
    {
        case SummonApproach::NoStone:
            return "no-stone";
        case SummonApproach::Click:
            return "click";
        case SummonApproach::Walk:
            return "walk";
        case SummonApproach::OutOfTime:
            break;
    }
    return "out-of-time";
}

char const* SummonPortalWord(SummonPortal portal)
{
    switch (portal)
    {
        case SummonPortal::Click:
            return "click";
        case SummonPortal::Wait:
            return "wait";
        case SummonPortal::NoChannel:
            return "no-channel";
        case SummonPortal::OutOfTime:
            break;
    }
    return "out-of-time";
}

SummonPortal ReadSummonPortal(bool portalSeen, bool channelling, uint32_t waitedMs,
                              uint32_t ceilingMs)
{
    // THE OBJECT FIRST, AND BEFORE EVERYTHING. A portal in the world is the
    // thing that gets clicked, so its existence settles this whether the clock
    // has run out on the same poll or the channel reads oddly. It is also the
    // only one of these three inputs that is a fact about the ritual rather
    // than about the summoner.
    if (portalSeen)
        return SummonPortal::Click;

    // AND THEN THE CHANNEL, BEFORE THE CLOCK. The ritual object belongs to the
    // channel: the core hands it to the caster with `AddGameObject` and takes
    // it away when the spell is cancelled. So a summoner that has stopped
    // channelling with no portal in the world is not a row that needs more
    // time, it is a row whose spell went away - refused at cast time, walked
    // out of, or interrupted - and spending the rest of the window on it would
    // hold a claim open to watch nothing happen.
    if (!channelling)
        return SummonPortal::NoChannel;

    // THE WAIT HAS HAD ITS TIME. Named separately from the answer above because
    // they are different things to report and different things to do about:
    // one says the spell is gone, the other says the spell is still running and
    // has produced nothing, and a single literal covering both is exactly the
    // fault this issue is half about.
    if (waitedMs >= ceilingMs)
        return SummonPortal::OutOfTime;

    return SummonPortal::Wait;
}

SummonApproach ReadSummonApproach(bool inReach, float nearestYards, float walkYards,
                                  uint32_t walkedMs, uint32_t ceilingMs)
{
    // A READING NOBODY TOOK IS NOT A DISTANCE. The executor's sweep leaves
    // `nearestYards` negative when it found nothing stone-shaped at all, and
    // that is the same answer to a sender as a stone on the far side of the
    // zone: there is nothing here to use.
    //
    // ASKED BEFORE `inReach`, so a caller that hands in a reach verdict about a
    // stone it never found cannot be answered `Click`.
    if (nearestYards < 0.f)
        return SummonApproach::NoStone;

    // THE CORE'S OWN GATE FIRST, AND BEFORE THE CLOCK. `inReach` is
    // GameObject::IsWithinDistInMap against the stone's own interaction
    // distance - the exact test WorldSession::HandleGameObjectUseOpcode makes -
    // so a stone this answers `Click` about is one that handler will accept.
    // And a clicker that gets here on the last poll of its walk has arrived,
    // whatever the clock says.
    if (inReach)
        return SummonApproach::Click;

    // FURTHER THAN THIS VERB WILL WALK ANYBODY. Not an error and not a wait:
    // the stone was seen, it is simply not one this row is going to close the
    // distance to, and the honest answer is the same one it gives for a map
    // with no stone on it. The executor's sweep width and this walk are the
    // same number today, so this branch is what keeps the rule true if either
    // of them ever moves.
    if (nearestYards > walkYards)
        return SummonApproach::NoStone;

    // THE WALK HAS HAD ITS TIME. Something the executor cannot see is holding
    // the character up - a fight it walked into, a cliff the ground check will
    // not take it over, a door - and a row that kept walking for ever would
    // hold a claim open and say nothing. Giving up with the distance in the row
    // is what the next attempt needs.
    if (walkedMs >= ceilingMs)
        return SummonApproach::OutOfTime;

    return SummonApproach::Walk;
}

bool ClassRestoresManaByDrinking(uint32_t classId)
{
    switch (classId)
    {
        case 2:   // paladin
        case 3:   // hunter, whose shots cost mana in this expansion
        case 5:   // priest
        case 7:   // shaman
        case 8:   // mage
        case 9:   // warlock
        case 11:  // druid
            return true;
        default:
            // warrior (1), rogue (4), death knight (6), and anything this
            // module has never heard of.
            return false;
    }
}

char const* ConjureWhatWord(ConjureWhat what)
{
    switch (what)
    {
        case ConjureWhat::Food:
            return "food";
        case ConjureWhat::Water:
            return "water";
        case ConjureWhat::None:
            break;
    }
    return "none";
}

ConjureRequest ParseConjureRequest(std::string const& command)
{
    ConjureRequest request;
    std::vector<std::string> const words = TownWords(command);

    if (words.empty())
    {
        // NO EMPTY FORM, unlike the hearth's. A hearthstone has exactly one
        // thing it can do, so an empty command can only mean that one thing.
        // This verb has two, and guessing which of food and water an empty row
        // meant would be this module choosing what a family eats.
        request.error = ConjureRefusal::Malformed;
        return request;
    }

    ConjureWhat what = ConjureWhat::None;
    if (words[0] == "food")
        what = ConjureWhat::Food;
    else if (words[0] == "water")
        what = ConjureWhat::Water;
    else
    {
        request.error = ConjureRefusal::Malformed;
        return request;
    }

    bool haveUpTo = false;
    uint32_t upTo = 0;
    for (size_t i = 1; i < words.size(); ++i)
    {
        uint32_t value = 0;
        if (!TownKeyed(words[i], "up_to", value))
        {
            request.error = ConjureRefusal::Malformed;
            return request;
        }
        // Twice is a row that disagrees with itself, zero is a row that asks
        // for nothing, and over the ceiling is almost always a typed extra
        // zero. None of the three is guessed at.
        if (haveUpTo || value == 0 || value > CONJURE_UNITS_MAX)
        {
            request.error = ConjureRefusal::Malformed;
            return request;
        }
        haveUpTo = true;
        upTo = value;
    }

    request.what = what;
    request.capped = haveUpTo;
    request.upTo = haveUpTo ? upTo : CONJURE_UNITS_DEFAULT;
    return request;
}

ConjurePlan PlanConjure(uint32_t carried, uint32_t wanted, uint32_t perCast,
                        uint32_t roomUnits)
{
    ConjurePlan plan;
    plan.carried = carried;
    plan.perCast = perCast;
    plan.wanted = wanted;

    if (carried >= wanted)
    {
        plan.wanted = carried;
        plan.nothingToDo = true;
        return plan;
    }

    // THE BAGS CLAMP THE TARGET, NOT THE OTHER WAY ROUND. Asking for twenty
    // when there is room for six is a request for six, said out loud, rather
    // than a refusal: six is worth having and the row reports what it aimed
    // for. A refusal here would leave a party with nothing over a bag that was
    // merely nearly full.
    uint32_t const missing = wanted - carried;
    if (roomUnits < missing)
    {
        plan.roomLimited = true;
        plan.wanted = carried + roomUnits;
    }

    if (plan.wanted <= carried)
    {
        // No room at all. Named as nothing to do rather than as a plan of zero
        // casts that a caller could mistake for "already stocked".
        plan.nothingToDo = true;
        return plan;
    }

    if (perCast == 0)
    {
        // A spell whose effect could not be read. Zero casts, and the executor
        // says why rather than dividing by this.
        return plan;
    }

    uint32_t const stillMissing = plan.wanted - carried;
    plan.casts = (stillMissing + perCast - 1) / perCast;
    return plan;
}

char const* ConjureStepWord(ConjureStep step)
{
    switch (step)
    {
        case ConjureStep::Cast:
            return "cast";
        case ConjureStep::Wait:
            return "wait";
        case ConjureStep::Settle:
            return "settle";
        case ConjureStep::Done:
            return "done";
        case ConjureStep::GaveUp:
            break;
    }
    return "gave up";
}

char const* ConjureGaveUpReasonWord(ConjureGaveUp reason)
{
    switch (reason)
    {
        case ConjureGaveUp::NeverStoodStill:
            return ConjureRefusal::NeverStoodStill;
        case ConjureGaveUp::CastRefused:
            return ConjureRefusal::CastRefused;
        case ConjureGaveUp::NothingAppeared:
            return ConjureRefusal::NothingAppeared;
        case ConjureGaveUp::BudgetSpent:
            return ConjureRefusal::BudgetSpent;
        case ConjureGaveUp::NotGivenUp:
            break;
    }
    return "";
}

namespace
{

// "NOT ONE OF THEM PRODUCED ANYTHING" AND "NOT ONE OF THEM EVER STARTED" ARE
// DIFFERENT SENTENCES, and this is the one line that tells them apart. A row
// that never had a single cast accepted, and had at least one declined, hit the
// bot AI's own refusal rather than the spell; anything else really did cast.
//
// Shared by both places a row can end on this pair so the two cannot answer
// differently, which is the mistake the whole of #325 is.
OverseerDecisions::ConjureGaveUp NothingOrRefused(
    OverseerDecisions::ConjureProgress const& progress)
{
    if (progress.castsSpent == 0 && progress.castsRefused != 0)
        return OverseerDecisions::ConjureGaveUp::CastRefused;
    return OverseerDecisions::ConjureGaveUp::NothingAppeared;
}

}  // namespace

ConjureGaveUp ConjureGiveUpReason(ConjureProgress const& progress)
{
    // Arriving is not giving up, and it is asked first for the same reason
    // ConjureNextStep asks it first: a character that reached its target on the
    // cast that is finishing right now has finished, whatever else is true.
    if (progress.wanted != 0 && progress.carried >= progress.wanted)
        return ConjureGaveUp::NotGivenUp;

    // THE WINDOW, AND IT BEATS A CAST IN FLIGHT. Everything below this line is
    // a reason to stop early; this is the reason a row cannot run for ever, and
    // a row that has run out of time has to end even mid-cast because a
    // character is being held still on the other side of it.
    //
    // Which sentence it ends with is the interesting part. A row that never got
    // a single cast away, on a character that has been walking, timed out for
    // exactly one reason and it is not the spell.
    if (progress.outOfTime)
    {
        if (progress.castsSpent == 0 && (progress.moving || progress.movingPolls != 0))
            return ConjureGaveUp::NeverStoodStill;
        return NothingOrRefused(progress);
    }

    // A cast in flight is progress, so it beats every remaining test. Charging
    // a three second cast against a budget counted in two second polls is how a
    // working loop gets killed for being slow.
    if (progress.castInFlight)
        return ConjureGaveUp::NotGivenUp;

    if (progress.castsSpent >= progress.castsAllowed)
        return ConjureGaveUp::BudgetSpent;

    // The test that catches a spell that does not work. Out of mana,
    // interrupted, bags filled by something else, a product this character may
    // not use, a bot AI that declines the cast before it starts: from here all
    // of those look the same, which is a cast that goes out and produces
    // nothing. An idleLimit of 0 disables it rather than giving up before the
    // first cast.
    if (progress.idleLimit != 0 && progress.idlePolls >= progress.idleLimit)
        return NothingOrRefused(progress);

    // ASKED LAST OF THE FOUR, AND THAT ORDER IS DELIBERATE. A row that got
    // casts off and then found the character walking again has already learned
    // something about the spell, and "the casts produced nothing" is the more
    // useful sentence for it. `NeverStoodStill` is for the row that never got
    // to try at all, which is the failure that shipped in #320.
    if (progress.movingLimit != 0 && progress.movingPolls >= progress.movingLimit)
        return ConjureGaveUp::NeverStoodStill;

    return ConjureGaveUp::NotGivenUp;
}

ConjureStep ConjureNextStep(ConjureProgress const& progress)
{
    // Done first, before anything else at all: a character that has already
    // reached the target is finished whatever it happens to be doing, and even
    // if the window ran out on the poll that got it there.
    if (progress.wanted != 0 && progress.carried >= progress.wanted)
        return ConjureStep::Done;

    // EVERY WALL, IN ONE QUESTION, and asked before `Wait` rather than after it
    // so that the window can end a row that is mid-cast. ConjureGiveUpReason
    // answers NotGivenUp for a cast in flight that still has time, so a working
    // loop is not cut short by moving this line up.
    if (ConjureGiveUpReason(progress) != ConjureGaveUp::NotGivenUp)
        return ConjureStep::GaveUp;

    if (progress.castInFlight)
        return ConjureStep::Wait;

    // STILL WALKING IS NOT STILL CASTING, and telling those two apart is the
    // whole of #325. PlayerbotAI::CastSpell refuses a moving bot any spell with
    // a cast time outright, and the core cancels one already preparing the
    // moment the caster moves, so a cast sent now is a cast thrown away and an
    // idle poll charged for nothing that was ever tried. The character has been
    // asked to stand; this is waiting for it to actually stop.
    if (progress.moving)
        return ConjureStep::Settle;

    return ConjureStep::Cast;
}

char const* ConjureOutcomeWord(ConjureOutcome outcome)
{
    switch (outcome)
    {
        case ConjureOutcome::Filled:
            return "filled";
        case ConjureOutcome::Short:
            return "short";
        case ConjureOutcome::Nothing:
            return "nothing";
        case ConjureOutcome::Unreadable:
            break;
    }
    return "unreadable";
}

ConjureOutcome ConjureReadBack(bool readable, uint32_t before, uint32_t after,
                               uint32_t wanted)
{
    if (!readable)
        return ConjureOutcome::Unreadable;
    // A row that asked for nothing has no target to be judged against, and
    // answering `Filled` to it would report a success nobody asked for. The
    // parse refuses `up_to:0`, so this is only reachable through a caller that
    // built a request by hand.
    if (wanted == 0)
        return ConjureOutcome::Unreadable;
    if (after >= wanted)
        return ConjureOutcome::Filled;
    if (after > before)
        return ConjureOutcome::Short;
    return ConjureOutcome::Nothing;
}

char const* ConjureCastBlocker(ConjureCastGate const& gate)
{
    // THE ORDER IS THE BOT AI'S OWN. PlayerbotAI::CastSpell tests flying first,
    // then the stand state, then movement, and only then builds a Spell whose
    // prepare answers the cooldowns. Reporting them in a different order would
    // name a wall the caster had not reached yet.
    if (!gate.grounded)
        return ConjureRefusal::InFlight;
    if (!gate.standing)
        return ConjureRefusal::NotStanding;
    if (gate.moving)
        return ConjureRefusal::Moving;
    if (!gate.spellReady)
        return ConjureRefusal::SpellOnCooldown;
    if (!gate.globalReady)
        return ConjureRefusal::GlobalCooldown;
    return "";
}

uint32_t ConjureVerifyWindowMs(uint32_t castMs, uint32_t casts, uint32_t marginMs,
                               uint32_t settleMs, uint32_t floorMs, uint32_t ceilingMs)
{
    // Saturating throughout. Every input here comes from somewhere this module
    // does not own - a DBC, a plan, a config - and the one failure this
    // function must not have is a huge input wrapping into a window so short
    // that a working conjure is judged as having done nothing.
    uint64_t const perCast = uint64_t(castMs) + uint64_t(marginMs);
    uint64_t const total = perCast * uint64_t(casts == 0 ? 1u : casts) + uint64_t(settleMs);
    uint64_t floored = total < uint64_t(floorMs) ? uint64_t(floorMs) : total;
    // THE CEILING WINS OVER THE FLOOR. A cap a floor can lift is not a cap, and
    // what is being capped here is how long a character is held still.
    if (ceilingMs != 0 && floored > uint64_t(ceilingMs))
        floored = uint64_t(ceilingMs);
    return floored > 0xFFFFFFFFull ? 0xFFFFFFFFu : uint32_t(floored);
}

TownRetry ConjureRefusalRetry(std::string const& detail)
{
    static char const* const NEVER[] = {
        // The row itself. Every vendor, every field, every hour: the same
        // answer.
        ConjureRefusal::Malformed,
        // The spellbook. A character that has not learned a conjure will not
        // learn one by being asked again, and a rank whose product it may not
        // use is a mismatch between two pieces of the world's own data. Both
        // are `Never` rather than `Later` because the thing that would change
        // them is a training session or a data fix, neither of which happens
        // by waiting, and a row retried for ever costs a row for ever.
        ConjureRefusal::CannotConjure,
        ConjureRefusal::NoSpellInfo,
        ConjureRefusal::NoItemTemplate,
        ConjureRefusal::CannotUseItem,
        // A spell that says it creates nothing is the world's own data
        // disagreeing with itself. Waiting does not settle that argument.
        ConjureRefusal::MakesNothing,
    };

    // The three added by #325 are all `Later` and fall through to the default
    // below, which is worth saying out loud rather than leaving to inference. A
    // cast that did not start, a character that would not stand still, and a
    // budget spent are every one of them about where the character was and what
    // it was doing in one 49 second window. Ask again in a minute and any of
    // them can answer differently, which is exactly what `Later` means.

    // THERE IS NO `Elsewhere` LIST, and its absence is the point rather than an
    // oversight. Every other errand in this module's town trip is refused
    // somewhere and allowed somewhere else, because each needs an NPC: a
    // vendor, a repairer, a banker, an auctioneer, an innkeeper, a mailbox.
    // A conjure needs nobody. Walking changes nothing about whether it works,
    // so no refusal here can honestly be classed as one that walking fixes.

    for (char const* literal : NEVER)
        if (detail == literal)
            return TownRetry::Never;

    // Everything left is the character's own state and every one of them ends
    // on its own: a fight, a corpse, a flight, a stun, a trade, a cast already
    // running, a bag that gets emptied, a session that comes back, a stack that
    // gets eaten or handed out. `Later` is also what an unrecognised literal
    // gets, which is the same call the sell, repair, buy, bind and hearth
    // tables make: a refusal this table has never heard of is more likely a new
    // transient than a new permanent.
    return TownRetry::Later;
}

// ------------------------------- holding a character still to cast (#335) --

CastHoldPlan PlanCastHold(CastHoldFacts const& facts)
{
    CastHoldPlan plan;
    // ADD `stay` ONLY IF IT IS ABSENT, so the release can tell "this hold put it
    // there" from "an operator did, and it is not mine to take away".
    plan.addStay = !facts.hasStay;
    // AND REMOVE THE TWO MOVERS ONLY IF THEY ARE PRESENT, for the same reason
    // read the other way round. `follow` is the one that matters for the case
    // this was written for: these characters are one permanent party, and a
    // follower is moved by the party rather than by anything of its own, so a
    // hold that leaves `follow` on has stopped nothing.
    plan.dropFollow = facts.hasFollow;
    plan.dropNewRpg = facts.hasNewRpg;
    return plan;
}

InnHold InnHoldStep(bool atTheInn, bool bindRefusedHere, bool alreadyHeld)
{
    // THE REFUSAL IS ASKED FIRST, AND IT OUTRANKS STANDING THERE. Standing at
    // the recorded point is exactly the state in which a refusal is most
    // tempting to ignore - the character is where it was sent, so surely it
    // should stay - and it is exactly the state in which staying is useless,
    // because the bind was just attempted from there and turned down.
    if (bindRefusedHere)
        return alreadyHeld ? InnHold::Release : InnHold::Nothing;

    // TAKEN WHETHER OR NOT ONE IS ALREADY IN FORCE, on purpose. Placing a hold
    // and re-asserting one are the same call, because the register stops this
    // module's own sweeps and nothing else: a strategy something outside it
    // granted has to be taken off again, and a caller that skipped the
    // re-assertion would leave that to nobody.
    if (atTheInn)
        return InnHold::Take;

    return alreadyHeld ? InnHold::Release : InnHold::Nothing;
}

// ------------- standing at a counter long enough to trade there (#378) ------

CounterArrival CounterArrivalStep(CounterRole role, bool inReach, bool oneIsNearby)
{
    // NOT A COUNTER, NOT THIS DECISION. Answered first and answered `Done`,
    // which is byte for byte what the arrival branch did for every creature
    // errand before this existed: a trainer, a flight master, a stable master
    // and a tabard designer all still arrive and release. Putting this test
    // anywhere but first would make a new reason to hold apply to errands
    // nobody has measured anything about.
    if (role == CounterRole::None)
        return CounterArrival::Done;

    // IN REACH OUTRANKS EVERYTHING ELSE, because it is the only state in which
    // the promise a hold makes is true. It is deliberately not combined with
    // `oneIsNearby`: an adapter that could pass `inReach` true and `oneIsNearby`
    // false has a bug in its sweep, and answering StandAndTrade anyway is the
    // right thing for this function to do about it - the character IS at a
    // counter the core will talk to, whatever a wider search thought.
    if (inReach)
        return CounterArrival::StandAndTrade;

    // NEARBY AND OUT OF REACH IS "NOT YET", AND IT IS THE ORDINARY CASE. The
    // arrival radius is measured against a spawn row and the creature has feet,
    // so a vendor eight yards off its row is a walk with a few yards left in it
    // rather than an errand that is over. Releasing here is what made the log
    // say "errand done" about a character that could not sell anything.
    if (oneIsNearby)
        return CounterArrival::CloseTheGap;

    // AND NOTHING OF THE ROLE ANYWHERE NEAR IS "NEVER", WHICH IS A DIFFERENT
    // ANSWER AND HAS TO BE. Standing on an empty spawn point waiting for a
    // creature that is despawned, dead or phased is exactly the pin #370
    // refused to place, and it would cost more here than there: the errand's
    // own backstop is twenty minutes, and the run that is waiting for this trip
    // is bounded by the same number. Releasing hands the problem back to the
    // pass that wrote the aim, which can write a different one.
    return CounterArrival::Done;
}

// ------------------- and a hold that is taken once is not a hold (#358) --

bool RetakeTheHold(HeldStillFacts const& facts, float slackYards)
{
    // THE FOUR REFUSALS FIRST, AND EACH OF THEM IS AN ANSWER RATHER THAN A
    // GUARD. Every one is a case where the right thing to do is nothing
    // whatever the distance says, so reading the distance first and treating
    // these as tie-breaks would be the same code with three of its reasons
    // demoted to footnotes.
    if (!facts.present)
        return false;
    if (facts.pastDeadline)
        return false;
    if (facts.walkingOnPurpose)
        return false;
    if (facts.inCombat)
        return false;

    // AND THEN THE ONLY MEASUREMENT. Strictly greater, so a slack of zero
    // means "any drift at all" - which is what a reader expects that word to
    // mean, and is a legitimate thing for a caller to ask for even though the
    // one caller that exists asks for more.
    return facts.driftYards > slackYards;
}

// ------------------ what stopped a cast this module drove at the core (#337) --

char const* CastWallBlocker(CastWallGate const& gate)
{
    // ONE WALL AND NOT A LIST, so a row carries the thing to act on rather than
    // an inventory. The order pairs each wall with the one the core asks in the
    // same breath: flight and the mount are the two answers of a single check
    // (Spell.cpp:6018), and the spell's own cooldown and the global one are the
    // two SPELL_FAILED_NOT_READY (Spell.cpp:5689 and 5711). Flight leads because
    // it is the only one of the seven that also makes the teleport itself
    // impossible, so naming anything else about a character on a taxi would send
    // a reader after the wrong half of the problem.
    if (!gate.grounded)
        return CastWall::InFlight;
    if (!gate.unmounted)
        return CastWall::Mounted;
    if (!gate.standing)
        return CastWall::NotStanding;
    if (!gate.still)
        return CastWall::Moving;
    if (!gate.ready)
        return CastWall::OnCooldown;
    if (!gate.globalReady)
        return CastWall::OnGlobalCooldown;
    if (!gate.free)
        return CastWall::AlreadyCasting;
    return "";
}

char const* HearthStayedDetail(bool castWasSeen, bool castWasQueued)
{
    if (castWasSeen)
        return HEARTH_STAYED_CAST_SEEN;
    if (castWasQueued)
        return HEARTH_STAYED_QUEUED;
    return HEARTH_STAYED_NO_CAST;
}


char const* PartyFlightBlockWord(PartyFlightBlock block)
{
    switch (block)
    {
        case PartyFlightBlock::None:
            return "has nothing in its way";
        case PartyFlightBlock::InCombat:
            return "is in combat";
        case PartyFlightBlock::NotSteerable:
            return "is not a character this module steers";
        case PartyFlightBlock::NoDepartureNode:
            return "has no departure node where it stands";
        case PartyFlightBlock::MasterOutOfReach:
            return "would have to walk as far to a flight master as this errand is "
                   "long enough to fly";
        case PartyFlightBlock::UndiscoveredNode:
            return "has never discovered a node the route needs";
        case PartyFlightBlock::NoRoute:
            return "has no route from its own node to that landing";
        case PartyFlightBlock::TooPoor:
            return "cannot pay its own fare";
    }
    // Unreachable while the enum and this switch agree, and said rather than
    // silent for the reason every other word function here is: a verdict that
    // prints nothing is a verdict nobody can act on.
    return "cannot board for a reason this module has not named";
}

char const* PartyFlightVerdictWord(PartyFlightVerdict verdict)
{
    switch (verdict)
    {
        case PartyFlightVerdict::Fly:
            return "fly";
        case PartyFlightVerdict::WaitForIt:
            return "wait";
        case PartyFlightVerdict::Walk:
            return "walk";
    }
    return "walk";
}

PartyFlightPlan PlanPartyFlight(std::vector<PartyFlightMember> const& members)
{
    PartyFlightPlan plan;

    // EXACTLY ONE LEADER, OR NOTHING FLIES. This is a caller bug rather than a
    // world state - the adapter builds this roster off Group::GetLeaderGUID and
    // knows which character is carrying the errand - and it is refused rather
    // than guessed at because every remaining rule here is written about "the
    // character the others follow". A roster with no leader, or two, has no
    // such character and the safe answer is the one that changes nothing.
    // `blockedBy` stays empty, which is how a caller tells a refused ROSTER
    // from a refused MEMBER.
    unsigned leaders = 0;
    for (PartyFlightMember const& member : members)
        if (member.leader)
            ++leaders;
    if (leaders != 1)
        return plan;   // Walk, the default

    std::vector<std::string> boarding;
    bool permanent = false;
    bool transient = false;
    std::string transientName;

    for (PartyFlightMember const& member : members)
    {
        // WHO IS BEHIND THE LEADER, which is the only population any of this is
        // about. The three exemptions carried over from #138 and the two added
        // by #360, in one expression so they cannot drift apart. The leader
        // itself is exempt from the last of them, because the character
        // carrying the errand is not following anybody by definition and would
        // otherwise exempt itself out of its own flight.
        bool const behind = member.onSameMap && member.alive && !member.inFlight &&
                            !member.atArrival &&
                            (member.leader || member.followingTheLeader);
        if (!behind)
        {
            // ...and the leader is never exempt, because the leader is the one
            // boarding. If the caller handed over a leader that is dead, off
            // the map, already flying or already at the landing, then whatever
            // it is asking, it is not the question this answers. Refuse the
            // roster, again with an empty `blockedBy`.
            if (member.leader)
                return PartyFlightPlan{};
            continue;
        }

        // THE ORDER OF THESE FOUR IS THE ORDER A CHARACTER MEETS THEM, and
        // combat is asked LAST on purpose. A member that is both in a fight and
        // holding no node at all must report the node, because that is the half
        // that will still be true when the fight ends. Asking combat first
        // would print the wolf and hide the reason the party is walking.
        PartyFlightBlock block = PartyFlightBlock::None;
        std::uint32_t node = 0;
        if (!member.steerable)
            block = PartyFlightBlock::NotSteerable;
        else if (!member.hasDepartureNode)
            block = PartyFlightBlock::NoDepartureNode;
        else if (!member.masterInReach)
            block = PartyFlightBlock::MasterOutOfReach;
        else if (!member.routeKnown)
        {
            // An undiscovered node and a missing route are the same refusal to
            // the executor and completely different work to whoever reads the
            // log: one of them names a flight master somebody can be sent to.
            block = member.undiscoveredNode ? PartyFlightBlock::UndiscoveredNode
                                            : PartyFlightBlock::NoRoute;
            node = member.undiscoveredNode;
        }
        else if (!member.canPayFare)
            block = PartyFlightBlock::TooPoor;
        else if (member.inCombat)
            block = PartyFlightBlock::InCombat;

        if (block == PartyFlightBlock::None)
        {
            boarding.push_back(member.name);
            continue;
        }
        if (block == PartyFlightBlock::InCombat)
        {
            if (!transient)
            {
                transient = true;
                transientName = member.name;
            }
            continue;
        }
        // FIRST PERMANENT BLOCKER IN ROSTER ORDER WINS THE REPORT, and the loop
        // carries on rather than returning: the verdict is already settled, and
        // finishing the sweep is what makes the answer independent of the order
        // the caller happened to build the roster in.
        if (!permanent)
        {
            permanent = true;
            plan.blockedBy = member.name;
            plan.block = block;
            plan.blockedNode = node;
        }
    }

    // A PERMANENT BLOCKER BEATS A TRANSIENT ONE. Waiting out a fight only to
    // refuse afterwards is a party standing still for no reason, so the walk
    // starts now.
    if (permanent)
    {
        plan.verdict = PartyFlightVerdict::Walk;
        return plan;
    }
    if (transient)
    {
        plan.verdict = PartyFlightVerdict::WaitForIt;
        plan.blockedBy = transientName;
        plan.block = PartyFlightBlock::InCombat;
        return plan;
    }

    // Everybody behind the leader can board, which includes the case where
    // nobody is behind it at all: a lone character, or one whose whole party is
    // dead, off the map or already in the air, boards on its own exactly as it
    // did before this existed.
    plan.verdict = PartyFlightVerdict::Fly;
    plan.boarding = std::move(boarding);
    return plan;
}

}  // namespace OverseerDecisions
