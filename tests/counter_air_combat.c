/* Uses the real rule evaluator, cache, odds math, context wrapper, and layouts.
 * Engine dependencies are deterministic stubs; optional fixtures run the actual
 * three executable air-combat loops with only their external calls redirected. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "C3X.h"
#include "counter_math.h"
#define __ 0
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static struct injected_state state, *is = &state;
static BIC bic, *p_bic_data = &bic;
static UnitType types[5];
static Unit interceptor, mission, nested_mission;
static Fighter fighter, other_fighter;
static Tile target, base_tile, null_tile, *p_null_tile = &null_tile;
static Tile_vtable tile_vtable;
static struct counter_rule rules[40];
static int base_odds = 512, last_odds, base_calls, tile_calls, city_calls, terrain_calls;
static int mode, target_city, target_district, target_terrain = SQ_Hills;
static int online, player_civ = 1, rng_calls, random_value = 600, destroy_calls, message_calls;
static int animation_calls, visible, depth, early_strengths;
static int popup_calls, popup_check_nested;
static char popup_text[1024];
static void (__fastcall *native_air)(Fighter *, int, Unit *, Unit *, int, int);

static char __stdcall is_online_game(void) { return online; }
static int WINAPI capture_message_box(HWND window, LPCSTR text, LPCSTR title, UINT flags) {
	assert(!online);
	assert(!strcmp(title, "C3X - Air combat counter test"));
	assert(flags == (MB_OK | MB_ICONINFORMATION));
	/* The dialog must see the restored scope, including when the inner combat
	 * is reporting while the outer combat is still alive. */
	if (popup_check_nested && strstr(text, "Mission target: (70, 80)")) {
		assert(is->counter_air_combat_ctx.fighter == &fighter);
		assert(is->counter_air_combat_ctx.tile_x == 30 && is->counter_air_combat_ctx.tile_y == 40);
	} else {
		assert(is->counter_air_combat_ctx.fighter == &other_fighter);
		assert(!is->counter_air_combat_ctx.debug_report);
	}
	assert(strlen(text) < sizeof popup_text);
	strcpy(popup_text, text); popup_calls++;
	return IDOK;
}
#define MessageBoxA capture_message_box

static Tile * tile_at(int x, int y) {
	tile_calls++;
	if (x == 30 && y == 40) return &target;
	if (x == -1) return p_null_tile;
	return &base_tile;
}
static bool Tile_has_city(Tile *t) { city_calls++; return t == &target && target_city; }
static enum SquareTypes __fastcall get_terrain(Tile *t) {
	terrain_calls++; return t == &target ? target_terrain : SQ_Grassland;
}
static int __fastcall has_mine(Tile *t, int edx, int n) { return 0; }
static bool __fastcall has_irrigation(Tile *t, int edx, int n) { return false; }
static bool tile_matches_square_type(Tile *t, enum SquareTypes s) { return false; }
static bool tile_is_lake(Tile *t) { return false; }
static bool district_is_complete(Tile *t, int id) { return t == &target && target_district == id; }
static bool unit_type_has_tag(struct c3x_config *cfg, int type, int tag) { return tag == 0 && type == 0; }
static unsigned int square_type_mask_bit(enum SquareTypes t) { return 1U << t; }
static unsigned int district_buildable_mine_mask_bit(void) { return 1U << 27; }
static unsigned int district_buildable_irrigation_mask_bit(void) { return 1U << 28; }
static unsigned int district_buildable_lake_mask_bit(void) { return 1U << 29; }
static int __fastcall Fighter_get_combat_odds(Fighter *f, int edx, Unit *a, Unit *d, bool b, bool ignore) {
	base_calls++;
	assert(ignore); /* Native air loop must retain false/true flags. */
	return base_odds;
}
void __fastcall patch_Fighter_do_air_combat(Fighter *, int, Unit *, Unit *, int, int);
int __fastcall patch_Fighter_get_odds_for_air_combat(Fighter *, int, Unit *, Unit *, bool, bool);
static void __fastcall Fighter_do_air_combat(Fighter *f, int edx, Unit *a, Unit *d, int x, int y) {
	if (mode == 1) return; /* Native early return, no odds call. */
	if (mode == 3) { native_air(f, 0, a, d, x, y); return; }
	if (mode == 2 && !depth) {
		struct counter_air_combat_context outer = is->counter_air_combat_ctx;
		depth++;
		patch_Fighter_do_air_combat(f, 0, a, &nested_mission, 70, 80);
		assert(last_odds == base_odds); /* Nested target must not inherit hills. */
		assert(!memcmp(&outer, &is->counter_air_combat_ctx, sizeof outer));
		depth--;
		/* Simulate a HUD/base odds probe during the outer combat. */
		assert(Fighter_get_combat_odds(f, 0, a, d, false, true) == base_odds);
		assert(patch_Fighter_get_odds_for_air_combat(&other_fighter, 0, a, d, false, true) == base_odds);
		assert(patch_Fighter_get_odds_for_air_combat(f, 0, a, &nested_mission, false, true) == base_odds);
	}
	last_odds = patch_Fighter_get_odds_for_air_combat(f, 0, a, d, false, true);
	if (mode == 4) { /* Simulate aircraft becoming invalid during destruction. */
		a->Body.UnitTypeID = d->Body.UnitTypeID = -1;
	}
}

#include "counter_air_generated.h"

static void reset_rules(int count) {
	free_counter_rule_match_cache();
	memset(rules, 0, sizeof rules);
	for (int i = 0; i < ARRAY_LEN(rules); i++) {
		rules[i].attacker_match = 0; rules[i].defender_match = 1;
		rules[i].self_atk_pct = rules[i].self_def_pct = 100;
		rules[i].enemy_atk_pct = rules[i].enemy_def_pct = 100;
		rules[i].self_bombard_pct = rules[i].enemy_bombard_pct = 100;
		rules[i].district_id = -1;
	}
	is->current_config.enable_unit_counters = true;
	is->current_config.debug_air_combat_counters = false;
	is->current_config.count_counter_rules = count;
	is->current_config.counter_rules = rules;
	mode = base_calls = tile_calls = city_calls = terrain_calls = 0;
	target_city = target_district = 0;
	base_odds = 512;
	mission.Body.UnitTypeID = 1;
}

static int combat(void) {
	struct counter_air_combat_context saved = is->counter_air_combat_ctx;
	struct counter_defender_selection_context ground = is->counter_defender_selection_ctx;
	struct counter_army_attacker_selection_context army = is->counter_army_attacker_selection_ctx;
	Fighter old_fighter = fighter;
	UnitType old_types[5]; memcpy(old_types, types, sizeof types);
	int ax = interceptor.Body.X, ay = interceptor.Body.Y, dx = mission.Body.X, dy = mission.Body.Y;
	patch_Fighter_do_air_combat(&fighter, 0, &interceptor, &mission, 30, 40);
	assert(!memcmp(&saved, &is->counter_air_combat_ctx, sizeof saved));
	assert(!memcmp(&ground, &is->counter_defender_selection_ctx, sizeof ground));
	assert(!memcmp(&army, &is->counter_army_attacker_selection_ctx, sizeof army));
	assert(!memcmp(&old_fighter, &fighter, sizeof fighter));
	assert(!memcmp(old_types, types, sizeof types));
	assert(ax == interceptor.Body.X && ay == interceptor.Body.Y && dx == mission.Body.X && dy == mission.Body.Y);
	return last_odds;
}

static void rule_tests(void) {
	reset_rules(1); rules[0].self_atk_pct = 150; assert(combat() == 410);
	assert(base_calls == 1 && tile_calls == 1 && terrain_calls == 0 && city_calls == 0);
	reset_rules(1); rules[0].enemy_def_pct = 150; assert(combat() == 614);
	reset_rules(1); rules[0].attacker_match = 1; rules[0].defender_match = 0;
	rules[0].enemy_atk_pct = 150; assert(combat() == 410);
	rules[0].enemy_atk_pct = 100; rules[0].self_def_pct = 150; assert(combat() == 614);
	reset_rules(1); rules[0].self_def_pct = rules[0].enemy_atk_pct = 900; assert(combat() == 512);
	/* Stealth and normal aircraft use exactly the same post-interception rules. */
	for (int type = 1; type <= 2; type++) {
		reset_rules(1); mission.Body.UnitTypeID = type; rules[0].defender_match = type;
		rules[0].self_atk_pct = 125;
		for (int a = 0; a < 4; a++) for (int d = 0; d < 4; d++) {
			interceptor.Body.Combat_Experience = a; mission.Body.Combat_Experience = d;
			rules[0].self_experience_mask = 1U << 2; rules[0].enemy_experience_mask = 1U << 1;
			assert(combat() == (a == 2 && d == 1 ? 455 : 512));
		}
	}
	reset_rules(1); rules[0].attacker_match = 1; rules[0].defender_match = 0;
	rules[0].self_experience_mask = 1U << 3; rules[0].enemy_experience_mask = 1U << 2;
	mission.Body.Combat_Experience = 3; interceptor.Body.Combat_Experience = 2;
	rules[0].self_def_pct = 150; assert(combat() == 614);
	mission.Body.Combat_Experience = -1; assert(combat() == 512);
	reset_rules(1); rules[0].self_atk_pct = 125; rules[0].terrain_mask = 1U << SQ_Hills;
	rules[0].only_in_city = true;
	assert(combat() == 512); target_city = 1; assert(combat() == 455);
	target_terrain = SQ_Grassland; assert(combat() == 512); target_terrain = SQ_Hills;
	rules[0].district_name = "Aerodrome"; rules[0].district_id = 3;
	is->current_config.enable_districts = true;
	assert(combat() == 512); target_district = 3; assert(combat() == 455);
	reset_rules(1); rules[0].self_atk_pct = 150; rules[0].terrain_mask = 1U << SQ_Hills;
	mode = 2; assert(combat() == 410); mode = 1; combat();
	assert(is->counter_air_combat_ctx.fighter == &other_fighter);
	reset_rules(1); rules[0].attacker_match = 3; rules[0].self_atk_pct = 200;
	for (base_odds = 1; base_odds <= 1023; base_odds++) assert(combat() == base_odds);
	reset_rules(0); combat(); assert(tile_calls == 0);
	reset_rules(1); is->current_config.enable_unit_counters = false; combat(); assert(tile_calls == 0);
	mode = 2; combat(); assert(tile_calls == 0); /* Disabled nested scope also restores. */
	reset_rules(1); mission.Body.UnitTypeID = -1; combat(); assert(tile_calls == 0);
	reset_rules(1); rules[0].self_atk_pct = 150;
	assert(patch_Fighter_get_odds_for_air_combat(&fighter, 0, &interceptor, &mission, false, true) == 512);
	assert(tile_calls == 0); /* No current air scope: direct probe is neutral. */
	struct counter_air_combat_context saved = is->counter_air_combat_ctx;
	patch_Fighter_do_air_combat(&fighter, 0, &interceptor, &mission, -1, -1);
	assert(last_odds == 512 && !memcmp(&saved, &is->counter_air_combat_ctx, sizeof saved));
	reset_rules(1); rules[0].attacker_match = UCM_TAG; rules[0].attacker_tag_id = 0;
	rules[0].self_atk_pct = 150; assert(combat() == 410);
	reset_rules(1); rules[0].self_atk_pct = 150; mission.Body.UnitTypeID = 4; assert(combat() == 410);
	/* Cross the bitmap word boundary; saturation makes evaluation order visible. */
	reset_rules(34); rules[0].self_atk_pct = 2000000000;
	rules[31].self_atk_pct = 200; rules[32].self_atk_pct = 1; rules[33].self_atk_pct = 1;
	struct counter_effect_summary effects;
	get_counter_effect_summary(&is->current_config, &interceptor, &mission, &target, &effects);
	assert(effects.attacker_atk_pct == 100000000LL);
	reset_rules(2); rules[0].self_atk_pct = rules[1].self_atk_pct = 125; assert(combat() == 400);
	for (int i = 0; i < 2; i++) { rules[i].only_in_city = true; rules[i].terrain_mask = 1U << SQ_Hills; }
	target_city = 1; city_calls = terrain_calls = 0; assert(combat() == 400);
	assert(city_calls == 1 && terrain_calls == 1);
	reset_rules(1); rules[0].self_atk_pct = 0; assert(combat() == 1023);
	rules[0].enemy_def_pct = 0; assert(combat() == 512);
	rules[0].self_atk_pct = 100; assert(combat() == 1);
	puts("PASS: rules, normal/stealth, all experience pairs, target environment, cache order, neutral paths, nested/HUD/context restoration");
}

static void debug_popup_tests(void) {
	reset_rules(1); online = popup_calls = 0;
	rules[0].self_atk_pct = 1000;
	assert(combat() == 93 && popup_calls == 0);
	is->current_config.debug_air_combat_counters = true;
	int before = base_calls;
	assert(combat() == 93 && base_calls == before + 1 && popup_calls == 1);
	assert(strstr(popup_text, "Interceptor ATK: 1000% of base"));
	assert(strstr(popup_text, "Mission aircraft DEF: 100% of base"));
	assert(strstr(popup_text, "50.00% -> 90.92%"));
	assert(strstr(popup_text, "512/1024 -> 93/1024"));
	assert(strstr(popup_text, "Mission target: (30, 40)"));
	assert(strstr(popup_text, "Base: (10, 12)"));
	assert(strstr(popup_text, "Base: (20, 22)"));
	assert(strstr(popup_text, "ENABLED; Loaded rules: 1"));
	mode = 4; combat();
	assert(strstr(popup_text, "Interceptor (ATTACKER): Interceptor"));
	assert(strstr(popup_text, "Mission aircraft (DEFENDER): Bomber"));
	interceptor.Body.UnitTypeID = 0; mission.Body.UnitTypeID = 1; mode = 0;
	free_counter_rule_match_cache(); rules[0].attacker_match = 3;
	assert(combat() == 512 && strstr(popup_text, "Net ATK/DEF effect: UNCHANGED"));
	is->current_config.enable_unit_counters = false; tile_calls = 0;
	assert(combat() == 512 && strstr(popup_text, "DISABLED") && !tile_calls);
	is->current_config.enable_unit_counters = true; is->current_config.count_counter_rules = 0;
	assert(combat() == 512 && strstr(popup_text, "Loaded rules: 0") && !tile_calls);
	mode = 1; combat(); assert(strstr(popup_text, "no round odds were captured"));
	reset_rules(1); rules[0].self_atk_pct = 150; rules[0].terrain_mask = 1U << SQ_Hills;
	is->current_config.debug_air_combat_counters = true; mode = 2; popup_check_nested = 1;
	before = popup_calls; assert(combat() == 410 && popup_calls == before + 2);
	assert(strstr(popup_text, "Mission target: (30, 40)")); popup_check_nested = 0;
	mode = 0; online = 1; before = popup_calls;
	assert(combat() == 410 && popup_calls == before);
	online = 0;
	puts("PASS: debug reports actual first-round bonus/odds, disabled/empty/neutral/early exits, destroyed aircraft, nested scopes, offline-only");
}

/* External dependencies of the real native loops. RNG must be called exactly
 * once after each odds query, with the original bound 1024. */
static int __fastcall engine_strength(Unit *u) { return early_strengths ? 0 : 1; }
static int __fastcall engine_hp(Unit *u) { return 2 + u->Body.Combat_Experience; }
static int __fastcall engine_rng(void *r, int edx, int limit) {
	assert(limit == 1024 && base_calls == rng_calls + 1); rng_calls++; return random_value;
}
static bool __stdcall engine_online(void) { return online; }
static bool __fastcall engine_authority(void *p) { return false; }
static bool __fastcall engine_visible(void *p, int edx, int x, int y, Unit *d) {
	assert(x == 30 && y == 40 && d == &mission); return visible;
}
static void __fastcall engine_animation(Unit *d, int edx, int x, int y, Unit *a, bool won) {
	assert(x == 30 && y == 40 && a == &interceptor && d == &mission); animation_calls++;
}
static void __fastcall engine_destroy(Fighter *f, int edx, Unit *a, Unit *d, int x, int y, bool won) {
	assert(x == 30 && y == 40 && a == &interceptor && d == &mission); destroy_calls++;
}
static void __fastcall engine_message(void *p, int edx, int a_id, int a, int b, int won, int kind, int x, int y, int c, int d_id) {
	assert(x == 30 && y == 40 && kind == 6 && a_id == interceptor.Body.ID && d_id == mission.Body.ID); message_calls++;
}
struct native_fixture { char *name; unsigned char *code; int size, calls[12], player_ref; };
#include "counter_air_native.h"

static void native_tests(void) {
	void *targets[] = {engine_strength, engine_strength, patch_Fighter_get_odds_for_air_combat,
	                  engine_rng, engine_hp, engine_hp, engine_online, engine_authority,
	                  engine_message, engine_visible, engine_animation, engine_destroy};
	for (int n = 0; n < NATIVE_COUNT; n++) {
		struct native_fixture *fx = &fixtures[n];
		byte *code = VirtualAlloc(NULL, fx->size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
		assert(code); memcpy(code, fx->code, fx->size);
		for (int i = 0; i < 12; i++) *(unsigned int *)(code + fx->calls[i]) = (unsigned int)targets[i] - (unsigned int)(code + fx->calls[i] + 4);
		*(int **)(code + fx->player_ref) = &player_civ;
		FlushInstructionCache(GetCurrentProcess(), code, fx->size);
		native_air = (void *)code;
		for (int debug = 0; debug < 2; debug++)
		for (online = 0; online < 2; online++) for (int stealth = 0; stealth < 2; stealth++)
		for (int exp = 0; exp < 4; exp++) for (int adjusted = 0; adjusted < 2; adjusted++) {
			reset_rules(1); mission.Body.UnitTypeID = 1 + stealth; rules[0].defender_match = 1 + stealth;
			is->current_config.debug_air_combat_counters = debug; popup_calls = 0;
			rules[0].self_atk_pct = adjusted ? 25 : 100;
			rules[0].only_in_city = true; rules[0].terrain_mask = 1U << SQ_Hills; target_city = 1;
			interceptor.Body.Combat_Experience = mission.Body.Combat_Experience = exp;
			interceptor.Body.Damage = mission.Body.Damage = 0;
			mode = 3; visible = 1; rng_calls = destroy_calls = message_calls = animation_calls = 0;
			combat();
			assert(rng_calls == 2 + exp && base_calls == rng_calls);
			assert((adjusted ? interceptor.Body.Damage : mission.Body.Damage) == 2 + exp);
			assert((adjusted ? mission.Body.Damage : interceptor.Body.Damage) == 0);
			assert(destroy_calls == !online && message_calls == online && animation_calls == !online);
			assert(popup_calls == (debug && !online)); /* Once per battle, not per round. */
		}
		/* Native zero-strength and network-authority early exits. */
		reset_rules(1); mode = 3; early_strengths = 1; rng_calls = 0; combat();
		assert(!base_calls && !rng_calls); early_strengths = 0;
		reset_rules(1); mode = 3; online = 1; player_civ = 2;
		interceptor.Body.Damage = mission.Body.Damage = 0;
		message_calls = destroy_calls = rng_calls = 0; combat();
		assert(!message_calls && !destroy_calls && rng_calls == base_calls); player_civ = 1;
		VirtualFree(code, 0, MEM_RELEASE);
		printf("PASS: %s native loop, offline/online, normal/stealth, experience, outcomes, one RNG per round, early returns\n", fx->name);
	}
}

int main(void) {
	bic.UnitTypes = types; bic.UnitTypeCount = ARRAY_LEN(types);
	strcpy(types[0].Name, "Interceptor"); strcpy(types[1].Name, "Bomber");
	strcpy(types[2].Name, "Stealth Bomber"); strcpy(types[3].Name, "Unrelated"); strcpy(types[4].Name, "Bomber");
	for (int i = 0; i < ARRAY_LEN(types); i++) types[i].Unit_Class = UTC_Air;
	types[2].UnitAbilities = 1U << UTA_Stealth;
	interceptor.Body.ID = 1; mission.Body.ID = 2;
	interceptor.Body.UnitTypeID = 0; mission.Body.UnitTypeID = nested_mission.Body.UnitTypeID = 1;
	interceptor.Body.CivID = 1; mission.Body.CivID = 2;
	interceptor.Body.X = 10; interceptor.Body.Y = 12; mission.Body.X = 20; mission.Body.Y = 22;
	tile_vtable.m50_Get_Square_BaseType = get_terrain;
	tile_vtable.m18_Check_Mines = has_mine; tile_vtable.m17_Check_Irrigation = has_irrigation;
	target.vtable = base_tile.vtable = &tile_vtable;
	is->counter_air_combat_ctx = (struct counter_air_combat_context){&other_fighter, &mission, &interceptor, 3, 4};
	is->counter_defender_selection_ctx = (struct counter_defender_selection_context){true, &mission, 5, 6};
	is->counter_army_attacker_selection_ctx = (struct counter_army_attacker_selection_context){true, &mission, &interceptor, &base_tile};
	rule_tests(); debug_popup_tests(); native_tests(); free_counter_rule_match_cache();
	return 0;
}
