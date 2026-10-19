// D2 battle (spec 24). Internal interface, exposed for unit tests.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/util.h"
#include "d2/data.h"

namespace d2 {

class Game;

namespace bt {

// Action codes. 0..6 are the enemy action-table kinds (spec 24 §2.1.1).
enum Action : int {
  kAttack = 0,
  kMagic = 1,
  kDefend = 2,
  kEscape = 3,   // enemy runs away
  kDouble = 4,
  kCharge = 5,
  kPoisonAtk = 6,
  kItem = 100,
  kNothing = 101,
  kDead = 102,   // "死亡中" (dead: fainted at turn start, used by the revive check)
  kFlee = 103,   // party "逃げる" (Flee) (slot 0 only)
};

enum DmgMode { kPhysical, kMagical, kAbsorb };

// One battler slot (spec 24 §1.1, §5).
struct Battler {
  bool used = false;
  bool enemy = false;
  bool alive = false;
  std::string name;  // SJIS
  int member = -1;   // party: character number
  int level = 0;
  // attack side
  bool atkElem[6] = {};
  bool addEff[7] = {};  // statuses 0..5 and 6 (death)
  int hitRate = 0, addRate = 0, critRate = 0;
  // defense side
  uint8_t resist[20] = {};
  uint8_t status[6] = {};
  uint8_t learned[30];
  int64_t hp = 0, maxHp = 0, mp = 0, maxMp = 0;
  int base[4] = {}, cur[4] = {};
  // per-turn
  bool defending = false;
  int charge = 0;
  int action = kNothing, param = 0, target = -1;
  bool interrupt = false;
  // enemy only
  int listCount = 0;
  uint8_t listKind[8] = {}, listParam[8] = {};
  bool flagA = false, revive = false, leader = false, act2 = false, act3 = false;
  bool counter[7] = {};
  uint8_t counterMagic[7] = {};
  int reviveCount = 0;
  int enemyNo = -1;
  int sizeKind = 0;  // drawing range (0 small 80x96, 1 large 160x128)
  int image = 0;
  int64_t exp = 0, gold = 0;
  int escapeRate = 0, dropRate = 0, dropItem = 0xFF;
  int64_t transformHp = 0;
  int transformTo = 0xFF;
  int x = 0, y = 0;
  // enemy graphics (EGD)
  EnemyImage img;
  int imgKind = 0;  // EGD 0x30 (used for the step-in footprint)

  Battler() { for (auto& l : learned) l = 0xFF; }
  bool canAct() const { return alive && !status[5] && !status[4] && !status[3]; }
};

// ---- pure formulas (spec 24 §12, §9.4, §10, §13) ----
int64_t variance(core::Rng& rng, int64_t v, int p);  // §12.16
int elementMultiplier(const bool elems[6], const uint8_t resist[20]);  // §12.1 step 4 (64 = x1)
int64_t applyMultiplier(int64_t d, int m);  // m>64: x2, m<64: /2 (m==0 handled by callers)
int64_t resistHpMp(int64_t v, int code);    // §12.10.1 step 5
int statusRate(int m, int resistCode);      // §12.6 steps 1-2
int lowerRate(int resistCode);              // §12.8
int64_t magicDamage(core::Rng& rng, const Magic& mg, const Battler& caster, const Battler& t, int m);  // §12.9
int64_t healAmount(core::Rng& rng, const Magic& mg, const Battler& caster);                           // §12.9
int evalVsParty(const Magic& mg, const Battler& p);                                                    // §9.4
int evalSelf(const Magic& mg, const Battler& caster, const Battler& e, bool isSelf);                  // §9.4
void actionOrder(core::Rng& rng, const Battler slots[10], int order[10]);                             // §10
int64_t attackValue(const Battler& self, const Battler& e);                                            // §13.2
bool magicBit(const Magic& mg, int n);

// Image data as drawn for a battler of size class `sizeKind` (24 §12.3 v1.1): each EGD plane's
// byte stream (EGD's own size) is read in rows of the class width (small 10 x 96, large 20 x 128);
// bytes past the plane's end are 0. Output: mask and 4 planes, each classW/8*classH bytes.
// An invalid image gives all-zero planes.
void classView(const EnemyImage& img, int sizeKind, std::vector<uint8_t>& mask, std::vector<uint8_t>& planes);

// Initialization helpers (spec 24 §5.1 / §5.2).
void initPartyBattler(Battler& b, const Member& m, int member, const Item items[256]);
void initEnemyBattler(Battler& b, const Enemy& e, int enemyNo);

}  // namespace bt
}  // namespace d2
