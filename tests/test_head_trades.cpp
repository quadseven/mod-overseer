/*
 * A trade opened to a selfbot head is answered like a person would.
 *
 * wow-dev 2026-10-04: Zug stood with a family member's trade window open and
 * a belt offered, because playerbots leaves a selfbot's trades to a human.
 */

#include "overseer_decisions.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using OverseerDecisions::AnswerTradeAtHead;
using OverseerDecisions::HeadTradeAnswer;

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

void TheDecision()
{
    Check("a family gift is accepted",
          AnswerTradeAtHead(true, 1, 0, 0, 0, 0) == HeadTradeAnswer::Accept);
    Check("gold from family is accepted",
          AnswerTradeAtHead(true, 0, 500, 0, 0, 5) == HeadTradeAnswer::Accept);
    Check("an empty family window waits",
          AnswerTradeAtHead(true, 0, 0, 0, 0, 10) == HeadTradeAnswer::Wait);
    Check("an empty family window is closed after a minute",
          AnswerTradeAtHead(true, 0, 0, 0, 0, 60) == HeadTradeAnswer::Decline);
    Check("a stranger is declined after a moment",
          AnswerTradeAtHead(false, 1, 0, 0, 0, 3) == HeadTradeAnswer::Decline);
    Check("not in the first instant",
          AnswerTradeAtHead(false, 1, 0, 0, 0, 0) == HeadTradeAnswer::Wait);
    Check("a head never gives through someone else's window",
          AnswerTradeAtHead(true, 1, 0, 1, 0, 0) == HeadTradeAnswer::Decline);
    Check("nor pays",
          AnswerTradeAtHead(true, 1, 0, 0, 100, 0) == HeadTradeAnswer::Decline);
}

void TheAdapterIsWired()
{
    std::ifstream in("src/mod_overseer.cpp");
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string const source = buffer.str();
    Check("the module source is readable (run from the repo root)", !source.empty());
    Check("the poll answers head trades", source.find("DriveHeadTrades();") != std::string::npos);
    Check("through the decision",
          source.find("OverseerDecisions::AnswerTradeAtHead(") != std::string::npos);
}

}  // namespace

int main()
{
    TheDecision();
    TheAdapterIsWired();
    if (failures)
        return 1;
    std::printf("ok test_head_trades\n");
    return 0;
}
