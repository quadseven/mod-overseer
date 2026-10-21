#include "overseer_decisions.h"
#include <cstdio>
#include <vector>
using OverseerDecisions::NextProfessionStep;
using OverseerDecisions::ProfessionHolding;
using OverseerDecisions::ProfessionStep;
using OverseerDecisions::ProfessionStepKind;
unsigned const ALCHEMY=171, HERBALISM=182, TAILORING=197, ENCHANTING=333;
ProfessionHolding Held(unsigned s, unsigned v){ProfessionHolding h;h.skill=s;h.value=v;return h;}
char const* Kn(ProfessionStepKind k){switch(k){case ProfessionStepKind::Nothing:return"Nothing";case ProfessionStepKind::GiveUp:return"GiveUp";case ProfessionStepKind::Take:return"Take";}return"?";}
int main(){
    struct{char const*who;std::vector<unsigned>w;std::vector<ProfessionHolding>h;unsigned mp;} cases[]={
        {"og-start",{TAILORING,ENCHANTING},{Held(ALCHEMY,1),Held(HERBALISM,117)},2},
        {"og-after-unlearn",{TAILORING,ENCHANTING},{Held(HERBALISM,117)},2},
        {"og-after-tailor",{TAILORING,ENCHANTING},{Held(HERBALISM,117),Held(TAILORING,1)},2},
        {"og-after-herb",{TAILORING,ENCHANTING},{Held(TAILORING,1)},2},
        {"og-converged",{TAILORING,ENCHANTING},{Held(TAILORING,1),Held(ENCHANTING,1)},2},
    };
    for(auto const&c:cases){
        ProfessionStep s=NextProfessionStep(c.w,c.h,c.mp);
        printf("%s -> %s skill %u cost %u\n",c.who,Kn(s.kind),s.skill,s.cost);
    }
    return 0;
}
