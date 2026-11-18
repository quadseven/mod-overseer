/*
 * The create-character verb's wiring in the adapter, read as text (the
 * adapter itself compiles only in CI).
 *
 * Pins: the row is routed before the online test and before the rename
 * branch, the character is made through the core's own Player::Create and
 * saved with SaveToDB (never an INSERT into characters), the verdict from the
 * decision layer gates it, every creation is logged, a level above 1 goes
 * through the factory's Randomize, and the config keys the module reads are
 * declared in the conf template.
 */

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
int failures = 0;

void Check(char const* what, bool ok)
{
    if (ok)
        return;
    std::printf("FAIL %s\n", what);
    ++failures;
}

std::string Read(char const* path)
{
    std::ifstream file(path);
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

std::string Between(std::string const& source, std::string const& start, std::string const& end)
{
    std::string::size_type first = source.find(start);
    if (first == std::string::npos)
        return {};
    first += start.size();
    std::string::size_type last = source.find(end, first);
    return last == std::string::npos ? std::string() : source.substr(first, last - first);
}
}  // namespace

int main()
{
    std::string const source = Read("src/mod_overseer.cpp");
    std::string const conf = Read("conf/mod_overseer.conf.dist");
    Check("source is readable", !source.empty());

    auto const createRoute = source.find("OverseerDecisions::IsCreateCharacterRow(command)");
    auto const renameRoute = source.find("OverseerDecisions::IsRenameRow(command))");
    Check("the create row is routed", createRoute != std::string::npos);
    Check("it is routed before the rename branch", createRoute < renameRoute);

    std::string const body = Between(source, "char const* DoCreateCharacter(", "void ResolveCreateChecks(");
    Check("the verb body is found", !body.empty());
    Check("it asks the decision layer", body.find("CreateCharacterVerdictFor(request, facts)") != std::string::npos);
    Check("a refusal returns before anything is made",
          body.find("Refuse") < body.find("created->Create("));
    Check("it creates through the core", body.find("created->Create(") != std::string::npos);
    Check("it saves through the core", body.find("SaveToDB(true, false)") != std::string::npos);
    Check("it never inserts a character row", body.find("INSERT INTO characters") == std::string::npos);
    Check("every creation is logged", body.find("create-character '{}' made") != std::string::npos);
    Check("the idempotent answer is applied and changes nothing",
          body.find("\"outcome\\\":\\\"exists\\\"") != std::string::npos);

    std::string const resolver = Between(source, "void ResolveCreateChecks(", "// The name questions DoRename");
    Check("a level above 1 goes through the factory", resolver.find("factory.Randomize(false)") != std::string::npos);
    Check("the resolver is polled", source.find("ResolveCreateChecks(sincePollMs);") != std::string::npos);

    std::string const accountBody = Between(source, "char const* DoCreateAccount(", "char const* DoCreateCharacter(");
    Check("the account verb body is found", !accountBody.empty());
    Check("the account is made by the core", accountBody.find("sAccountMgr->CreateAccount(upper, password)") != std::string::npos);
    Check("the password is never logged or returned",
          accountBody.find("LOG_") != std::string::npos && accountBody.find("<< password") == std::string::npos &&
              accountBody.find(", password)") == accountBody.rfind(", password)") &&
              accountBody.find("J(password)") == std::string::npos);
    Check("the password is made at the core's limit, not longer",
          accountBody.find("CREATE_ACCOUNT_PASSWORD_LENGTH") != std::string::npos && accountBody.find("< 40") == std::string::npos);
    Check("the account verb is routed before the character verb",
          source.find("IsCreateAccountRow(command)") < source.find("IsCreateCharacterRow(command)"));

    for (char const* key : {"Overseer.CreateCharacter.Enable", "Overseer.CreateCharacter.Accounts",
                            "Overseer.CreateCharacter.SlotCap", "Overseer.CreateCharacter.MaxLevel"})
    {
        std::string const line = std::string("#") + key + " = ";
        Check(key, conf.find(line) != std::string::npos);
        Check(key, source.find(std::string("\"") + key + "\"") != std::string::npos);
    }

    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
