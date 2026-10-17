// D2 event interpreter internals (spec 22), exposed for unit tests.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace d2 {

class Game;

// Wait ticks for a move command (spec 22 §6.6 table; >= 20 -> 0).
int moveWaitTicks(int cmd);

// Condition of command 0x5A (spec 22 §7.3). a points at the 5 argument bytes.
bool evalCondition(Game& g, const uint8_t* a);

// Runs a script (L bytes incl. terminator) for event `self` with the §5 loop. Does not perform
// the start/end processing of §4.1/§4.2 (runEvent does). Returns 1 normal end, 2 ended by battle
// escape. May throw EventAbort / ReturnToTitle / QuitProgram.
int runScript(Game& g, int self, const uint8_t* script, size_t len);

// Skip search of spec 22 §5.3 on a work area: from q, find a command whose number is A or B and
// whose depth byte equals D. Returns its offset, or size-3 if none was found.
size_t skipTo(const std::vector<uint8_t>& work, size_t size, size_t q, int A, int B, uint8_t D);

}  // namespace d2
