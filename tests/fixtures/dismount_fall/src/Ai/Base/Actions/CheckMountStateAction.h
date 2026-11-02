{
public:
    CheckMountStateAction(PlayerbotAI* botAI) : UseItemAction(botAI, "check mount state", true) {}

    bool Execute(Event event) override;
    bool isUseful() override;
    bool isPossible() override { return true; }
    bool Mount();

    static void CompleteDismount(Player* bot);
    static void LoadPreferredMounts();

private:
    Player* master;
    ShapeshiftForm masterInShapeshiftForm;
    ShapeshiftForm botInShapeshiftForm;
    static std::unordered_map<uint32, PreferredMountCache> mountCache;
