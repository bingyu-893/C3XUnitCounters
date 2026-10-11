/* Shared by injected code and the standalone regression tests. All counter
 * arithmetic is integer-only; the engine's integer strengths stay untouched. */
#ifndef C3X_COUNTER_MATH_H
#define C3X_COUNTER_MATH_H

#define COUNTER_SCALE 1000000LL /* 100%; one millionth of the base strength */
#define COUNTER_MAX_SCALE 1000000000000LL /* Saturate at 1,000,000 times base. */

static long long
counter_multiply_percent (long long value, int percent)
{
	if ((value <= 0) || (percent <= 0))
		return 0;
	if (percent == 100)
		return value > COUNTER_MAX_SCALE ? COUNTER_MAX_SCALE : value;
	/* Test before multiplying, even for INT_MAX input. Preserve rule order. */
	if (value > COUNTER_MAX_SCALE * 100 / percent)
		return COUNTER_MAX_SCALE;
	value = value * percent / 100;
	/* A positive rule never becomes the explicit zero-percent case. */
	return value > 0 ? value : 1;
}

struct counter_ratio {
	unsigned long long numerator, denominator;
};

/* Defense/attack odds ratio, before final rounding. 0/0 keeps base odds;
 * one zero multiplier gives a zero/infinite ratio and the final clamp applies. */
static struct counter_ratio
counter_odds_ratio (int odds, long long attack, long long defense)
{
	if (odds < 1) odds = 1;
	if (odds > 1023) odds = 1023;
	if ((attack <= 0) && (defense <= 0))
		attack = defense = COUNTER_SCALE;
	if (attack < 0) attack = 0;
	if (defense < 0) defense = 0;
	if (attack > COUNTER_MAX_SCALE) attack = COUNTER_MAX_SCALE;
	if (defense > COUNTER_MAX_SCALE) defense = COUNTER_MAX_SCALE;
	return (struct counter_ratio) {
		(unsigned long long)odds * defense,
		(unsigned long long)(1024 - odds) * attack
	};
}

static int
counter_adjust_odds (int odds, long long attack, long long defense)
{
	struct counter_ratio r = counter_odds_ratio (odds, attack, defense);
	long long total = r.numerator + r.denominator;
	int result = (1024LL * r.numerator + total / 2) / total;
	return result < 1 ? 1 : (result > 1023 ? 1023 : result);
}

/* Selection scores retain their factors, so even INT_MAX base strengths and
 * saturated multipliers cannot overflow before the cross comparison. */
struct counter_strength_ratio {
	unsigned long long base, multiplier, bonus, divisor;
};

static int
counter_compare_products (unsigned long long * a, unsigned long long * b, int count)
{
	unsigned long long ap = 1, bp = 1, max = ~(unsigned long long)0;
	int fits = 1;
	for (int i = 0; i < count; i++) {
		if ((a[i] && ap > max / a[i]) || (b[i] && bp > max / b[i])) {
			fits = 0;
			break;
		}
		ap *= a[i]; bp *= b[i];
	}
	if (fits)
		return (ap > bp) - (ap < bp);
	/* Rare extreme case: base-2^32 long multiplication, using only 64-bit
	 * products and carries. Four 64-bit factors require at most eight limbs. */
	unsigned int limbs[2][8] = {{1}, {1}};
	for (int side = 0; side < 2; side++)
		for (int i = 0; i < count; i++) {
			unsigned long long factor = side ? b[i] : a[i];
			unsigned int result[8] = {0};
			for (int half = 0; half < 2; half++) {
				unsigned long long carry = 0, part = half ? factor >> 32 : (unsigned int)factor;
				for (int j = 0; j + half < 8; j++) {
					unsigned long long value = (unsigned long long)limbs[side][j] * part + result[j + half] + carry;
					result[j + half] = (unsigned int)value;
					carry = value >> 32;
				}
			}
			for (int j = 0; j < 8; j++) limbs[side][j] = result[j];
		}
	for (int j = 7; j >= 0; j--)
		if (limbs[0][j] != limbs[1][j])
			return (limbs[0][j] > limbs[1][j]) ? 1 : -1;
	return 0;
}

static struct counter_strength_ratio
counter_strength (int base, long long multiplier, long long divisor, int bonus)
{
	// The 0% / 0% case preserves the base selection metric as well as base odds.
	if (! multiplier && ! divisor) multiplier = divisor = COUNTER_SCALE;
	return (struct counter_strength_ratio) {
		base > 0 ? base : 0, multiplier, bonus > 0 ? bonus : 0, divisor
	};
}

static int
counter_compare_strengths (struct counter_strength_ratio a, struct counter_strength_ratio b)
{
	if (! a.divisor || ! b.divisor)
		return (! a.divisor) - (! b.divisor);
	unsigned long long left[] = {a.base, a.multiplier, a.bonus, b.divisor};
	unsigned long long right[] = {b.base, b.multiplier, b.bonus, a.divisor};
	return counter_compare_products (left, right, 4);
}

#endif
