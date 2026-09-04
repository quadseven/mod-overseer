/*
 * mod-overseer's pure decisions, in a file something can actually include.
 *
 * The predicates below were written to be testable and said so in their own
 * comments, which are reproduced verbatim underneath: "KEPT FREE OF EVERY CORE
 * TYPE ON PURPOSE ... it can be exercised directly by a unit test with no
 * world, no bot, and no database". That was true of the code and false of the
 * build. They were private static members of OverseerWorldScript, inside
 * src/mod_overseer.cpp, a translation unit with no header of its own - so
 * there was nothing for a test to include and nothing for it to link against,
 * and the only thing exercising them was a text check asserting that the
 * source of these functions contains no `->`. That check is worth having and
 * it is not a test of what they decide. A seam nothing can reach is a comment
 * about a seam.
 *
 * So they live here instead, as free functions in a namespace, in a pair of
 * files that include NOTHING from AzerothCore or from mod-playerbots: <string>
 * and <vector>, and nothing else, on purpose. That is the property worth
 * protecting, and it is the reason this is a separate file rather than another
 * region of the big one. A test - or a reader - can compile these two on their
 * own, and anything that would drag a core type in here has to fail to build
 * rather than quietly end the arrangement.
 *
 * WHAT DID NOT CHANGE: what any of these functions decides. The bodies are the
 * originals and the comments are the originals; only their indentation moved
 * with them, and `static` became namespace scope so a caller outside this file
 * can name them.
 *
 * WHAT ELSE IS IN HERE. Anything this module decides that needs nothing from
 * the world belongs in this pair of files, not only the dungeon-run predicates
 * that started it. The ratchet at the bottom is the second tenant: the "has it
 * got anywhere, and if not, give up" rule that four separate drives were each
 * carrying their own copy of.
 *
 * AzerothCore's module build globs modules/<module>/src for sources and adds
 * that directory to the include path (CollectSourceFiles and
 * CollectIncludeDirectories, both driven from the core's own
 * modules/CMakeLists.txt), so a second .cpp beside mod_overseer.cpp is
 * compiled and `#include "overseer_decisions.h"` resolves with no build file
 * of this module's own. This module has no CMakeLists.txt and does not need
 * one.
 */

#ifndef MOD_OVERSEER_DECISIONS_H
#define MOD_OVERSEER_DECISIONS_H

#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace OverseerDecisions
{

// THE MODULE'S OWN VERSION, and the reason it lives in this header rather than
// in mod_overseer.cpp: this file is the one part of the module that compiles
// on its own, with no AzerothCore include path (see check.decisions.yml, which
// exists to enforce exactly that). So a test, a tool, or the module itself can
// read the version without dragging a core in behind it.
//
// KEPT IN STEP WITH THE `VERSION` FILE AT THE REPOSITORY ROOT, which is the
// human-facing source of truth and what a release is cut from. Two copies of a
// version string is exactly the kind of thing that drifts silently, so the
// decisions workflow compares them and fails if they disagree. Change both, or
// change neither.
//
// WHY 0.x AND NOT 1.0.0. Semver's promise at 1.0 is a stable public interface.
// This module's public interface is its database schema, which the Overseer
// site reads, and its command surface. Both moved this week: overseer_goal and
// overseer_dungeon_run are new, overseer_roster gained the professions columns,
// and a site built against the newer schema returned 503 against an older
// worldserver. Claiming 1.0.0 today would promise a stability the schema
// demonstrably does not have yet. It goes to 1.0.0 when the schema stops
// moving under the site, and that is a real event worth waiting for.
constexpr char VERSION[] = "0.1.0";


// IS A LIVING CHARACTER UNDER A FLOOR IT CANNOT WALK ONTO? (#174)
//
// A wall check can refuse the step that leaves the walkable world, but it says
// nothing about a character already below geometry. The measured failure was
// an entire party alive and moving at z 59-61 while the city surface above it
// was around z 95. Horizontal movement remained possible on the raw terrain
// beneath the city, so neither a death recovery nor a stall could notice.
//
// THE NAVMESH IS THE FALSE-POSITIVE GUARD. A cave, cellar or building may
// legitimately have another surface well above the character. If its current
// position still belongs to the local walkable mesh, it is an interior, not a
// recovery candidate. Conversely, the raw terrain hidden beneath a city WMO
// has no walkable polygon at the character's height.
//
// `surfaceValid` is separate from the number because the core has two invalid
// height sentinels. Invalid data grants no permission to move a character.
// The boundary is inclusive so a declared thirty-yard gap means exactly that,
// rather than thirty yards plus one floating-point step.
bool BelowTerrainNeedsRecovery(float currentZ, float surfaceAboveZ,
                               bool surfaceValid, bool hasLocalNavmesh,
                               float minimumGap);


// WHAT A REALM SAYS ABOUT ITSELF (mod-overseer#184).
//
// THE PROBLEM, STATED AS THE OPERATOR STATES IT. Three realms run this module:
// a live one, a disposable one, and a small hardcore one. They do not run the
// same build. Today one of them is on an AzerothCore twelve days older than the
// other two, and nothing outside a container log says so. Which realm a reader
// is looking at has, until now, been carried entirely by the hostname the page
// came from - and that distinction is being retired. When it goes, a page with
// no other label is a page that will eventually show the live family's
// positions under a promise that it is not the live family.
//
// So the page has to become the label, and this is the part of that the module
// owns: the realm says who it is and what it is running, into its own database,
// and the site renders what it finds there. A passive reader and a self-
// reporting world, rather than a reader that has to be told out of band.
//
// WHY THE COMPOSITION IS HERE AND NOT IN mod_overseer.cpp. Everything below is
// a decision about strings - which facts go in the report, what a declared
// realm kind is allowed to mean, whether a declared commit describes this
// binary. None of it needs a world, a player or a database, and all of it is
// the kind of thing that is wrong in a way no compile catches. Here it is
// reachable by tests/test_build_report.cpp with no core behind it. What stays
// in mod_overseer.cpp is the part that genuinely cannot move: reading the
// environment, asking GitRevision, and writing the rows.

// The standing of a reported fact. Not decoration: a reader that treated all
// three alike would overstate what is actually known. See the table comment in
// data/sql/characters/base/2026_09_03_00_overseer_build.sql.
constexpr char SOURCE_COMPILED[] = "compiled";  // read out of this binary
constexpr char SOURCE_DECLARED[] = "declared";  // handed in by the deployment
constexpr char SOURCE_DERIVED[]  = "derived";   // this module's own verdict

// The environment variables a deployment may set for this module, named here so
// the writer and its tests cannot disagree about them.
//
// WHY `OVERSEER_` AND NOT THE `AC_` PREFIX THE PINS FILE USES. AzerothCore's
// ConfigMgr derives an environment variable name from every one of its own
// config keys by a camelCase-aware transform and reads whatever matches. An
// `AC_`-prefixed name of our own is at best ignored and at worst collides with
// a key nobody was thinking about, and the failure mode of that collision is
// silent. Our own prefix cannot collide with theirs.
//
// EVERY ONE OF THESE IS OPTIONAL. A deployment that sets none of them still
// gets a report - a thinner one, saying what the binary knows about itself and
// admitting it was told nothing else. That is the honest answer, and it is also
// what every realm will produce on the first start after this ships, because
// the manifests that set these are a separate change on a separate cadence.
constexpr char ENV_REALM[]              = "OVERSEER_REALM";
constexpr char ENV_REALM_KIND[]         = "OVERSEER_REALM_KIND";
constexpr char ENV_PIN_CORE[]           = "OVERSEER_PIN_CORE";
constexpr char ENV_PIN_PLAYERBOTS[]     = "OVERSEER_PIN_PLAYERBOTS";
constexpr char ENV_PIN_OLLAMA_CHAT[]    = "OVERSEER_PIN_OLLAMA_CHAT";
constexpr char ENV_PIN_DUNGEON_CLEAR[]  = "OVERSEER_PIN_DUNGEON_CLEAR";
constexpr char ENV_PIN_AH_BOT[]         = "OVERSEER_PIN_AH_BOT";

// The three answers to "is this the live world". THERE ARE THREE ON PURPOSE,
// and the third is the whole safety argument.
//
// A binary question would force every realm that has not said anything into one
// of the two real answers, and both choices are wrong. Defaulting to
// non-production is the accident this feature exists to prevent: an unlabelled
// live realm would render as safe. Defaulting to production would put a
// production banner over the disposable realm and the canary, so the warning
// would be false two times out of three and would be trained away within a
// week - which is the same failure with a longer fuse.
//
// So a realm that has not been told, or has been told something this module
// does not recognise, reports UNKNOWN, and the site renders unknown as an
// alarm rather than as either answer. A typo in a manifest becomes a visible
// question instead of a confident lie.
constexpr char REALM_PRODUCTION[]     = "production";
constexpr char REALM_NON_PRODUCTION[] = "non-production";
constexpr char REALM_UNKNOWN[]        = "unknown";

// Whether the declared upstream pins actually describe this binary.
constexpr char PINS_MATCH[]   = "match";
constexpr char PINS_STALE[]   = "stale";
constexpr char PINS_UNKNOWN[] = "unknown";

// One line of a realm's report about itself.
struct BuildFact
{
    std::string name;
    std::string value;
    std::string source;
};

// The declared realm kind, reduced to one of the three answers above.
//
// Case and surrounding whitespace are forgiven because a YAML value picks both
// up for free. NOTHING ELSE IS. "prod", "PRODUCTION " and "Production" all
// arrive as production; "prd", "live" and "" all arrive as unknown, which is
// the alarm, not the safe answer. Widening this set is a deliberate act - every
// spelling added here is a spelling that can be typed into a manifest and
// believed.
std::string RealmKind(std::string const& declared);

// The commit AzerothCore prints for itself, pulled out of the sentence
// GitRevision::GetFullVersion() returns:
//
//   "AzerothCore rev. 47960183bb03+ 2026-08-28 21:04:11 +0200 (HEAD branch)
//    (Unix, RelWithDebInfo, Static)"   ->   "47960183bb03"
//
// The trailing `+` means the tree had local modifications at build time, which
// is always true here because the build applies this repo's patches. It is not
// part of the commit and is dropped. Empty if the string is not in that shape,
// which is the honest answer for a core that changes its banner one day.
std::string CoreRevision(std::string const& coreVersion);

// DOES THE DEPLOYMENT'S DECLARATION DESCRIBE THIS BINARY, and this is the check
// that makes the declared rows safe to publish at all.
//
// The upstream commits of mod-playerbots, mod-ollama-chat, mod-dungeon-clear
// and mod-ah-bot-plus are compiled in and then unreachable: there is no symbol
// to ask, so the only way they reach the page is for the deployment to say what
// it built. A declaration can be stale - a manifest that names today's pins in
// front of an image built weeks ago declares the wrong SHAs with total
// confidence, and a page that printed them would be worse than a page that
// printed nothing, because it would look authoritative.
//
// The core commit is the one declared fact that CAN be checked, because the
// core also reports itself. So it is used as the witness for all of them: if
// the declared core commit is not the core actually running, the declaration as
// a whole was written for a different image and every SHA in it is suspect.
//
// Returns PINS_MATCH, PINS_STALE, or PINS_UNKNOWN when either side is missing
// or not in a shape that can be compared. Unknown is not a failure - it is what
// a realm that was told nothing correctly reports.
std::string PinsVerdict(std::string const& coreVersion,
                        std::string const& declaredCoreSha);

// The whole report, in the order a reader would want it.
//
// `coreVersion` is GitRevision::GetFullVersion(). `env` maps the names above to
// their values, with anything the deployment did not set simply absent - so a
// caller reads the environment once and this stays testable.
//
// TWO ROWS ARE ALWAYS PRESENT AND THE REST ARE NOT, which is deliberate.
// `realm_kind` and `pins` are always written, including when the answer is
// "unknown", because their absence and their unknown mean different things to a
// reader and it must be able to tell them apart: no row at all means this realm
// has never reported, and that is a fact about the realm rather than about its
// configuration. Everything else is omitted when it was not declared, because
// an empty string pretending to be a commit is worse than a gap.
std::vector<BuildFact> BuildReport(std::string const& coreVersion,
                                   std::map<std::string, std::string> const& env);


// THE BARRIER PREDICATE, KEPT FREE OF EVERY CORE TYPE ON PURPOSE. Nothing
// here touches Player, Map, or PlayerbotAI - it is fed plain facts the
// caller already gathered, so it can be exercised directly by a unit test
// with no world, no bot, and no database, and so a change to how the facts
// are gathered can never also silently change what BARRIER requires.
//
// ALL THREE CONDITIONS ARE FROM THE EPIC, VERBATIM: "hold until ALL are
// within ~10y, alive, and out of combat." Fails closed: an empty roster or
// any member this poll could not even find (a name that resolved to
// nobody, or a distance never measured because the character is on a
// different map) reads as barrier-not-met, never as vacuously met -
// exactly the "geography is necessary but not sufficient" lesson
// InDungeonRun above already had to learn once.
struct DungeonRunMemberState
{
    std::string name;
    bool seen{false};              // false = not found in the world this poll
    bool alive{false};
    bool inCombat{false};
    float distanceFromStage{-1.f}; // negative = not measured (wrong map, or !seen)
    // ALREADY THROUGH THE DOOR THIS BARRIER IS WAITING OUTSIDE (#165). Measured
    // live: a run sat `active` and unstaged for forty-three minutes while its
    // barrier line read "Ugga (not seen)" and, on the run before, "Ugga (wrong
    // map)". She was neither. She was inside the instance, having walked
    // through early, and the barrier was waiting for her to arrive at a place
    // she had gone past.
    //
    // THIS IS DELIBERATELY NOT A FOURTH WAY TO SATISFY THE BARRIER, and the
    // predicate below ignores it on purpose. Whether a member who is already
    // through counts as staged, or whether nobody may cross until the barrier
    // opens, is #165's own question and a choice between two defensible
    // answers. Naming the state correctly is a prerequisite for either and is
    // neither: what it buys today is a blockers line that cannot be misread,
    // and a give-up that can say what was actually wrong.
    bool inside{false};
};

bool DungeonRunBarrierMet(std::vector<DungeonRunMemberState> const& members,
                          float radiusYards);

// Why a member is failing BARRIER, for the one log line BARRIER prints
// while it waits. Kept separate from the predicate above so the predicate
// itself stays a plain bool with nothing to format - a pure function that
// also builds strings is a pure function that is harder to test twice.
std::string DungeonRunBarrierBlockers(std::vector<DungeonRunMemberState> const& members,
                                      float radiusYards);

// THE CROSSING PREDICATES, KEPT FREE OF EVERY CORE TYPE FOR THE SAME REASON
// THE BARRIER ONE IS. Nothing below touches Player, Map or PlayerbotAI, so
// "when may the party be knocked through" can be exercised by a unit test
// with no world, and a change to how the facts are gathered can never
// silently change what a crossing requires.
//
// ONE SHAPE FOR BOTH DIRECTIONS. `through` means "on the far side of this
// door", which for ENTER is the instance map and for EXIT is the map
// outside it. ENTER and EXIT differ in which trigger and which far side,
// and in nothing else, so they share these predicates rather than owning a
// copy each - a party that can get in and cannot get out is a worse failure
// than one that never went in, and two copies is how the second one rots.
//
// WHY THIS IS NOT DungeonRunMemberState WITH A DIFFERENT CENTRE. A member
// that is ALREADY THROUGH is on another map, which to the barrier predicate
// reads as "wrong map" and therefore as not-met - the one state a crossing
// most needs to distinguish would have been indistinguishable from failure.
// Being through is a third answer, not a bad distance, so it is a field of
// its own.
struct DungeonRunEntryState
{
    std::string name;
    bool seen{false};             // false = not found in the world this poll
    bool alive{false};
    bool inCombat{false};
    bool through{false};          // already on the far side of the door
    float distanceFromDoor{-1.f}; // negative = not measured (through, wrong map, or !seen)
};

// Is every member either already through, or standing on the doorstep alive
// and out of combat? Fails closed on an empty roster and on any member this
// poll could not find, exactly as the barrier predicate does and for the
// same reason: a knock for a party that is not all there is the tank
// entering alone with extra steps.
bool DungeonRunEntryReady(std::vector<DungeonRunEntryState> const& members,
                          float doorstepYards);

// Is the crossing finished? Separate from the readiness predicate above
// because "everybody is through" and "everybody may be knocked" are
// different questions with different answers on every poll in between, and
// a single function answering both would have to be asked which it meant.
bool DungeonRunAllThrough(std::vector<DungeonRunEntryState> const& members);

// Why a member is not through yet, for the one line ENTER prints while it
// waits. Kept out of the predicates for the reason DungeonRunBarrierBlockers
// already gives: a pure function that also builds strings is a pure function
// that is harder to test twice.
std::string DungeonRunEntryBlockers(std::vector<DungeonRunEntryState> const& members,
                                    float doorstepYards);

// ------------------------------------------------------------- the ratchet --
//
// "HAS IT GOT ANYWHERE, AND IF NOT, FOR HOW LONG?" - a question this module
// asks in five places, and one that was written out five times, with five
// clocks and five constants, before it was written once here.
//
// WHY THE MEASUREMENT MATTERS MORE THAN THE CLOCK. This was learned expensively
// on the travel backstop (#63), and that backstop's own comment is the argument
// for all of them. It exists to catch a character STANDING STILL, and it used
// to approximate that as "taking a while" - which is a different thing, and
// wrong in the one case that matters most. Measured on the dev world: a
// character aimed at the Deadmines portal from Elwynn was released 586 yards
// short, having walked 2347 of 2933 yards at 112 yards a minute, about five
// minutes from arriving. It was released as UNREACHABLE while it was visibly
// reaching it, and the log said so in those words. Nothing was stuck; the
// journey was simply longer than a constant that had only ever been asked about
// trainers in the same city.
//
// So ask the question a backstop is actually for. The best reading only ever
// ratchets one way, so beating it means the subject has done something it has
// not managed before on this errand - which no bot jammed against scenery,
// circling, or standing in a field can keep doing, and which a walking bot does
// on every poll. A target that truly cannot be reached still gets given up on:
// the character closes to whatever range it can manage, stops improving, and
// the clock then runs out undisturbed. The bound is on being stuck, where it
// belongs, rather than on distance or on patience alone.
//
// WHAT IS SHARED, AND WHAT IS DELIBERATELY NOT. Shared: the comparison, the
// mark it is made against, the clock that restarts when the mark is beaten, and
// the verdict when it has not restarted for long enough. Not shared, and left
// at each site: what to DO about a stall. Travel releases the errand, a
// crossing gives up on the door, a stalled follower has its movement generator
// cleared and may be nudged again later, a quest that went nowhere collects a
// strike. Those are four reactions to one fact, and they are the only part that
// was ever really different.
//
// THE READINGS ARE NOT ALL THE SAME EITHER, so that is a parameter below rather
// than an average. One caller measures how near it has got to something it was
// sent to; one counts things that have already happened; two measure how far
// they have moved from a mark dropped where they were last seen going
// somewhere. Three rules, three different answers to the same numbers, and all
// three named.
//
// ONE OF THE FIVE IS NOT HERE, ON PURPOSE. The quest-aim backstop
// (DRIVE_AIM_BACKSTOP_SECONDS) has no measurement to ratchet: its progress is
// "the roster names a different quest", an identity rather than a distance, and
// its clock is additionally carried forward across a travel hand-back by an
// amount only that drive knows. It is a plain deadline and it stays one.
// Handing it a distance it does not have, so that this list could read evenly,
// would be inventing a rule rather than sharing one.

// WHAT A READING MEANS. Named for what the number IS rather than for a
// direction, because the meaning is what differs between the sites and the
// direction follows from it.
enum class RatchetReading
{
    // A DISTANCE TO SOMETHING THE SUBJECT IS TRYING TO REACH. Progress is
    // getting NEARER than it has ever been, by more than `margin`, so the mark
    // only ever falls. A mark of ZERO means no reading has been taken yet and
    // never "already arrived": the caller measures with
    // WorldObject::GetDistance2d, which clamps at zero once the subject is
    // within its own size of the target, and it settles arrival before it asks
    // this.
    DistanceToTarget,

    // A COUNT OF THINGS THAT HAVE ALREADY HAPPENED. Progress is a bigger count
    // than has ever been seen. Zero is a REAL reading here (nothing has
    // happened yet) rather than an unset one, and that is the whole difference
    // from the distance above: the first poll of a crossing nobody has
    // crossed yet is not progress, so the clock its caller started when the
    // phase began is left running rather than restarted.
    CountAchieved,

    // A DISTANCE FROM A MARK THE CALLER MOVES to wherever the subject now is.
    // The subject is not going anywhere in particular and there is nothing to
    // get nearer to; the question is whether it has got anywhere AT ALL since
    // the mark was dropped. Any reading past `margin` counts, and the mark goes
    // back to nothing rather than to the reading, because the next reading is
    // measured from the new mark and starts from zero again.
    DistanceFromLastMark,
};

// One site's whole rule, in one constant a reader can take in at once.
struct RatchetLimits
{
    RatchetReading reading{RatchetReading::DistanceToTarget};
    float margin{0.f};  // how much better a reading has to be before it counts
    // How long without progress is long enough. ZERO MEANS THE CALLER COUNTS,
    // and one caller does: the quest re-pick's patience is three consecutive
    // picks that went nowhere, not a number of minutes, so it asks
    // RatchetProgressed below and keeps its own count rather than growing a
    // timer it never had just to be able to use the whole function.
    time_t patienceSeconds{0};
};

// What one subject's ratchet remembers between polls. Every site that keeps one
// keeps it inside its own per-character state, world-thread only and unguarded,
// and loses it on a restart - which costs one restarted clock and no
// correctness, exactly as each of those states already documented for itself.
struct RatchetState
{
    float best{0.f};  // the best reading so far, in the sense named above
    time_t since{0};  // when `best` was last beaten
};

struct RatchetVerdict
{
    bool progressed{false};  // this reading beat the mark; the clock restarted
    bool stalled{false};     // no progress for longer than `patienceSeconds`
};

// Does this reading count as progress? The comparison half of Ratchet on its
// own, for the site whose patience is counted in tries rather than in seconds
// and which therefore has no clock to keep. Pure in the strongest sense: it
// changes nothing and reads nothing but its arguments.
bool RatchetProgressed(float reading, float best, RatchetLimits const& limits);

// The whole thing: compare, then either restart the clock or say how long it
// has been running. A poll that progressed is never also stalled - it has just
// restarted the clock - so the two verdicts read as the alternatives they are.
RatchetVerdict Ratchet(RatchetState& state, float reading, time_t now,
                       RatchetLimits const& limits);

// -------------------------------------------------- the staging watchdog --
//
// "IT IS FAR AWAY" AND "IT IS NOT COMING" ARE DIFFERENT FACTS, and the
// dungeon-run barrier could only ever say the first one. It prints the gap for
// every member on every poll it holds - "Grog (549y away)" - and a character
// walking in from 549 yards away and a character standing at a herb node 549
// yards away produce the identical line. That is narration. What the operator
// asked for is the second fact and an action to go with it, so that a run that
// cannot start is fixed by the module rather than by somebody watching a
// stream.
//
// THE SECOND FACT IS THE RATCHET ABOVE, ASKED WITH A DISTANCE. Is the gap
// closing? A member that keeps beating its own best distance is walking,
// however far out it still is; one that has not beaten it for long enough is
// stalled, whatever the gap says. That is the whole of the detection and it is
// not a new mechanism - it is the one four other drives already share.
//
// THE ACTION IS A LADDER, NOT A VERDICT, because the three things that can be
// wrong with a staged character are different and they are ordered by how much
// putting them right disturbs it:
//
//   1. SOMETHING IS STEERING IT. A strategy that walks the character somewhere
//      of its own choosing is back on its engine - which is measured, not
//      hypothetical, and is what the escort's own strategy set exists to stop.
//      Re-asserting that set moves nothing and costs nothing, so it is first.
//   2. THE WALK WAS LOST. The aim is still on the roster row and the character
//      is not acting on it. Re-issuing costs one restarted spline.
//   3. THE MOVEMENT GENERATOR IS JITTERING WITHOUT ARRIVING. This is the case
//      KeepRosterFollowing measured (#70) and already answers with
//      MotionMaster::Clear(), and it is last because it throws away whatever
//      the character was in the middle of doing.
//
// AND IT IS BOUNDED, because a self-correcting mechanism that never gives up is
// a loop with a log line in it. After the last rung the character is declared
// unstageable ONCE and this stops touching it; the run's own bounds - the
// travel errand's twenty-minute unreachable backstop, and the operator - own it
// from there. Any poll that shows real progress puts the whole ladder back to
// the bottom, so a character that recovers is watched from scratch rather than
// from the rung its last bad patch reached.
enum class StagingNudge
{
    Nothing,        // walking, staged, or nothing worth reading this poll
    Restrategy,     // re-assert the escort's own strategy set
    Reaim,          // re-issue the aim
    ClearMovement,  // discard the movement generator, as the follow stall does
    GiveUp,         // say once that it cannot be staged, and stop
};

// How many rungs there are below GiveUp. Named so the bound is a number a
// reader can see rather than a switch they have to count.
constexpr unsigned STAGING_NUDGE_STEPS = 3;

// What one member's watchdog remembers between polls. Kept inside the
// coordinator's own run state, world-thread only and unguarded, and lost with
// the run on a restart - which costs one restarted clock and no correctness,
// exactly as every other per-character state in this module already documents.
struct StagingStallState
{
    RatchetState progress;
    unsigned escalated{0};  // how many rungs have been climbed
    bool gaveUp{false};     // the give-up line has been said
};

// One member, one poll. `measurable` is false when this poll's distance is not
// a reading ABOUT WALKING - the member is in combat, in the air, dead, or on
// another map - and the clock is then held rather than run, for the reason the
// travel drive already holds its own over a flight: half a taxi route goes the
// wrong way round a mountain, and a fight is a pause rather than a stall.
StagingNudge StagingWatchdog(StagingStallState& state, float distanceFromStage,
                             bool measurable, time_t now,
                             RatchetLimits const& limits);
// ------------------------------------------------------------- professions --
//
// THE THIRD TENANT, AND THE SAME REASON AS THE OTHER TWO. The roster declares
// what professions a character should END UP holding
// (overseer_roster.professions), and something has to work out what the next
// step towards that is. That arithmetic needs no world at all: it is a declared
// set, a held set, and the ceiling of two primaries the core enforces - so it
// belongs here, where a test can reach it, rather than three ifs deep inside a
// poll that needs a live Player before it will run.
//
// WHY THIS PARTICULAR DECISION IS WORTH PULLING OUT. Giving up a primary
// profession is the only thing this module does that CANNOT BE UNDONE: it
// destroys every point of the skill and every recipe hanging off it. The
// acceptance criterion most likely to be got wrong is therefore also the one
// that costs the most when it is - re-running the assignment must not unlearn
// and relearn what is already correct - and "already correct" has to be
// something a person can read and a test can pin, not a condition that is only
// true by accident of the order two polls happened to run in.

// One primary profession a character actually holds, and what giving it up
// would cost. The value travels WITH the skill rather than being looked up
// again later, because it is the price: it is the number of points that stop
// existing, and a price fetched at a different moment from the decision it
// prices is how a stale one gets paid.
struct ProfessionHolding
{
    unsigned skill{0};
    unsigned value{0};
};

enum class ProfessionStepKind
{
    // NOTHING TO DO, AND THIS IS THE COMMON ANSWER. A character already
    // holding what the roster asked for gets this on every poll, forever, and
    // is therefore never touched by any of it. That is the idempotence, and it
    // is a property of this enum's first value rather than of a guard
    // somewhere downstream.
    Nothing,

    // Give up `skill`, destroying `cost` points, because every slot is full
    // and the roster wants something this character does not hold.
    GiveUp,

    // Take `skill`: a slot is free and the roster wants it.
    Take,
};

struct ProfessionStep
{
    ProfessionStepKind kind{ProfessionStepKind::Nothing};
    unsigned skill{0};
    unsigned cost{0};  // GiveUp only: the skill value this destroys
};

// The next step from what a character HOLDS towards what the roster SAYS.
//
// THE RULES, IN THE ORDER THEY ARE APPLIED, AND WHY EACH IS THE WAY ROUND IT
// IS.
//
//   1. AN EMPTY `wanted` IS "NO OPINION", NOT "HOLD NOTHING". A character
//      nobody has decided about is not one that may be freely rearranged; the
//      safe reading of an absent decision is to leave it exactly as it is. The
//      column comment in 2026_08_26_00_overseer_roster_professions.sql says
//      the same thing about the same value, and this is where it is enforced.
//
//   2. NOTHING MISSING MEANS NOTHING TO DO, AND IT IS ASKED FIRST. This is the
//      idempotence, and it is deliberately NOT written as `held == wanted`: a
//      character that also holds something the roster has no opinion about is
//      left alone rather than tidied up. Only the MISSING half of the
//      comparison can ever cause anything to happen, so the only way to make
//      this function destroy something is to ask for something it does not
//      have.
//
//   3. A FREE SLOT IS FILLED BEFORE ANYTHING IS DESTROYED. Giving something up
//      is only ever a way of making room, so it cannot be the answer while
//      there is room. This is what stops a character holding one profession
//      and one empty slot losing the profession it has - and it is what makes
//      the whole sequence just-in-time: every GiveUp this returns is followed
//      by a Take into the slot it opened, so nobody is left standing around
//      holding nothing at all.
//
//   4. GATHERING BEFORE CRAFTING while both are still missing. A craft with no
//      supply is a skill that sits at 1/75, which is the failure the whole
//      issue is about, so mining is taken before blacksmithing and skinning
//      before leatherworking. It has to be said explicitly because the ids
//      sort the wrong way round for both of those pairs (164 blacksmithing
//      before 186 mining, 165 leatherworking before 393 skinning), so "take
//      them in id order" would get the supply chain backwards every time.
//
//   5. THE CHEAPEST THING THE ROSTER DOES NOT WANT IS WHAT GOES, and something
//      the roster DOES want is never a candidate however cheap it looks. That
//      second half is not an optimisation. It is what makes a character
//      already holding its assigned pair unreachable by the destructive branch
//      at all, rather than merely unlucky enough not to be picked.
//
// A NOTHING WITH SOMETHING STILL MISSING IS A REAL ANSWER, and the caller can
// tell it from rule 2's Nothing by asking `wanted` again: it means every slot
// is full of skills the roster ALSO wants, so the declared end state does not
// fit in `maxPrimary` and no amount of polling will change that. Nothing is
// destroyed in that case, which is the right direction to fail in for a plan
// nobody can satisfy.
ProfessionStep NextProfessionStep(std::vector<unsigned> const& wanted,
                                  std::vector<ProfessionHolding> const& held,
                                  unsigned maxPrimary);

// ------------------------------------------------------ the give backoff --
//
// "IT DID NOT WORK, AND IT WILL NOT WORK TWO SECONDS FROM NOW EITHER."
//
// The command queue is drained every couple of seconds and the sender is free
// to re-insert a give it has not seen succeed, so a give that cannot succeed
// is not attempted once - it is attempted for as long as the condition lasts.
// Measured on the dev world for mod-overseer#169: 31 rows of one identical
// refusal, the oldest of them hours old, four characters, three items, and
// nothing about any of it changing between one attempt and the next.
//
// The cost is not the work. It is that every one of those attempts is a fresh
// failure with a fresh answer, and everything downstream that reacts to an
// answer reacts again - which is how one blocked hand-over became a line of
// party chat every two seconds for an evening.
//
// SO: REMEMBER THE WALL, AND STOP WALKING INTO IT. A refusal buys a pause. A
// give asked again inside that pause is answered from the memory instead of
// being retried, and the reason is printed only when it is NEW, so the log
// says what is wrong once rather than nine hundred times an hour. The pause is
// short by design: this is a backoff and not a give-up, so the wall is re-
// tested on a cadence a person would use rather than on the queue's.
//
// KEPT FREE OF EVERY CORE TYPE, like everything else in this pair of files, so
// the rule can be exercised with no world, no bot, and no database - which for
// a rule whose whole content is "what happened LAST time, and how long ago" is
// the only way to test it at all.

// One refused give, remembered.
struct GiveRefusal
{
    std::string reason;  // the detail the last attempt returned
    time_t since{0};     // when that attempt was made; 0 = no memory at all
};

// The refusals collected against ONE RECEIVER, keyed by the give that earned
// them. Grouped by receiver rather than kept flat because that is the unit
// that gets forgotten: what these refusals are usually about is how much room
// the receiver has, so the moment anything DOES reach him, every one of them
// is a stale answer to a question whose facts just changed.
using GiveRefusalBook = std::map<std::string, GiveRefusal>;

// Is this give still inside the pause its last refusal bought? Writes the
// remembered reason into `reason` when it is, so the caller can answer the row
// with the same detail it would have produced by trying again.
//
// Sweeps as it goes: any entry older than `forgetSeconds` is erased, because a
// refusal that outlives its reason is its own bug (the lesson
// WithinHandbackGrace in mod_overseer.cpp already carries) and because a book
// nothing ever removes from is a leak with a slow fuse.
bool GiveHeldOff(GiveRefusalBook& book, std::string const& key, time_t now,
                 time_t backoffSeconds, time_t forgetSeconds, std::string& reason);

// Record a refusal against `key`, and answer whether it is worth SAYING: true
// the first time a reason is seen, false for every repeat of it. The clock is
// restarted either way - that is what makes the next few polls cheap - so
// "say it once" and "try it rarely" stay two separate answers.
bool NoteGiveRefusal(GiveRefusalBook& book, std::string const& key,
                     std::string const& reason, time_t now);

// ------------------------------------------------------------- gear (#145) --
//
// WHAT AN ITEM IS WORTH TO ONE CHARACTER, AND WHETHER IT MAY WEAR IT AT ALL.
//
// THE TWO DEFECTS THIS ANSWERS, BOTH MEASURED ON THE LIVE FAMILY. A level 27
// warrior tank was wearing a CLOTH robe, LEATHER boots and a LEATHER belt
// alongside four pieces of mail, and a mail boot of item level 19 carrying 113
// armour was not preferred over a leather boot of item level 22 carrying 56 -
// twice the mitigation, passed over because the comparison came down to item
// level. Separately, a level 22 priest is carrying leather boots and leather
// gloves she can never put on, four item levels above the cloth she wears.
//
// WHY IT IS HERE AND NOT IN mod_overseer.cpp. Same reason as everything else
// in this pair of files: a decision that needs nothing from the world is a
// decision a test can reach. This one needs it more than most - it is a
// weighting table, and a weighting table nothing can exercise is a weighting
// table nobody will ever dare change (#134, #116).
//
// ---------------------------------------------------------------------------
// THE WEIGHTING, AND WHY IT IS THIS AND NOT SOMETHING ELSE
// ---------------------------------------------------------------------------
//
// EVERY TERM IS IN ARMOUR POINTS. The score is "how many points of armour this
// piece is worth to this character", so the armour term needs no conversion at
// all and every other term is stated as its exchange rate against it. That is
// what makes the numbers below arguable rather than arbitrary: a reader can
// say "two stamina is not worth four armour to a tank" and be talking about
// something real.
//
// THERE IS NO SEPARATE "WRONG ARMOUR CLASS" PENALTY, ON PURPOSE. It would be a
// constant somebody chose. The armour VALUE already is the penalty, in the
// units the tank actually cares about: the cloth robe measured on the tank
// carries 38 armour where mail on the same character's legs carries 168, and
// that gap is a fact about the two items rather than a number invented to
// express a preference. What the score must not do - and what the upstream one
// does - is multiply the whole weight through by item level afterwards, which
// is how a low-armour piece of the wrong class wins on freshness alone.
// (Upstream's calculator has an armour-type penalty written and commented out,
// mod-playerbots StatsWeightCalculator.cpp:631-636, with the helper it would
// have called, NotBestArmorType, still compiled at :724 and reachable from
// nowhere; the multiply-by-item-level is at :120-128, and the armour stat's
// own weight is 0.001 at :266.)
//
// ITEM LEVEL SURVIVES ONLY AS A TIEBREAK, half a point per level. It is a
// proxy for everything not modelled here - a proc, a set bonus, a resistance -
// and a proxy is worth having as a nudge and disastrous as the whole answer.
// Half a point per level cannot move a decision armour or stats have already
// made, and it does break a tie between two otherwise equal pieces in favour
// of the newer one.
//
// PROFICIENCY IS READ PER CHARACTER AND NEVER INFERRED FROM CLASS. The caller
// fills the five booleans below out of the character's own skills, because
// "warrior" does not mean "plate": a warrior learns plate at level 40, the
// tank measured here is 27, and his skill rows are mail, leather, cloth and
// shield with no plate row at all. Deriving it from class and level instead -
// which is what upstream does, mod-playerbots RandomItemMgr.cpp:1069-1078 -
// happens to get that one case right by arithmetic and would get a future
// party member, a heirloom, or a class change wrong. The core's own rule is a
// skill lookup: an item's armour subclass maps to a skill (AzerothCore
// ItemTemplate.h:782-796) and equipping it needs a nonzero value in that skill
// (PlayerStorage.cpp:2344-2366). Worth knowing that the template-only check
// every "can this bot use it" path starts from,
// Player::CanUseItem(ItemTemplate const*), does NOT make that test at all
// (PlayerStorage.cpp:2377-2421) - it checks faction, class mask, race,
// RequiredSkill, RequiredSpell and level, and lets a priest straight through
// on a pair of leather boots.

enum class GearRole
{
    // No opinion, and the score says so rather than pretending otherwise.
    // Every STAT counts one for one - which is the only neutral answer there
    // is, since with no role there is nothing to prefer - while the armour,
    // weapon-damage and item-level terms keep middle values belonging to no
    // role in particular (0.20, 4.0 and the same 0.5 everyone gets). What
    // comes out is a rough ordering, not a judgement, and it is marked
    // unjudged for exactly that reason: an unjudged verdict never drives an
    // automatic swap and never casts a vote. See GearVerdict::judged. An
    // honest refusal, not a guess.
    Unknown,
    Tank,
    Melee,
    Ranged,
    Healer,
    Caster,
};

// One stat line off an item, exactly as ItemTemplate::ItemStat carries it: the
// core's ItemModType id and the value. Plain ints, so this file still includes
// no core header.
struct GearStat
{
    int type{0};
    int value{0};
};

// What THIS character may wear, and what it is for.
struct GearWearer
{
    std::string name;
    int level{0};
    GearRole role{GearRole::Unknown};

    // From the character's own skills. See the proficiency note above for why
    // these are booleans the caller fills rather than a class id this file
    // reasons about.
    bool cloth{false};
    bool leather{false};
    bool mail{false};
    bool plate{false};
    bool shield{false};

    // Does the character hold the weapon skill this particular item needs?
    // Resolved by the caller, for the same reason: a skill lookup, not a
    // property of the class.
    bool weaponProficient{true};

    // Does the item's AllowableClass admit this character? Resolved by the
    // caller against the class mask.
    bool classAllowed{true};
};

// An item, reduced to what the score actually reads.
struct GearItem
{
    std::string name;
    int itemClass{0};  // ITEM_CLASS_WEAPON 2, ITEM_CLASS_ARMOR 4
    int subClass{0};   // armour: cloth 1, leather 2, mail 3, plate 4, shield 6
    int inventoryType{0};
    int itemLevel{0};
    int requiredLevel{0};
    int quality{0};
    int armour{0};
    float dps{0.f};
    std::vector<GearStat> stats;

    // Carries an on-use or proc spell this file does not model. Not a reason
    // to refuse the item; a reason to stop claiming the score is the whole
    // story about it. See GearVerdict::judged.
    bool hasEffect{false};

    // The caller could not resolve a random suffix or property into stats, so
    // some of this item's worth is missing from `stats` and the score is a
    // floor rather than a figure.
    bool unresolvedRandomProperty{false};
};

struct GearVerdict
{
    // May this character put it on at all? False is final: no score, no
    // upgrade, no Need. This is the half that stops a priest needing leather.
    bool wearable{false};

    // In armour points. Meaningless unless `wearable`.
    float score{0.f};

    // Is the score the whole story? False when the role is unknown, when the
    // item's worth is partly in an effect this file does not read, or when a
    // random property could not be resolved. An unjudged verdict never drives
    // an automatic swap or a Need roll - it is said out loud instead.
    bool judged{false};

    // One clause, for the line the caller prints: "mail, 113 armour", or
    // "no leather proficiency".
    std::string why;
};

GearVerdict GearScore(GearItem const& item, GearWearer const& who);

// What a candidate actually has to beat in the slot it would go into.
//
// A TWO-HANDER HAS TO BEAT BOTH HANDS, which is the whole of the Severing Axe
// test (#14): a green two-hander is not an upgrade for a tank holding a shield
// worth 445 armour and a block, however good the axe is on its own. An empty
// slot scores zero, so the first item into one is an upgrade by construction.
float GearIncumbent(float mainHandScore, float offHandScore, bool takesBothHands);

// How much better a candidate has to be before it is worth a swap or a Need.
// One percent plus half a point: the percentage keeps it proportionate at
// every level, and the absolute floor stops two near-zero scores trading
// places forever. Deliberately small - the failure being fixed is a family
// that never swaps anything, not one that swaps too eagerly - and the swap is
// one-way, so it cannot oscillate: once the better item is worn, the one now
// in the bag is the lower score.
bool GearIsUpgrade(GearVerdict const& candidate, float incumbent);

// WHO NEEDS WHEN TWO MEMBERS BOTH WANT THE SAME DROP (#145). The one with the
// lower total equipped score, because what a dungeon run raises is the party's
// floor; on a tie, the larger gain, and on a tie in that, the name, so the
// answer never depends on the order the party happens to be walked in.
// Returns an empty string when nobody wants it.
struct GearContender
{
    std::string name;
    float gain{0.f};       // candidate score minus incumbent; must be > 0 to count
    float totalWorn{0.f};  // everything this character is wearing, scored
};

std::string GearNeedWinner(std::vector<GearContender> const& contenders);

}  // namespace OverseerDecisions

#endif  // MOD_OVERSEER_DECISIONS_H
