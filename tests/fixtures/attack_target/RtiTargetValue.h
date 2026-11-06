class RtiTargetValue : public TargetValue
{
public:
    RtiTargetValue(PlayerbotAI* botAI, std::string const type = "rti", std::string const name = "rti target")
        : TargetValue(botAI, name), type(type)
    {
    }

    static int32 GetRtiIndex(std::string const rti);
    Unit* Calculate() override;
    static const int8 starIndex = 0;
    static const int8 circleIndex = 1;
    static const int8 diamondIndex = 2;
    static const int8 triangleIndex = 3;
    static const int8 moonIndex = 4;
    static const int8 squareIndex = 5;
