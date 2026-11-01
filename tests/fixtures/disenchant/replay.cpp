#include "core.h"
#include "overseer_disenchant.h"
#include "overseer_disenchant_store.h"
#include <cstdio>

class Runtime
{
// RUNTIME_METHODS
    OverseerDisenchant::Store _disenchantStore;
    QueryCallbackProcessor _disenchantQueries;
    std::map<uint32, DisenchantTask> _disenchantTasks;
    std::map<std::string, time_t> _disenchantClaiming;
    std::set<uint32> _disenchantCompleted;
    bool _disenchantOwnedBusy = false;
    bool _disenchantPendingBusy = false;
    bool _disenchantFactsReadable = false;
    time_t _disenchantFactsAt = 0;
    std::set<std::string> _craftLoneHeld;
    static bool InAnyDungeonRun(Player* player) { return player->dungeon; }
    static bool ItemKept(Player const*, Item const* item) { return item->kept; }
    static std::vector<Item*> GearCarried(Player* player)
    {
        std::vector<Item*> items;
        for (auto const& item:player->items) items.push_back(item.second.get());
        return items;
    }
public:
    Runtime()
    {
        DisenchantWindows().clear();
        if (!_disenchantStore.Start("isolated fixture")) throw std::runtime_error("store unavailable");
        ObjectAccessor::players.clear();
        beforeLoot=[](Player* player,ObjectGuid guid,Loot& loot) { TakeDisenchantLoot(player,guid,loot); };
    }
    ~Runtime() { _disenchantStore.Stop(); }
    void Add(Player& player)
    {
        ObjectAccessor::players[player.name]=&player;
        _disenchantStore.Exec("INSERT INTO overseer_roster VALUES('Smith',1,'craft');"
            "INSERT INTO overseer_command(id,target_name,kind,status) VALUES(1,'Smith','disenchant','pending');");
    }
    void Tick()
    {
        ++GameTime::now;
        sqlClock=GameTime::now;
        _disenchantStore.Drain();
        _disenchantQueries.ProcessReadyCallbacks();
        DriveDisenchant();
        ReadDisenchantCommands();
    }
    void Ticks(int n) { while(n--) Tick(); }
    std::string Status() { return _disenchantStore.Status(1); }
    void FailCompletion() { _disenchantStore.failCompletionOnce=true; }
    void FailOwnedReads(bool fail) { _disenchantStore.failOwnedReads=fail; }
    std::size_t Tasks() const { return _disenchantTasks.size(); }
    std::string ReplacementStatus() { return _disenchantStore.Status(2); }
    void Sql(char const* sql) { _disenchantStore.Exec(sql); }
    bool HasProof() { return HasDisenchantProof("Smith"); }
    void AddSecond(Player& player)
    {
        ObjectAccessor::players[player.name]=&player;
        _disenchantStore.Exec("INSERT INTO overseer_roster VALUES('Jones',1,'craft');"
            "INSERT INTO overseer_command(id,target_name,kind,status) VALUES(100,'Jones','disenchant','pending');");
    }
    std::string SecondStatus() { return _disenchantStore.Status(100); }
    std::size_t Queued() { return _disenchantStore.Queued(); }
};
int failures=0;
void Check(char const* text,bool ok)
{ if (!ok) { ++failures;std::printf("FAIL %s\n",text); } }
int main()
{
    {
        Runtime runtime; Player player; runtime.Add(player);
        runtime.Tick();
        Check("the drive only queued its initial database work", runtime.Queued()>0 && player.casts==0 && runtime.Status()=="pending");
        runtime.Ticks(2);
        Check("the selected owned item reaches the cast adapter", player.casts==1 && player.GetItemByGuid(ObjectGuid(42)));
        Check("reagents actually reached the inventory", player.GetItemCount(10940)==9);
        Check("the send-loot hook did not release its still-live source", player.sourceSurvivedSendLoot && player.releases==0);
        runtime.Ticks(3);
        Check("verified inventory and consumed source clear the persisted errand", runtime.Status()=="delivered" && !player.GetItemByGuid(ObjectGuid(42)) && player.casts==1);
    }
    {
        Runtime runtime; Player player;player.emitLoot=false;runtime.Add(player);runtime.Ticks(8);
        Check("an accepted cast without inventory effect never clears", runtime.Status()=="claimed" && player.GetItemCount(10940)==7 && player.releases==0);
    }
    {
        Runtime runtime; Player player;player.loseStoredReagent=true;runtime.Add(player);runtime.Ticks(8);
        Check("a successful store response without a reagent delta cannot clear", runtime.Status()=="claimed" && player.GetItemCount(10940)==7 && player.releases==0);
    }
    {
        Runtime runtime; Player player;player.refuseCast=true;runtime.Add(player);runtime.Ticks(7);
        Check("transient cast refusal leaves the persisted errand standing", runtime.Status()=="claimed" && player.GetItemByGuid(ObjectGuid(42)));
        player.refuseCast=false;runtime.Ticks(4);
        Check("a later successful cast can finish the same errand", runtime.Status()=="delivered");
    }
    {
        Runtime runtime; Player player;player.acceptLoot=false;runtime.Add(player);runtime.Ticks(8);
        Check("full bags do not discard the source or its unlooted reagent", runtime.Status()=="claimed" && player.GetItemByGuid(ObjectGuid(42)) && !player.items.at(42)->loot.isLooted());
        player.acceptLoot=true;runtime.Ticks(4);
        Check("bag-space retry does not recast or reroll the source", runtime.Status()=="delivered" && player.casts==1 && player.GetItemCount(10940)==9);
    }
    {
        Runtime runtime; Player player;runtime.Add(player);runtime.FailCompletion();runtime.Ticks(9);
        Check("completion acknowledgement retries without another cast", runtime.Status()=="delivered" && player.casts==1 && player.GetItemCount(10940)==9);
    }
    {
        Runtime runtime; Player player;runtime.Add(player);
        runtime.Sql("UPDATE overseer_command SET status='claimed',claimed_by='another-world';");
        runtime.Ticks(7);
        Check("another world's claimed errand is never run", player.casts==0 && runtime.Status()=="claimed");
    }
    {
        Runtime runtime; Player player;player.items.at(42)->proto.usable=true;runtime.Add(player);runtime.Ticks(7);
        Check("wearable gear is not disenchanted and establishes no craft hold", player.casts==0 && !runtime.HasProof());
    }
    {
        Runtime runtime; Player player;player.items.at(42)->kept=true;runtime.Add(player);runtime.Ticks(7);
        Check("reserved unusable gear survives without a cast", player.casts==0 && player.GetItemByGuid(ObjectGuid(42)) && runtime.Status()=="claimed");
        if (Item* source=player.GetItemByGuid(ObjectGuid(42))) source->kept=false;
        runtime.Ticks(4);
        Check("removing the reservation allows the same standing errand", runtime.Status()=="delivered" && player.casts==1);
    }
    {
        Runtime runtime; Player player;runtime.Add(player);runtime.Ticks(3);
        player.items.at(42)->kept=true;runtime.Ticks(4);
        Check("a reservation added after loot opens prevents source destruction", player.releases==0 && player.GetItemByGuid(ObjectGuid(42)) && runtime.Status()=="claimed");
        if (Item* source=player.GetItemByGuid(ObjectGuid(42))) source->kept=false;
        runtime.Ticks(3);
        Check("released reservation finishes from existing inventory proof without another cast", runtime.Status()=="delivered" && player.casts==1);
    }
    {
        Runtime runtime; Player player;player.restricted=true;runtime.Add(player);runtime.Ticks(7);
        Check("the core's play-time pickup restriction is preserved", runtime.Status()=="claimed" && player.GetItemCount(10940)==7 && player.releases==0);
    }
    {
        Runtime runtime; Player player;player.combat=true;runtime.Add(player);runtime.Ticks(7);
        Check("combat leaves a claimed errand standing without a cast", player.casts==0 && runtime.Status()=="claimed");
        player.combat=false;runtime.Ticks(4);
        Check("a later safe moment completes the same errand", runtime.Status()=="delivered");
    }
    {
        Runtime runtime; Player player;runtime.Add(player);
        runtime.Tick();runtime.Tick();
        runtime.Sql("UPDATE overseer_roster SET job='quest';");runtime.Ticks(7);
        Check("changed roster permission is read before casting", player.casts==0 && runtime.Status()=="claimed");
    }
    {
        Runtime runtime; Player first;Player second;second.name="Jones";
        runtime.Add(first);runtime.AddSecond(second);
        runtime.Sql("WITH RECURSIVE n(i) AS (VALUES(2) UNION ALL SELECT i+1 FROM n WHERE i<66) "
            "INSERT INTO overseer_command(id,target_name,kind,status) SELECT i,'Smith','disenchant','pending' FROM n;");
        runtime.Ticks(14);
        Check("one member's duplicate pending rows do not starve another member", runtime.SecondStatus()=="delivered" && second.casts==1);
        Check("duplicate requests do not reroll a consumed item", first.casts==1);
    }
    {
        Runtime runtime; Player player;player.known=false;runtime.Add(player);runtime.Ticks(7);
        Check("no spell is granted to satisfy an errand", player.casts==0 && !player.known && runtime.Status()=="claimed");
    }
    {
        Runtime runtime; Player player;player.refuseCast=true;runtime.Add(player);runtime.Ticks(5);
        runtime.Sql("UPDATE overseer_command SET status='error' WHERE id=1;");runtime.Ticks(4);
        Check("a cancelled command is removed from a valid empty owned snapshot", runtime.Tasks()==0);
        runtime.Sql("INSERT INTO overseer_command(id,target_name,kind,status) VALUES(2,'Smith','disenchant','pending');");
        player.refuseCast=false;runtime.Ticks(6);
        Check("cancelled work does not defer a replacement errand forever", runtime.ReplacementStatus()=="delivered");
    }
    {
        Runtime runtime; Player player;player.refuseCast=true;runtime.Add(player);runtime.Ticks(5);
        runtime.FailOwnedReads(true);
        runtime.Sql("UPDATE overseer_command SET status='error' WHERE id=1;");runtime.Ticks(4);
        Check("a failed owned read does not masquerade as an empty snapshot", runtime.Tasks()==1);
        runtime.FailOwnedReads(false);runtime.Ticks(4);
        Check("a recovered valid empty snapshot releases the stale task", runtime.Tasks()==0);
    }
    return failures?1:0;
}
