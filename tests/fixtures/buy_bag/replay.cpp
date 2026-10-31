#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using int32 = int32_t;
using int64 = int64_t;
constexpr uint8 NULL_BAG = 255, NULL_SLOT = 255, INVENTORY_SLOT_BAG_0 = 0;
constexpr uint8 INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr unsigned ITEM_CLASS_CONTAINER = 1, ITEM_SUBCLASS_CONTAINER = 0;
constexpr unsigned INVTYPE_BAG = 18;
enum InventoryResult {
  EQUIP_ERR_OK,
  EQUIP_ERR_INVENTORY_FULL,
  EQUIP_ERR_CANT_EQUIP,
  EQUIP_ERR_ITEM_DOESNT_GO_TO_SLOT
};
using ItemPosCountVec = std::vector<unsigned>;
enum { CMSG_LIST_INVENTORY, CMSG_BUY_ITEM, CMSG_BUY_ITEM_IN_SLOT };
struct WorldPacket {
  std::vector<uint64_t> values;
  WorldPacket(int, int) {}
  template <class T> WorldPacket &operator<<(T value) {
    values.push_back(static_cast<uint64_t>(value));
    return *this;
  }
};
namespace WorldPackets::Item {
struct ListInventory {
  explicit ListInventory(WorldPacket &&) {}
  void Read() {}
};
// Packet decoding is the pinned SDK boundary. Native adapter CI separately
// compiles these calls against the real packet classes and session handlers.
struct BuyItem {
  WorldPacket raw;
  explicit BuyItem(WorldPacket &&p) : raw(std::move(p)) {}
  void Read() {}
};
struct BuyItemInSlot : BuyItem {
  using BuyItem::BuyItem;
};
} // namespace WorldPackets::Item
struct ItemTemplate {
  unsigned Class = ITEM_CLASS_CONTAINER, SubClass = ITEM_SUBCLASS_CONTAINER;
  unsigned ContainerSlots = 6, InventoryType = INVTYPE_BAG;
};
struct Item {
  uint32 entry = 0;
  uint32 GetEntry() const { return entry; }
};
struct Player {
  int64 money = 100;
  int quantity = 0, free = 0;
  bool equipmentAllowed = true, coreRefuses = false, misdirected = false;
  int blockedSlot = -1;
  mutable Item slotItem;
  uint32 worn[23] = {};
  Player() { worn[19] = 4496; }
  uint64_t GetGUID() const { return 77; }
  bool HasEnoughMoney(uint32 price) const { return money >= price; }
  int64 GetMoney() const { return money; }
  unsigned GetHonorPoints() const { return 0; }
  int GetItemCount(uint32, bool) const { return quantity; }
  Item *GetItemByPos(uint8, uint8 slot) const {
    slotItem.entry = worn[slot];
    return worn[slot] ? &slotItem : nullptr;
  }
  InventoryResult CanStoreNewItem(uint8, uint8, ItemPosCountVec &, uint32,
                                  uint32) {
    return free > 0 ? EQUIP_ERR_OK : EQUIP_ERR_INVENTORY_FULL;
  }
  InventoryResult CanEquipNewItem(uint8 slot, uint16 &dest, uint32, bool) {
    dest = slot;
    return equipmentAllowed && !worn[slot] && slot != blockedSlot
               ? EQUIP_ERR_OK
               : EQUIP_ERR_CANT_EQUIP;
  }
};
struct Vendor {
  uint64_t GetGUID() const { return 88; }
  unsigned GetVendorItemCurrentCount(void *) const { return 10; }
};
struct Line {
  unsigned maxcount = 0;
};
struct Request {
  uint32 entry = 4496, count = 1;
  bool capped = false;
  uint32 maxCopper = 100;
};
struct Evidence {
  bool haveInventoryResult = false;
  InventoryResult inventoryResult = EQUIP_ERR_OK;
  int32 itemsWanted = 1, vendorSlot = 3, carriedBefore = 0, carriedAfter = 0,
        stockLeft = 0;
  int64 moneyBefore = 100, moneyAfter = 100, honorBefore = -1, honorAfter = -1,
        honorPrice = 0;
};
struct Session {
  Player *buyer;
  uint32 price = 20;
  bool listed = false, targeted = false;
  explicit Session(Player *p) : buyer(p) {}
  void HandleListInventoryOpcode(WorldPackets::Item::ListInventory &) {
    listed = true;
  }
  void HandleBuyItemOpcode(WorldPackets::Item::BuyItem &p) {
    if (!listed || buyer->coreRefuses || buyer->free == 0 ||
        p.raw.values[2] != 4)
      return;
    buyer->quantity += p.raw.values[3];
    buyer->money -= price;
  }
  void HandleBuyItemInSlotOpcode(WorldPackets::Item::BuyItemInSlot &p) {
    auto const &v = p.raw.values;
    targeted = true;
    if (!listed || buyer->coreRefuses || v.size() != 6 || v[0] != 88 ||
        v[2] != 4 || v[3] != buyer->GetGUID() || v[4] < 19 || v[4] >= 23 ||
        v[5] != 1 || buyer->worn[v[4]] || !buyer->equipmentAllowed)
      return;
    buyer->worn[buyer->misdirected ? 21 : v[4]] = v[1];
    buyer->quantity += 1;
    buyer->money -= price;
  }
};
#define LOG_INFO(...)
char const *purchase(Player *buyer, Request request, ItemTemplate const *proto,
                     Session *session, Evidence &ev) {
  Vendor vendorObject;
  Vendor *vendor = &vendorObject;
  Line lineObject;
  Line *line = &lineObject;
  uint32 const price = session->price;
  char const *status = "error";
  auto refuse = [](char const *reason) { return reason; };
  auto describe = [](char const *, char const *) {};
  ev.moneyBefore = buyer->money;
  ev.itemsWanted = request.count;
  // The Python runner injects the production DoBuy tail here before compiling.
  // A raw template build must fail rather than call an unfilled function.
#error "Run tests/replay_buy_bag.py to inject the production purchase body"
}
int main() {
  int failures = 0;
  auto check = [&](char const *name, bool ok) {
    if (!ok) {
      ++failures;
      std::cerr << "FAIL " << name << '\n';
    }
  };
  Player p;
  Request request;
  ItemTemplate proto;
  Evidence ev;
  Session session(&p);
  check("full backpack buys into an empty bag position",
        std::string(purchase(&p, request, &proto, &session, ev)).empty());
  check("destination is the first empty bag slot",
        p.worn[19] == 4496 && p.worn[20] == 4496 && !p.worn[21]);
  check("price and quantity are read back", p.money == 80 && p.quantity == 1 &&
                                                ev.moneyAfter == 80 &&
                                                ev.carriedAfter == 1);
  check("targeted native purchase handler is used", session.targeted);
  Player later;
  later.blockedSlot = 20;
  Session laterSession(&later);
  Evidence laterRead;
  check("a refused empty position does not hide a later legal one",
        std::string(purchase(&later, request, &proto, &laterSession, laterRead))
                .empty() &&
            !later.worn[20] && later.worn[21] == request.entry);
  for (unsigned mode = 0; mode < 6; ++mode) {
    Player held;
    Request ask;
    ItemTemplate item;
    if (mode == 0)
      for (unsigned i = 19; i < 23; ++i)
        held.worn[i] = 4496;
    if (mode == 1)
      held.equipmentAllowed = false;
    if (mode == 2)
      item.Class = 0;
    if (mode == 3)
      ask.count = 2;
    if (mode == 4) {
      ask.capped = true;
      ask.maxCopper = 10;
    }
    if (mode == 5)
      held.money = 10;
    int64 const money = held.money;
    Session core(&held);
    Evidence read;
    check("unsafe purchase is refused",
          !std::string(purchase(&held, ask, &item, &core, read)).empty());
    check("refusal leaves purse and inventory unchanged",
          held.money == money && held.quantity == 0);
  }
  Player refused;
  refused.coreRefuses = true;
  Session core(&refused);
  Evidence read;
  check(
      "core rejection is detected by readback",
      !std::string(purchase(&refused, request, &proto, &core, read)).empty() &&
          refused.money == 100);
  Player wrong;
  wrong.misdirected = true;
  Session shifted(&wrong);
  Evidence misplaced;
  check("quantity alone cannot prove equipment arrival",
        !std::string(purchase(&wrong, request, &proto, &shifted, misplaced))
             .empty());
  Player room;
  room.free = 1;
  Session ordinary(&room);
  Evidence stored;
  check("ordinary storage path still works",
        std::string(purchase(&room, request, &proto, &ordinary, stored))
                .empty() &&
            !ordinary.targeted);
  return failures ? 1 : 0;
}
