/**
 * This file is part of Hercules.
 * http://herc.ws - http://github.com/HerculesWS/Hercules
 *
 * Copyright (C) 2015-2023 Hercules Dev Team
 * Copyright (C) 2015 Haru <haru@dotalux.com>
 *
 * Hercules is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/// Base author: Haru <haru@dotalux.com>

#include "common/hercules.h"

#include "common/random.h"
#include "common/cbasetypes.h"
#include "common/conf.h"
#include "common/core.h"
#include "common/memmgr.h"
#include "common/showmsg.h"
#include "common/strlib.h"
#include "common/socket.h"
#include "common/nullpo.h"

#include "map/map.h"
#include "map/mob.h"
#include "map/skill.h"
#include "map/status.h"
#include "map/pc.h"

#include <stdlib.h>

#include "common/HPMDataCheck.h"

HPExport struct hplugin_info pinfo = {
	"test_autospell",    // Plugin name
	SERVER_TYPE_MAP,     // Which server types this plugin works with?
	"0.1",               // Plugin version
	HPM_VERSION,         // HPM Version (don't change, macro is automatically updated)
};

#define TEST(name, function, ...) do { \
	ShowMessage("-------------------------------------------------------------------------------\n"); \
	ShowNotice("Testing %s...\n", (name)); \
	if (!(function)(##__VA_ARGS__)) { \
		ShowError("Failed.\n"); \
	} else { \
		ShowStatus("Passed.\n"); \
	} \
	ShowMessage("-------------------------------------------------------------------------------\n"); \
} while (false)

#define context(message, ...) do { \
	ShowNotice("\n"); \
	ShowNotice("> " message "\n", ##__VA_ARGS__); \
} while (false)

#define expect(message, actual, expected, ...) do { \
	ShowNotice("\t" message "... ", ##__VA_ARGS__); \
	if (actual != expected) { \
		passed = false; \
		ShowMessage("" CL_RED "Failed" CL_RESET "\n"); \
		ShowNotice("\t\t(Expected: " CL_GREEN " %d " CL_RESET ", Received: " CL_RED " %d " CL_RESET ")\n", expected, actual); \
	} else { \
		ShowMessage("" CL_GREEN "Passed" CL_RESET "\n"); \
	} \
} while (false)

struct {
	struct block_list *src;
	struct block_list *bl;
	enum sc_type type;
	int rate;
	int val1;
	int val2;
	int val3;
	int val4;
	int tick;
	int flag;
	int skill_id;
} fake_sc_start_val;

static int fake_sc_start(struct block_list *src, struct block_list *bl, enum sc_type type, int rate, int val1, int val2, int val3, int val4, int tick, int flag, int skill_id)
{
	fake_sc_start_val.src = src;
	fake_sc_start_val.bl = bl;
	fake_sc_start_val.type = type;
	fake_sc_start_val.rate = rate;
	fake_sc_start_val.val1 = val1;
	fake_sc_start_val.val2 = val2;
	fake_sc_start_val.val3 = val3;
	fake_sc_start_val.val4 = val4;
	fake_sc_start_val.tick = tick;
	fake_sc_start_val.flag = flag;
	fake_sc_start_val.skill_id = skill_id;

	return 0;
}

uint8 dummy_buff[1000] = { 0 };

void run_mob_autospell(int level)
{
	struct spawn_data data;

	status->change_start = fake_sc_start;

	memset(&data, 0, sizeof(struct spawn_data));

	struct mob_data *md = mob->spawn_dataset(&data, 0);

	skill->autospell_select_spell(&md->bl, level);

	unit->free(&md->bl, CLR_DEAD);
}

int null_fn(int fd)
{
	return 0;
}

struct map_session_data *make_dummy_pc(void)
{
	struct map_session_data *sd = pc->get_dummy_sd();
	sd->fd = 100;
	sockt->create_session(sd->fd, null_fn, null_fn, null_fn, null_fn, null_fn);
	sockt->session[sd->fd]->client_addr = 0;
	sockt->session[sd->fd]->flag.validate = sockt->validate;
	sockt->session[sd->fd]->func_client_connected(sd->fd);
	pc->setnewpc(sd, 100, 100, 0, 0, SEX_MALE, sd->fd);

	sd->parse_cmd_func = clif->parse_cmd;

	return sd;
}

struct pc_skill {
	int skill_id;
	int skill_lv;
};


void force_pc_addskill(struct map_session_data *sd, struct pc_skill *skill_)
{
	nullpo_retv(sd);

	sd->status.skill[skill->get_index(skill_->skill_id)].id = skill_->skill_id;
	sd->status.skill[skill->get_index(skill_->skill_id)].lv = skill_->skill_lv;
}


struct map_session_data *run_pc_autospell(int level, struct pc_skill *skills, int skills_len, bool keep_sd)
{
	struct map_session_data *sd = make_dummy_pc();
	for (int i = 0; i < skills_len; i++)
		force_pc_addskill(sd, (skills + i));

	skill->autospell_select_spell(&sd->bl, level);

	struct PACKET_ZC_AUTOSPELLLIST *p = WFIFOP(sd->fd, 0);
	memcpy(dummy_buff, p, p->packetLength);

	sockt->close(100);
	if (keep_sd)
		return sd;
	

	aFree(sd);
	return NULL;
}

int32 (*og_random) (void);

int32 fake_random_val = 0;

int32 fake_random(void)
{
	return fake_random_val;
}

void use_fake_random(int val)
{
	rnd->random = fake_random;
	fake_random_val = val;
}

void reset_mocks(void)
{
	rnd->random = og_random;
	fake_random_val = 0;
	memset(&fake_sc_start_val, 0, sizeof(fake_sc_start_val));
	memset(&dummy_buff, 0, sizeof(dummy_buff));
}

static int wfifoset(int fd, size_t len, bool validate)
{
	return 0;
}

static bool test_autospell_cast_mob(void)
{
	bool passed = true;

	struct {
		int autospell_level;
		int skill;
		int max_lv;
		int lock_rand;
	} cases[] = {
		{ 1, MG_NAPALMBEAT, 3, -1 },
		
		{ 2, MG_COLDBOLT, 1, 0 },
		{ 2, MG_FIREBOLT, 1, 1 },
		{ 2, MG_LIGHTNINGBOLT, 1, 2 },
		{ 2, MG_COLDBOLT, 1, 3 },

		{ 3, MG_COLDBOLT, 2, 0 },
		{ 3, MG_FIREBOLT, 2, 1 },
		{ 3, MG_LIGHTNINGBOLT, 2, 2 },
		{ 3, MG_COLDBOLT, 2, 3 },

		{ 4, MG_COLDBOLT, 3, 0 },
		{ 4, MG_FIREBOLT, 3, 1 },
		{ 4, MG_LIGHTNINGBOLT, 3, 2 },
		{ 4, MG_COLDBOLT, 3, 3 },

		{ 5, MG_SOULSTRIKE, 1, -1 },
		{ 6, MG_SOULSTRIKE, 2, -1 },
		{ 7, MG_SOULSTRIKE, 3, -1 },
		{ 8, MG_FIREBALL, 1, -1 },
		{ 9, MG_FIREBALL, 2, -1 },
		{ 10, MG_FROSTDIVER, 1, -1 },
	};

	 for (int i = 0; i < ARRAYLENGTH(cases); ++i) {
		if (cases[i].lock_rand >= 0) {
			use_fake_random(cases[i].lock_rand);
			context("Using Level %d AutoSpell. (Random locked at %d)", cases[i].autospell_level, cases[i].lock_rand);
		} else {
			context("Using Level %d AutoSpell.", cases[i].autospell_level);
		}
	
		run_mob_autospell(cases[i].autospell_level);
		
		expect("it should set SC_AUTOSPELL", fake_sc_start_val.type, SC_AUTOSPELL);
		expect("it should set %s in AutoSpell", fake_sc_start_val.val2, cases[i].skill, skill->get_name(cases[i].skill));
		expect("it should set max level to %d", fake_sc_start_val.val3, cases[i].max_lv, cases[i].max_lv);

		reset_mocks();
	}

	return passed;
}

static bool skills_match(struct PACKET_ZC_AUTOSPELLLIST *p, int *expected_skills, int expected_len)
{
	bool passed = true;
	int len = (p->packetLength - sizeof(struct PACKET_ZC_AUTOSPELLLIST)) / 4;
	for (int i = 0; i < len; i++) {
		expect("it should show %s (idx: %d)", p->skills[i], expected_skills[i], skill->get_name(expected_skills[i]), i);
	}

	if (expected_len > 0) {
		for (int i = len; i < expected_len; i++) {
			expect("it should show %s (idx: %d)", 0, expected_skills[i], skill->get_name(expected_skills[i]), i);
		}
	}

	return passed;
}

static bool test_autospell_cast_pc(void)
{
	bool passed = true;

	struct PACKET_ZC_AUTOSPELLLIST *p;

	sockt->wfifoset = wfifoset;

	struct pc_skill all_skills[] = {
		{ MG_NAPALMBEAT, 3 },
		{ MG_FIREBOLT, 3 },
		{ MG_COLDBOLT, 3 },
		{ MG_LIGHTNINGBOLT, 3 },
		{ MG_SOULSTRIKE, 3 },
		{ MG_FIREBALL, 3 },
		{ MG_FROSTDIVER, 3 },
	};

	{
		context("Player uses AutoSpell Lv10 but don't know other skills");
		
		struct pc_skill skills[] = { { SA_AUTOSPELL, 10 } };
		int expected_skills[] = {};
		struct map_session_data *sd = run_pc_autospell(10, skills, ARRAYLENGTH(skills), true);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;

		expect("it should send the packet.", p->packetType, HEADER_ZC_AUTOSPELLLIST);
		expect("it should show an empty list", skills_match(p, expected_skills, ARRAYLENGTH(expected_skills)), true);

		expect("it should set player workinprogress to 3", sd->state.workinprogress, 3);
		expect("it should set player menuskill ID to AutoSpell", sd->menuskill_id, SA_AUTOSPELL);
		expect("it should set player menuskill VAL to AutoSpell level", sd->menuskill_val, 10);

		aFree(sd);
	}

	{
		context("Player uses AutoSpell Lv10 but only knows MG_NAPALMBEAT");
		
		struct pc_skill skills[] = { { MG_NAPALMBEAT, 5 } };
		int expected_skills[] = { MG_NAPALMBEAT };
		struct map_session_data *sd = run_pc_autospell(10, skills, ARRAYLENGTH(skills), true);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		
		expect("it should send the packet.", p->packetType, HEADER_ZC_AUTOSPELLLIST);
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));

		expect("it should set player workinprogress to 3", sd->state.workinprogress, 3);
		expect("it should set player menuskill ID to AutoSpell", sd->menuskill_id, SA_AUTOSPELL);
		expect("it should set player menuskill VAL to AutoSpell level", sd->menuskill_val, 10);

		aFree(sd);
	}

	{
		context("Player knows all skills and uses AutoSpell Lv1");
		int expected_skills[] = { MG_NAPALMBEAT };
		run_pc_autospell(1, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}

	for (int i = 2; i <= 4; i++)
	{
		context("Player knows all skills and uses AutoSpell Lv%d", i);
		int expected_skills[] = { MG_NAPALMBEAT, MG_COLDBOLT, MG_FIREBOLT, MG_LIGHTNINGBOLT };
		run_pc_autospell(i, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}

	for (int i = 5; i <= 7; i++)
	{
		context("Player knows all skills and uses AutoSpell Lv%d", i);
		int expected_skills[] = { MG_NAPALMBEAT, MG_COLDBOLT, MG_FIREBOLT, MG_LIGHTNINGBOLT, MG_SOULSTRIKE };
		run_pc_autospell(i, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}

	for (int i = 8; i <= 9; i++)
	{
		context("Player knows all skills and uses AutoSpell Lv%d", i);
		int expected_skills[] = { MG_NAPALMBEAT, MG_COLDBOLT, MG_FIREBOLT, MG_LIGHTNINGBOLT, MG_SOULSTRIKE, MG_FIREBALL };
		run_pc_autospell(i, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}

	{
		context("Player knows all skills and uses AutoSpell Lv10");
		int expected_skills[] = { MG_NAPALMBEAT, MG_COLDBOLT, MG_FIREBOLT, MG_LIGHTNINGBOLT, MG_SOULSTRIKE, MG_FIREBALL, MG_FROSTDIVER };
		run_pc_autospell(10, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}

	return passed;
}

void server_online(void)
{
	ShowMessage("===============================================================================\n");
	ShowStatus("Starting tests.\n");

	og_random = rnd->random;

	TEST("when monster casts AutoSpell", test_autospell_cast_mob);
	TEST("when player casts AutoSpell", test_autospell_cast_pc);

	map->do_shutdown();
}

void plugin_final(void) {
	ShowMessage("===============================================================================\n");
	ShowStatus("All tests passed.\n");
}
