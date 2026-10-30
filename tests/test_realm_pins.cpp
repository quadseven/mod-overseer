/*
 * Source contract for realm-pins.env (#574).
 *
 * The adapter check and the image build compile against the pins the realm
 * runs. The pins live in ONE file at the repository root, and both workflows
 * read it through scripts/read-realm-pins.sh. This runs that reader against
 * the real file and against broken copies of it, and reads the two workflows
 * as text, so a second copy of a pin or a fallback to a built-in one fails
 * here in seconds. Run from the repo root.
 */

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace
{
std::string Read(std::string const& path)
{
    std::ifstream in(path);
    if (!in)
        return std::string();
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

void Write(std::string const& path, std::string const& text)
{
    std::ofstream out(path);
    out << text;
}

int failures = 0;

void Check(bool ok, std::string const& what)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

struct Run
{
    int status;
    std::string output;  // stdout and stderr together
};

Run Reader(std::string const& file, std::string const& prefix = std::string())
{
    std::string const out = "r3-m574-reader.out";
    std::string const cmd =
        "bash scripts/read-realm-pins.sh '" + file + "' " + prefix + " > " + out + " 2>&1";
    int const raw = std::system(cmd.c_str());
    Run run;
    run.status = raw == 0 ? 0 : 1;
    run.output = Read(out);
    std::remove(out.c_str());
    return run;
}

std::vector<std::string> Lines(std::string const& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

bool HasLine(std::string const& text, std::string const& line)
{
    for (std::string const& l : Lines(text))
        if (l == line)
            return true;
    return false;
}

// The file with every line that starts with `key=` removed, or with that line
// replaced.
std::string Mutate(std::string const& text, std::string const& key, std::string const& replacement)
{
    std::string out;
    for (std::string const& l : Lines(text))
    {
        if (l.compare(0, key.size() + 1, key + "=") == 0)
        {
            if (!replacement.empty())
                out += replacement + "\n";
            continue;
        }
        out += l + "\n";
    }
    return out;
}

bool IsHex40(std::string const& s)
{
    if (s.size() != 40)
        return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

// True when the text has a line `<spaces>NAME: <40 hex>`, i.e. a pin written
// into a workflow instead of read from the pins file.
bool HasWorkflowPin(std::string const& text)
{
    for (std::string const& l : Lines(text))
    {
        std::size_t const colon = l.find(": ");
        if (colon == std::string::npos || l.find('#') != std::string::npos)
            continue;
        std::string const value = l.substr(colon + 2);
        if (IsHex40(value))
            return true;
    }
    return false;
}
}  // namespace

int main()
{
    std::string const pins = Read("realm-pins.env");
    if (pins.empty())
    {
        std::cerr << "FAIL: realm-pins.env is missing or empty (run from the repo root)\n";
        return EXIT_FAILURE;
    }

    char const* const shaKeys[] = {"CORE_SHA", "MODULE_SHA", "HEADS_CORE_SHA", "HEADS_MODULE_SHA",
                                   "OLLAMA_CHAT_SHA", "DUNGEON_CLEAR_SHA", "JUNK_TO_GOLD_SHA",
                                   "AH_BOT_SHA"};
    char const* const repoKeys[] = {"CORE_REPO", "MODULE_REPO", "HEADS_CORE_REPO",
                                    "HEADS_MODULE_REPO", "OLLAMA_CHAT_REPO",
                                    "DUNGEON_CLEAR_REPO", "JUNK_TO_GOLD_REPO", "AH_BOT_REPO"};

    // The real file reads clean and names every module the realm image carries.
    Run const real = Reader("realm-pins.env");
    Check(real.status == 0, "the real realm-pins.env reads clean: " + real.output);
    for (char const* key : shaKeys)
    {
        std::string const k = key;
        if (k.compare(0, 6, "HEADS_") == 0)
            continue;
        bool found = false;
        for (std::string const& l : Lines(real.output))
            if (l.compare(0, k.size() + 1, k + "=") == 0 && IsHex40(l.substr(k.size() + 1)))
                found = true;
        Check(found, "the reader prints a 40-hex " + k);
    }
    for (char const* mod : {"OLLAMA_CHAT_REPO", "DUNGEON_CLEAR_REPO", "JUNK_TO_GOLD_REPO",
                            "AH_BOT_REPO"})
        Check(real.output.find(std::string(mod) + "=https://") != std::string::npos,
              std::string("the realm's other modules are named: ") + mod);

    // The heads set comes out under the unprefixed names.
    std::string const headsCore = [&]() {
        for (std::string const& l : Lines(pins))
            if (l.compare(0, 15, "HEADS_CORE_SHA=") == 0)
                return l.substr(15);
        return std::string();
    }();
    Run const heads = Reader("realm-pins.env", "HEADS_");
    Check(heads.status == 0, "the HEADS_ set reads clean");
    Check(HasLine(heads.output, "CORE_SHA=" + headsCore),
          "HEADS_CORE_SHA is printed as CORE_SHA for the heads leg");

    // Missing file: fails and names it. No fallback to a built-in pin.
    Run const missing = Reader("r3-m574-no-such-pins.env");
    Check(missing.status != 0, "a missing pins file fails");
    Check(missing.output.find("r3-m574-no-such-pins.env") != std::string::npos,
          "the failure names the file");
    Check(missing.output.find("CORE_SHA=") == std::string::npos,
          "a missing file prints no pin to fall back on");

    // Every key is required: removing any one fails and names the file and key.
    std::string const broken = "r3-m574-broken.env";
    std::vector<std::string> allKeys;
    for (char const* k : shaKeys)
        allKeys.push_back(k);
    for (char const* k : repoKeys)
        allKeys.push_back(k);
    for (std::string const& key : allKeys)
    {
        Write(broken, Mutate(pins, key, ""));
        Run const r = Reader(broken);
        Check(r.status != 0, "removing " + key + " fails the reader");
        Check(r.output.find(broken) != std::string::npos && r.output.find(key) != std::string::npos,
              "the failure for " + key + " names the file and the key");
        Check(r.output.find("_SHA=") == std::string::npos,
              "a failing read prints no pins for " + key);
    }

    // A SHA that is not 40 lowercase hex characters fails.
    Write(broken, Mutate(pins, "MODULE_SHA", "MODULE_SHA=40884e9c"));
    Check(Reader(broken).status != 0, "a short SHA fails");
    Write(broken, Mutate(pins, "JUNK_TO_GOLD_SHA",
                         "JUNK_TO_GOLD_SHA=2134690BB03899E5C9E44D0682E8E6ABF0BBBAF2"));
    Check(Reader(broken).status != 0, "an uppercase SHA fails");
    Write(broken, Mutate(pins, "CORE_REPO", "CORE_REPO=git@example.com:x/y.git"));
    Check(Reader(broken).status != 0, "a repo that is not an https URL fails");
    Write(broken, Mutate(pins, "AH_BOT_SHA", "AH_BOT_SHA="));
    Check(Reader(broken).status != 0, "an empty value fails");
    std::remove(broken.c_str());

    // Both workflows read the file and carry no pin of their own.
    for (char const* wf : {".github/workflows/build.yml", ".github/workflows/check.adapter.yml"})
    {
        std::string const text = Read(wf);
        Check(!text.empty(), std::string("can read ") + wf);
        Check(text.find("scripts/read-realm-pins.sh realm-pins.env") != std::string::npos,
              std::string(wf) + " reads its pins through scripts/read-realm-pins.sh");
        Check(!HasWorkflowPin(text), std::string(wf) + " carries no 40-hex pin of its own");
    }

    // The adapter check clones every module the pins file lists.
    std::string const adapter = Read(".github/workflows/check.adapter.yml");
    for (char const* mod : {"mod-ollama-chat", "mod-dungeon-clear", "mod-junk-to-gold",
                            "mod-ah-bot-plus"})
        Check(adapter.find(std::string("clone_module ") + mod) != std::string::npos,
              std::string("check.adapter.yml clones ") + mod);

    if (failures)
        return EXIT_FAILURE;
    std::cout << "realm-pins.env is the one copy of the pins and the reader fails closed\n";
    return EXIT_SUCCESS;
}
