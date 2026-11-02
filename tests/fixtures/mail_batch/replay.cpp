#include "overseer_decisions.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

#define LOG_INFO(...) ((void)0)
#define LOG_WARN(...) ((void)0)
constexpr int BOT_STATE_NON_COMBAT = 0;
constexpr char const* MAIL_WALK_HOLD_VERB = "mailwalk";
time_t clockNow = 100;
time_t ReplayTime(time_t*) { return clockNow; }
#define time ReplayTime

struct PlayerbotAI
{
    bool stay = true;
    void ChangeStrategy(std::string const& strategy, int)
    {
        if (strategy == "-stay")
            stay = false;
    }
};
struct Player
{
    PlayerbotAI ai;
    float mailboxYards = 2.83f;
    std::string GetName() const { return "Worker"; }
    void StopMoving() {}
};
Player worker;
#define GET_PLAYERBOT_AI(player) (&(player)->ai)
namespace ObjectAccessor
{
Player* FindPlayerByName(std::string const&, bool) { return &worker; }
}
struct HoldRecord
{
    std::string verb;
    time_t until = 0;
    time_t walkingUntil = 0;
    bool addedStay = true;
    bool removedFollow = false;
    bool removedNewRpg = true;
    bool removedMount = false;
    unsigned retaken = 0;
};
std::map<std::string, HoldRecord> holds;
auto& HoldsInForce() { return holds; }
void PinWhereItStands(Player*) {}
void AnchorHoldWhereItStands(HoldRecord&, Player*) {}
bool ARowStillOwnsTheHold(std::string const&) { return false; }
std::set<std::string> _guildDeathSpotTries;
std::map<std::string, unsigned> _guildDeathSpotCeilings;
std::map<std::string, std::string> _guildDeathSpotRefusal;

#error "Inject RELEASE from the production adapter"
#error "Inject ARRIVAL from the production adapter"
#error "Inject EXPIRY from the production adapter"

void Sent(Player* who)
{
    struct { unsigned expectedDelay = 0; } facts;
    std::string status;
    auto describe = [](char const*, char const*, char const*) {};
#error "Inject SENT from the production adapter"
}

unsigned sent = 0;
unsigned purse = 400;
bool PostNextAttachment()
{
    // Native send/readback is a boundary fixture. Only a successful send
    // reaches the extracted callback. The failure being tested is departure
    // BETWEEN commands, not the core's packet or database implementation.
    if (worker.mailboxYards > 5.f || sent == 6 || purse < 30)
        return false;
    ++sent;
    purse -= 30;
    Sent(&worker);
    return true;
}

int failures = 0;
void Check(bool condition, char const* message)
{
    if (!condition)
    {
        std::printf("FAIL %s\n", message);
        ++failures;
    }
}

int main()
{
    holds[worker.GetName()] = {MAIL_WALK_HOLD_VERB};
    HoldAtTheMailbox(&worker, worker.GetName());
    time_t const deadline = holds.at(worker.GetName()).until;
    Check(deadline == 100 + OverseerDecisions::MAIL_WALK_LINGER_SECONDS,
          "arrival establishes the bounded mailbox linger");
    clockNow = 106;
    Check(PostNextAttachment(), "the first attachment is posted");
    // An ordinary AI movement opportunity after the first send, matching
    // the captured distance change without needing a running game server.
    if (!worker.ai.stay)
        worker.mailboxYards = 12.76f;
    clockNow = 110;
    Check(PostNextAttachment(), "the second attachment is posted before leaving the mailbox");
    Check(sent == 2 && purse == 340, "two attachments charge postage exactly once each");
    for (unsigned attachment = 2; attachment < 6; ++attachment)
    {
        clockNow += 5;
        if (!worker.ai.stay)
            worker.mailboxYards = 12.76f;
        Check(PostNextAttachment(), "the rest of a six-letter batch stays at the mailbox");
    }
    Check(sent == 6 && purse == 220, "the six-letter batch is posted with ordinary postage");
    Check(!PostNextAttachment() && purse == 220, "an exhausted batch adds no postage");
    Check(holds.count(worker.GetName()) == 1, "one send does not end the mailbox visit");
    if (holds.count(worker.GetName()))
        Check(holds.at(worker.GetName()).until == deadline, "sends do not extend the deadline");
    clockNow = deadline - 1;
    ReleaseExpiredHolds();
    Check(worker.ai.stay, "the visit stays held until its deadline");
    clockNow = deadline;
    ReleaseExpiredHolds();
    Check(holds.empty() && !worker.ai.stay, "the normal sweep releases the visit at its ceiling");

    holds[worker.GetName()] = {"conjure", clockNow + 30};
    worker.ai.stay = true;
    Sent(&worker);
    Check(holds.at(worker.GetName()).verb == "conjure" && worker.ai.stay,
          "a successful send does not release another verb's hold");
    holds.clear();
    Sent(&worker);
    Check(holds.empty(), "a send without a walk does not create a hold");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
