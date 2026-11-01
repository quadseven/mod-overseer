#ifndef DISENCHANT_REPLAY_CORE_H
#define DISENCHANT_REPLAY_CORE_H
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>
#include <sqlite3.h>
using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using Cell = std::variant<int64_t, std::string>;
inline int64_t sqlClock=1000;
struct Field
{
    Cell value;
    template<class T> T Get() const
    {
        if constexpr (std::is_same_v<T, std::string>)
            return std::holds_alternative<std::string>(value) ? std::get<std::string>(value) : std::to_string(std::get<int64_t>(value));
        else
            return static_cast<T>(std::holds_alternative<int64_t>(value) ? std::get<int64_t>(value) : std::stoll(std::get<std::string>(value)));
    }
};
struct Result
{
    std::vector<std::vector<Field>> rows;
    std::size_t at = 0;
    Field* Fetch() { return rows.at(at).data(); }
    bool NextRow() { return ++at < rows.size(); }
};
using PreparedQueryResult = std::shared_ptr<Result>;
struct Future { bool ready = false; PreparedQueryResult result; };
class QueryCallback
{
public:
    std::shared_ptr<Future> future;
    std::function<void(PreparedQueryResult)> callback;
    explicit QueryCallback(std::shared_ptr<Future> f) : future(std::move(f)) {}
    QueryCallback&& WithPreparedCallback(std::function<void(PreparedQueryResult)> cb)
    { callback = std::move(cb); return std::move(*this); }
};
class QueryCallbackProcessor
{
    std::vector<QueryCallback> callbacks;
public:
    void AddCallback(QueryCallback cb) { callbacks.push_back(std::move(cb)); }
    void ProcessReadyCallbacks()
    {
        std::vector<QueryCallback> ready;
        for (auto it = callbacks.begin(); it != callbacks.end();)
            if (it->future->ready) { ready.push_back(std::move(*it)); it = callbacks.erase(it); }
            else ++it;
        for (auto& cb : ready) cb.callback(cb.future->result);
    }
};
class PreparedStatementBase
{
public:
    uint32 index;
    std::vector<Cell> values;
    PreparedStatementBase(uint32 i, uint8 count) : index(i), values(count, int64_t(0)) {}
    template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
    void SetData(uint8 i, T value) { values.at(i) = int64_t(value); }
    void SetData(uint8 i, std::string const& value) { values.at(i) = value; }
};
class SQLOperation {};
template<class T> class ProducerConsumerQueue {};
struct MySQLConnectionInfo {};
constexpr int CONNECTION_ASYNC = 1;
class MySQLConnection
{
public:
    bool m_reconnecting = false;
    std::vector<int> m_stmts;
    std::map<uint32, std::string> sql;
    explicit MySQLConnection(MySQLConnectionInfo&) {}
    MySQLConnection(ProducerConsumerQueue<SQLOperation*>*, MySQLConnectionInfo&) {}
    virtual ~MySQLConnection() = default;
    virtual void DoPrepareStatements() = 0;
    void PrepareStatement(uint32 i, char const* text, int flags)
    {
        if (flags != CONNECTION_ASYNC) throw std::runtime_error("drive statement is not asynchronous");
        sql[i] = text;
    }
};
class ModuleDatabasePool
{
    sqlite3* db = nullptr;
    std::unique_ptr<MySQLConnection> connection;
    std::vector<std::function<void()>> work;
public:
    bool failCompletionOnce = false;
    virtual ~ModuleDatabasePool() { Close(); }
    void SetConnectionInfo(std::string_view, uint8 async, uint8 sync)
    { if (async != 1 || sync != 1) throw std::runtime_error("unexpected pool concurrency"); }
    uint32 Open()
    {
        if (sqlite3_open(":memory:", &db) != SQLITE_OK) throw std::runtime_error("SQLite unavailable");
        sqlite3_create_function(db, "NOW", 0, SQLITE_UTF8, nullptr,
            [](sqlite3_context* c, int, sqlite3_value**) { sqlite3_result_int64(c, sqlClock); }, nullptr, nullptr);
        Exec("CREATE TABLE overseer_roster(name TEXT PRIMARY KEY, enabled INTEGER, job TEXT);"
             "CREATE TABLE overseer_command(id INTEGER PRIMARY KEY,target_name TEXT,kind TEXT,status TEXT,claimed_by TEXT DEFAULT '',result TEXT DEFAULT '',updated_at INTEGER DEFAULT 0);");
        return 0;
    }
    bool PrepareStatements()
    {
        MySQLConnectionInfo info;
        connection.reset(CreateConnection(info));
        connection->DoPrepareStatements();
        for (auto const& entry : connection->sql) {
            sqlite3_stmt* s = nullptr;
            if (sqlite3_prepare_v2(db, entry.second.c_str(), -1, &s, nullptr) != SQLITE_OK)
                throw std::runtime_error(sqlite3_errmsg(db));
            sqlite3_finalize(s);
        }
        return true;
    }
    void Close() { if (db) { Drain(); sqlite3_close(db); db = nullptr; } }
    uint8 GetPreparedStatementParamCount(uint32 i) const
    {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db, connection->sql.at(i).c_str(), -1, &s, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
        int n = sqlite3_bind_parameter_count(s); sqlite3_finalize(s); return uint8(n);
    }
    void Execute(PreparedStatementBase* raw)
    {
        auto p = std::shared_ptr<PreparedStatementBase>(raw);
        work.push_back([this,p] { Run(*p); });
    }
    QueryCallback AsyncQuery(PreparedStatementBase* raw)
    {
        auto p = std::shared_ptr<PreparedStatementBase>(raw);
        auto f = std::make_shared<Future>();
        work.push_back([this,p,f] { f->result = Run(*p); f->ready = true; });
        return QueryCallback(f);
    }
    void Drain()
    {
        auto queue = std::move(work); work.clear();
        for (auto& operation : queue) operation();
    }
    std::size_t Queued() const { return work.size(); }
    void Exec(char const* sql)
    {
        char* error = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
            std::string message = error; sqlite3_free(error); throw std::runtime_error(message);
        }
    }
    std::string Status(uint32 id)
    {
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db, "SELECT status FROM overseer_command WHERE id=?", -1, &s, nullptr);
        sqlite3_bind_int(s,1,int(id));
        std::string result;
        if (sqlite3_step(s)==SQLITE_ROW) result=reinterpret_cast<char const*>(sqlite3_column_text(s,0));
        sqlite3_finalize(s); return result;
    }
protected:
    virtual MySQLConnection* CreateConnection(MySQLConnectionInfo&) = 0;
    virtual MySQLConnection* CreateConnection(ProducerConsumerQueue<SQLOperation*>*,MySQLConnectionInfo&) = 0;
private:
    PreparedQueryResult Run(PreparedStatementBase const& p)
    {
        auto const& sql = connection->sql.at(p.index);
        if (failCompletionOnce && sql.find("SET status = 'delivered'") != std::string::npos) {
            failCompletionOnce=false; return {};
        }
        sqlite3_stmt* raw = nullptr;
        if (sqlite3_prepare_v2(db,sql.c_str(),-1,&raw,nullptr)!=SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
        std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw,sqlite3_finalize);
        for (std::size_t i=0;i<p.values.size();++i) {
            auto const& value=p.values[i];
            if (auto text=std::get_if<std::string>(&value))
                sqlite3_bind_text(raw,int(i+1),text->c_str(),-1,SQLITE_TRANSIENT);
            else sqlite3_bind_int64(raw,int(i+1),std::get<int64_t>(value));
        }
        auto result=std::make_shared<Result>();
        int rc;
        while ((rc=sqlite3_step(raw))==SQLITE_ROW) {
            std::vector<Field> row;
            for (int c=0;c<sqlite3_column_count(raw);++c)
                if (sqlite3_column_type(raw,c)==SQLITE_TEXT)
                    row.push_back({std::string(reinterpret_cast<char const*>(sqlite3_column_text(raw,c)))});
                else row.push_back({int64_t(sqlite3_column_int64(raw,c))});
            result->rows.push_back(std::move(row));
        }
        if (rc!=SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db));
        return result->rows.empty() ? PreparedQueryResult{} : result;
    }
};
class ObjectGuid
{
    uint64 value = 0;
public:
    explicit ObjectGuid(uint64 v=0) : value(v) {}
    uint64 GetRawValue() const { return value; }
    bool IsEmpty() const { return !value; }
    bool IsItem() const { return value && value<10000; }
    bool operator==(ObjectGuid other) const { return value==other.value; }
    bool operator!=(ObjectGuid other) const { return !(*this==other); }
};
constexpr uint32 ITEM_QUALITY_UNCOMMON=2, SKILL_ENCHANTING=333, TRIGGERED_NONE=0;
constexpr uint32 PLAYER_FLAGS_NO_PLAY_TIME=1;
enum InventoryResult { EQUIP_ERR_OK, EQUIP_ERR_REFUSED };
enum SpellCastResult { SPELL_CAST_OK, SPELL_FAILED_REFUSED };
constexpr uint32 LOOT_DISENCHANTING=4;
struct LootItem { uint32 itemid=10940, count=2; bool is_looted=false; };
struct Loot
{
    uint32 loot_type=LOOT_DISENCHANTING;
    std::vector<LootItem> items, quest_items;
    bool isLooted() const
    {
        for (auto const& item:items) if (!item.is_looted) return false;
        for (auto const& item:quest_items) if (!item.is_looted) return false;
        return true;
    }
};
struct ItemTemplate { uint32 Quality=2,DisenchantID=1; int32_t RequiredDisenchantSkill=1; bool usable=false; };
struct Item
{
    ItemTemplate proto;
    uint64 guid=42,owner=100;
    bool bound=true,m_lootGenerated=false;
    Loot loot;
    ItemTemplate const* GetTemplate() const { return &proto; }
    ObjectGuid GetGUID() const { return ObjectGuid(guid); }
    ObjectGuid GetOwnerGUID() const { return ObjectGuid(owner); }
    bool IsSoulBound() const { return bound; }
};
struct SpellInfo {};
struct SpellMgr { SpellInfo info; SpellInfo const* GetSpellInfo(uint32) { return &info; } };
inline SpellMgr spellMgr;
inline SpellMgr* sSpellMgr=&spellMgr;
struct SpellCastTargets { Item* item=nullptr; void SetItemTarget(Item* target) { item=target; } };
struct Player;
struct WorldSession { Player* player; void DoLootRelease(ObjectGuid); };
inline std::function<void(Player*,ObjectGuid,Loot&)> beforeLoot;
struct Player
{
    std::string name="Smith";
    bool alive=true,online=true,combat=false,moving=false,dungeon=false,known=true;
    bool refuseCast=false,emitLoot=true,acceptLoot=true,releaseWorks=true,restricted=false;
    bool loseStoredReagent=false;
    uint32 skill=100,casts=0,releases=0;
    bool sourceSurvivedSendLoot=true;
    ObjectGuid currentLoot;
    std::map<uint64,std::unique_ptr<Item>> items;
    std::map<uint32,uint32> bags{{10940,7}};
    WorldSession session{this};
    Player() { items[42]=std::make_unique<Item>(); }
    std::string const& GetName() const { return name; }
    ObjectGuid GetGUID() const { return ObjectGuid(100); }
    Item* GetItemByGuid(ObjectGuid guid) const
    { auto it=items.find(guid.GetRawValue()); return it==items.end()?nullptr:it->second.get(); }
    uint32 GetItemCount(uint32 entry,bool=false) const
    { auto it=bags.find(entry); return it==bags.end()?0:it->second; }
    WorldSession* GetSession() { return &session; }
    ObjectGuid GetLootGUID() const { return currentLoot; }
    bool IsAlive() const { return alive; }
    bool IsInWorld() const { return online; }
    bool IsInCombat() const { return combat; }
    bool isMoving() const { return moving; }
    bool IsNonMeleeSpellCast(bool) const { return false; }
    bool HasSpell(uint32) const { return known; }
    bool HasPlayerFlag(uint32) const { return restricted; }
    uint32 GetSkillValue(uint32) const { return skill; }
    InventoryResult CanUseItem(Item* item) const { return item->proto.usable?EQUIP_ERR_OK:EQUIP_ERR_REFUSED; }
    LootItem* StoreLootItem(uint8 slot,Loot* loot,InventoryResult& result)
    {
        result=EQUIP_ERR_REFUSED;
        if (!acceptLoot) return nullptr;
        auto& group=slot<loot->items.size()?loot->items:loot->quest_items;
        std::size_t index=slot<loot->items.size()?slot:slot-loot->items.size();
        if (index>=group.size() || group[index].is_looted) return nullptr;
        auto& item=group[index];
        if (!loseStoredReagent) bags[item.itemid]+=item.count;
        item.is_looted=true;
        result=EQUIP_ERR_OK; return &item;
    }
    SpellCastResult CastSpell(SpellCastTargets const& targets,SpellInfo const*,void const*,uint32)
    {
        ++casts;
        if (refuseCast) return SPELL_FAILED_REFUSED;
        if (emitLoot) {
            Item* item=targets.item; item->m_lootGenerated=true;
            item->loot.items={{10940,2,false}}; currentLoot=item->GetGUID();
            beforeLoot(this,currentLoot,item->loot);
            sourceSurvivedSendLoot = sourceSurvivedSendLoot && GetItemByGuid(currentLoot);
        }
        return SPELL_CAST_OK;
    }
};
inline void WorldSession::DoLootRelease(ObjectGuid guid)
{
    ++player->releases;
    if (player->releaseWorks) { player->items.erase(guid.GetRawValue()); player->currentLoot=ObjectGuid(); }
}
namespace ObjectAccessor
{
inline std::map<std::string,Player*> players;
inline Player* FindPlayerByName(std::string const& name)
{ auto it=players.find(name);return it==players.end()?nullptr:it->second; }
}
namespace GameTime
{
inline int64_t now=1000;
inline std::chrono::seconds GetGameTime() { return std::chrono::seconds(now); }
}
inline std::string const g_runToken="unit' OR 1=1 --";
template<class... Args> void ReplayLog(char const*,char const*,Args const&...) {}
#define LOG_INFO(...) ReplayLog(__VA_ARGS__)
#define LOG_WARN(...) ReplayLog(__VA_ARGS__)
#define LOG_ERROR(...) ReplayLog(__VA_ARGS__)
#endif
