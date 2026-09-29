"""Replay the actual IDLE hearth handler with lightweight core stand-ins."""

from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
src = (root / 'src/mod_overseer.cpp').read_text()
# AI-REVIEW 2026-09-29 [deepseek-v4.1-flash] elder#1 (bot review, PR #801):
# Diagnose stale extraction anchors separately from handler failures.
if "episode.run = coord;" not in src:
    raise SystemExit("EXIT must preserve the original coordinator")
try:
    start = src.index("            {\n                auto const episode = _exitHearths.find(family);")
    end = src.index("\n\n            if (!IsDungeonJob(leaderJob)", start)
except ValueError as exc:
    raise SystemExit("IDLE hearth handler block not found; update the replay anchors") from exc
block=src[start:end]
prefix=r'''
#include <cassert>
#include <map>
#include <string>
#include <vector>
#include <utility>
#define LOG_WARN(...)
using uint32 = unsigned;
struct DungeonRunCoordinatorState { unsigned campaignId=0,runNumber=0,runId=0,runsWanted=0; bool capKnown=false,provedComplete=false,evacuated=false,leaderClientLostOutcome=false; std::string portalKeyword,stalledReason; };
struct ExitHearthEpisode {unsigned mapId=0,runId=0; DungeonRunCoordinatorState run;};
struct Player {unsigned map; bool inWorld=true; unsigned GetMapId(){return map;} bool IsInWorld(){return inWorld;}};
Player outside{1},inside{389};
// AI-REVIEW 2026-09-29 [deepseek-v4.1-flash] elder#2 (bot review, PR #801):
// Resolve each member independently so one unseen or inside member blocks finalization.
std::map<std::string,Player*> seen{{"Zug",&outside},{"Oz",&outside}};
namespace ObjectAccessor { Player* FindPlayerByName(std::string const& name){auto p=seen.find(name);return p==seen.end()?nullptr:p->second;} }
struct DungeonPortal{}; DungeonPortal portal;
DungeonPortal const* FindDungeonPortal(std::string const&){return &portal;}
bool IsDungeonJob(std::string const&){return true;}
namespace OverseerDecisions { const char* DungeonRunExitOutcome(bool complete,bool stalled,bool evacuated){return evacuated?"evacuated":complete?"complete":stalled?"stalled":"left";} }
std::map<std::string,ExitHearthEpisode> _exitHearths;
DungeonRunCoordinatorState coord; int finalized=0; std::string outcome;
bool DriveExitHearths(std::string const&,std::string const&,std::vector<std::string> const&,ExitHearthEpisode&,const char*){return true;}
void EndRunAndDecide(DungeonRunCoordinatorState& c,std::string const&,DungeonPortal const&,unsigned id,const char* o,std::string const&,bool,std::vector<std::string> const*){assert(id==71); assert(c.campaignId==34 && c.runNumber==1 && c.runsWanted==50 && c.capKnown); finalized++; outcome=o;}
void Poll(Player* activeInside){std::string family="Zug",leaderName="Zug",leaderJob="dungeon:ragefire"; std::vector<std::string> members{"Zug","Oz"};
'''
suffix=r'''
}
int main(){ExitHearthEpisode e; e.mapId=389;e.runId=71;e.run.campaignId=34;e.run.runNumber=1;e.run.runId=71;e.run.runsWanted=50;e.run.capKnown=true;e.run.provedComplete=true;e.run.portalKeyword="ragefire";
_exitHearths["Zug"]=e;Poll(nullptr);assert(finalized==1 && outcome=="complete");assert(_exitHearths.empty());Poll(nullptr);assert(finalized==1);
_exitHearths["Zug"]=e;seen["Oz"]=&inside;Poll(&inside);assert(finalized==1 && _exitHearths.size()==1);
seen.erase("Oz");Poll(nullptr);assert(finalized==1 && _exitHearths.size()==1);
seen["Oz"]=&outside;_exitHearths["Zug"].run.provedComplete=false;_exitHearths["Zug"].run.stalledReason="lost progress";Poll(nullptr);assert(finalized==2 && outcome=="stalled");
}
'''
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "replay.cpp"
    binary = Path(directory) / "replay"
    source.write_text(prefix + block + suffix)
    # AI-REVIEW 2026-09-29 [deepseek-v4.1-flash] elder#3 (bot review, PR #801):
    # Bound both subprocesses so a wedged compiler or replay fails promptly.
    subprocess.run(["g++", "-std=c++17", str(source), "-o", str(binary)],
                   check=True, timeout=30)
    subprocess.run([str(binary)], check=True, timeout=5)
print("PASS: completed, stalled, still-inside, unseen, and once-only finalization")
