class UnitCalculatedValue : public CalculatedValue<Unit*>
{
public:
    UnitCalculatedValue(PlayerbotAI* botAI, std::string const name = "value", int32 checkInterval = 1);

    std::string const Format() override;
    Unit* Get() override;
};
