// D2 battle: pure formulas and battler initialization (spec 24 §5, §9.4, §10, §12, §13).
#include <algorithm>
#include <cstring>

#include "core/text.h"
#include "d2/battle.h"

namespace d2 {
namespace bt {

bool magicBit(const Magic& mg, int n) { return mg.hasEffect(n); }

int64_t variance(core::Rng& rng, int64_t v, int p) {
  if (p <= 0) return v;
  v += rng.next() % 3;
  const int64_t t = v * p / 12;
  if (t > 1) v += t / 2 - rng.next() % t;
  return v;
}

int elementMultiplier(const bool elems[6], const uint8_t resist[20]) {
  int m = 64;
  for (int e = 0; e < 6; ++e) {
    if (!elems[e]) continue;
    switch (resist[14 + e]) {
      case 1: m /= 2; break;
      case 2: m = 0; break;
      case 3: m *= 2; break;
      default: break;
    }
  }
  return m;
}

int64_t applyMultiplier(int64_t d, int m) {
  if (m > 64) return d * 2;
  if (m < 64) return d / 2;
  return d;
}

int64_t resistHpMp(int64_t v, int code) {
  switch (code) {
    case 1: return v / 2;
    case 2: return 0;
    case 3: return v * 2;
    default: return v;
  }
}

int statusRate(int m, int code) {
  int rate = m > 64 ? 100 : (m == 64 ? 90 : (m == 0 ? 0 : 60));
  if (code == 1) rate -= 30;
  else if (code == 2) rate = 0;
  else if (code == 3) rate += 20;
  return rate;
}

int lowerRate(int code) {
  switch (code) {
    case 1: return 60;
    case 2: return 0;
    case 3: return 100;
    default: return 90;
  }
}

static inline int64_t s16(int64_t v) { return int64_t(int16_t(uint16_t(v & 0xFFFF))); }

int64_t magicDamage(core::Rng& rng, const Magic& mg, const Battler& c, const Battler& t, int m) {
  int64_t d = mg.power;
  const int a = mg.atkCoef, sp = mg.spiCoef;
  if (a) {
    if (mg.hasEffect(20))
      d += c.hp * a / 4;
    else
      d += s16(int64_t(c.cur[0]) * a) / 10;
    if (!mg.hasEffect(21)) d -= s16(int64_t(t.cur[1]) * a) / 20;
  }
  if (sp) {
    d += s16(int64_t(c.cur[2]) * sp) / 10;
    if (!mg.hasEffect(21)) d -= s16(int64_t(t.cur[2]) * sp) / 40;
  }
  d = applyMultiplier(d, m);
  if (m == 0) return 0;
  if (d < 2) return rng.next() % 2;
  return variance(rng, d, mg.variance);
}

int64_t healAmount(core::Rng& rng, const Magic& mg, const Battler& c) {
  int64_t d = mg.power;
  if (mg.atkCoef) d += s16(int64_t(c.cur[0]) * mg.atkCoef) / 10;
  if (mg.spiCoef) d += s16(int64_t(c.cur[2]) * mg.spiCoef) / 10;
  if (d < 2) return rng.next() % 2;
  return variance(rng, d, mg.variance);
}

static bool anyBit0to19(const Magic& mg) {
  for (int n = 0; n < 20; ++n)
    if (mg.hasEffect(n)) return true;
  return false;
}

int evalVsParty(const Magic& mg, const Battler& p) {
  if (!p.alive) return 0;
  if (mg.hasEffect(0) || (mg.hasEffect(1) && p.mp != 0)) return 100;
  for (int s = 0; s < 6; ++s)
    if (!p.status[s] && mg.hasEffect(2 + s)) return 80;
  if (mg.hasEffect(8)) return 80;
  for (int k = 0; k < 4; ++k)
    if (mg.hasEffect(10 + k) && p.cur[k] > p.base[k] / 2) return 100;
  if (anyBit0to19(mg)) return 1;
  return 60;
}

int evalSelf(const Magic& mg, const Battler& caster, const Battler& e, bool isSelf) {
  if (caster.flagA && !isSelf) return 0;
  if (!e.alive) return mg.hasEffect(8) ? 200 : 0;
  if (mg.hasEffect(0)) {
    if (e.hp < e.maxHp / 4) return 120;
    if (e.hp < e.maxHp / 2) return 80;
    if (e.hp / 3 < e.maxHp / 4) return 40;
    return 1;
  }
  for (int s = 0; s < 6; ++s)
    if (e.status[s] && mg.hasEffect(2 + s)) return s * 20 + 80;
  for (int k = 0; k < 4; ++k)
    if (mg.hasEffect(10 + k) && e.cur[k] < e.base[k] * 2 + 10) return 100;
  if (anyBit0to19(mg)) return 1;
  return 60;
}

void actionOrder(core::Rng& rng, const Battler slots[10], int order[10]) {
  int64_t key[10];
  for (int s = 0; s < 10; ++s) {
    order[s] = s;
    int64_t k = slots[s].used ? int64_t(slots[s].cur[3]) + (slots[s].alive ? 1 : 0) : 0;
    if (k > 0) k += rng.next() % k;
    key[s] = k;
  }
  for (int i = 0; i < 9; ++i)
    for (int j = i + 1; j < 10; ++j)
      if (key[i] < key[j]) {
        std::swap(key[i], key[j]);
        std::swap(order[i], order[j]);
      }
}

int64_t attackValue(const Battler& self, const Battler& e) {
  if (!e.alive) return 0;
  int64_t d = int64_t(self.cur[0]) - e.cur[1] / 2;
  const int m = elementMultiplier(self.atkElem, e.resist);
  d = applyMultiplier(d, m);
  if (e.hp < d) d += d / 5;
  return d;
}

void classView(const EnemyImage& img, int sizeKind, std::vector<uint8_t>& mask, std::vector<uint8_t>& planes) {
  const size_t n = size_t(sizeKind ? 20 : 10) * (sizeKind ? 128 : 96);
  mask.assign(n, 0);
  planes.assign(4 * n, 0);
  if (!img.valid || img.w <= 0) return;
  const size_t src = size_t(img.w / 8) * img.h;  // bytes per plane in the EGD
  for (size_t i = 0; i < n && i < src; ++i) {
    if (i < img.mask.size()) mask[i] = img.mask[i];
    for (int p = 0; p < 4; ++p) {
      const size_t o = p * src + i;
      if (o < img.planes.size()) planes[p * n + i] = img.planes[o];
    }
  }
}

void initPartyBattler(Battler& b, const Member& m, int member, const Item items[256]) {
  b = Battler();
  b.used = true;
  b.enemy = false;
  b.member = member;
  b.name = core::rawName(reinterpret_cast<const uint8_t*>(m.name), 20);
  b.level = m.level;
  const Item& w = items[m.equip[0] == 0xFF ? 255 : m.equip[0]];
  for (int e = 0; e < 6; ++e) b.atkElem[e] = (w.elements() >> e) & 1;
  const uint8_t ae = w.addEffect();
  b.addEff[0] = ae & 0x01;
  b.addEff[1] = ae & 0x02;
  b.addEff[2] = ae & 0x04;
  b.addEff[3] = ae & 0x08;
  b.addEff[5] = ae & 0x10;
  b.addEff[6] = ae & 0x20;
  b.hitRate = w.hitRate();
  b.addRate = w.addRate();
  b.critRate = 5;
  for (int s = 1; s <= 4; ++s) {
    const Item& a = items[m.equip[s] == 0xFF ? 255 : m.equip[s]];
    for (int e = 0; e < 6; ++e)
      if ((a.elements() >> e) & 1) b.resist[14 + e] = 1;
  }
  b.alive = m.alive != 0;
  std::memcpy(b.status, m.status, 6);
  std::memcpy(b.learned, m.learned, 30);
  b.hp = m.hp;
  b.mp = m.mp;
  b.maxHp = m.maxHp;
  b.maxMp = m.maxMp;
  for (int k = 0; k < 4; ++k) b.base[k] = b.cur[k] = m.stat[k];
}

void initEnemyBattler(Battler& b, const Enemy& e, int enemyNo) {
  const int x = b.x, y = b.y;
  EnemyImage img = std::move(b.img);
  const int imgKind = b.imgKind;
  b = Battler();
  b.x = x;
  b.y = y;
  b.img = std::move(img);
  b.imgKind = imgKind;
  b.used = true;
  b.enemy = true;
  b.alive = true;
  b.enemyNo = enemyNo;
  b.name = core::rawName(reinterpret_cast<const uint8_t*>(e.name), 17);
  for (int i = 0; i < 8; ++i) {
    const int k = e.actions[2 * i], p = e.actions[2 * i + 1];
    if (k <= 6) {
      b.listKind[b.listCount] = uint8_t(k);
      b.listParam[b.listCount] = uint8_t(p);
      ++b.listCount;
    } else if (k == 7) {
      b.flagA = true;
    } else if (k == 8) {
      b.revive = true;
    } else if (k == 9) {
      b.leader = true;
    } else if (k == 10) {
      b.critRate = 30;  // set below
    } else if (k == 11) {
      b.hitRate = 70;
    } else if (k == 12) {
      b.act2 = true;
    } else if (k == 13) {
      b.act3 = true;
    } else if (k >= 14 && k <= 20) {
      b.counter[k - 14] = true;
      b.counterMagic[k - 14] = uint8_t(p);
    }
  }
  b.hitRate = b.hitRate == 70 ? 70 : 95;
  b.critRate = b.critRate == 30 ? 30 : 5;
  for (int i = 0; i < 20; ++i) b.resist[i] = uint8_t(e.resistance(i));
  b.hp = b.maxHp = e.maxHp;
  b.mp = b.maxMp = e.mp;
  b.base[0] = b.cur[0] = e.atk;
  b.base[1] = b.cur[1] = e.def;
  b.base[2] = b.cur[2] = e.spi;
  b.base[3] = b.cur[3] = e.spd;
  b.sizeKind = e.size ? 1 : 0;
  b.image = e.image;
  b.exp = e.exp;
  b.gold = e.gold;
  b.escapeRate = e.escapeRate;
  b.dropRate = e.dropRate;
  b.dropItem = e.dropItem;
  b.transformHp = e.transformHp;
  b.transformTo = e.transformTo;
}

}  // namespace bt
}  // namespace d2
