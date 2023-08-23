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

#include "map/battle.h"
#include "map/map.h"
#include "map/mob.h"
#include "map/skill.h"
#include "map/status.h"
#include "map/pc.h"

#include "plugins/tests/test_utils.h"

#include <stdlib.h>

#include "common/HPMDataCheck.h"

HPExport struct hplugin_info pinfo = {
	"test_autospell",    // Plugin name
	SERVER_TYPE_MAP,     // Which server types this plugin works with?
	"0.1",               // Plugin version
	HPM_VERSION,         // HPM Version (don't change, macro is automatically updated)
};

#include "plugins/tests/mock_utils.inc.c"

void run_mob_autospell(int level)
{
	use_fake_sc_start();
	
	struct mob_data *md = make_dummy_mob();
	skill->autospell_select_spell(&md->bl, level);

	clear_mob(md);
	reset_mocks();
}

struct map_session_data *run_pc_autospell(int level, struct pc_skill *skills, int skills_len, bool keep_sd)
{
	use_fake_sc_start();
	use_fake_sockets();

	struct map_session_data *sd = make_dummy_pc();
	for (int i = 0; i < skills_len; i++)
		force_pc_addskill(sd, (skills + i));

	skill->autospell_select_spell(&sd->bl, level);

	struct PACKET_ZC_AUTOSPELLLIST *p = WFIFOP(sd->fd, 0);
	memcpy(dummy_buff, p, p->packetLength);

	sockt->close(100);

	reset_mocks();

	if (keep_sd)
		return sd;

	aFree(sd);
	return NULL;
}

static bool skills_match(struct PACKET_ZC_AUTOSPELLLIST *p, int *expected_skills, int expected_len)
{
	bool passed = true;
	int packet_len = (p->packetLength - sizeof(struct PACKET_ZC_AUTOSPELLLIST)) / 4;
	
	int comparable_len = packet_len;
	if (comparable_len > expected_len) {
		ShowError("!!!! We are showing more skills than we should !!!!\n");
		passed = false;
		comparable_len = expected_len;
	}

	for (int i = 0; i < comparable_len; i++) {
		expect("it should show %s (idx: %d)", p->skills[i], expected_skills[i], skill->get_name(expected_skills[i]), i);
	}

	if (expected_len > 0) {
		for (int i = comparable_len; i < expected_len; i++) {
			expect("it should show %s (idx: %d)", 0, expected_skills[i], skill->get_name(expected_skills[i]), i);
		}
	}

	return passed;
}


#ifndef RENEWAL

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

#else

static bool test_autospell_cast_mob(void)
{
	bool passed = true;

	struct {
		int autospell_level;
		int skill;
		int max_lv;
		int lock_rand;
	} cases[] = {
		{ 1, MG_COLDBOLT, 1, 0 },
		{ 1, MG_FIREBOLT, 1, 1 },
		{ 1, MG_LIGHTNINGBOLT, 1, 2 },
		{ 1, MG_COLDBOLT, 1, 3 },

		{ 2, MG_COLDBOLT, 1, 0 },
		{ 2, MG_FIREBOLT, 1, 1 },
		{ 2, MG_LIGHTNINGBOLT, 1, 2 },
		{ 2, MG_COLDBOLT, 1, 3 },
		
		{ 3, MG_COLDBOLT, 1, 0 },
		{ 3, MG_FIREBOLT, 1, 1 },
		{ 3, MG_LIGHTNINGBOLT, 1, 2 },
		{ 3, MG_COLDBOLT, 1, 3 },

		{ 4, MG_SOULSTRIKE, 2, 0 },
		{ 4, MG_FIREBALL, 2, 1 },
		{ 4, MG_SOULSTRIKE, 2, 2 },

		{ 5, MG_SOULSTRIKE, 2, 0 },
		{ 5, MG_FIREBALL, 2, 1 },
		{ 5, MG_SOULSTRIKE, 2, 2 },
		
		{ 6, MG_SOULSTRIKE, 3, 0 },
		{ 6, MG_FIREBALL, 3, 1 },
		{ 6, MG_SOULSTRIKE, 3, 2 },

		{ 7, WZ_EARTHSPIKE, 3, 0 },
		{ 7, MG_FROSTDIVER, 3, 1 },
		{ 7, WZ_EARTHSPIKE, 3, 2 },
		
		{ 8, WZ_EARTHSPIKE, 4, 0 },
		{ 8, MG_FROSTDIVER, 4, 1 },
		{ 8, WZ_EARTHSPIKE, 4, 2 },

		{ 9, WZ_EARTHSPIKE, 4, 0 },
		{ 9, MG_FROSTDIVER, 4, 1 },
		{ 9, WZ_EARTHSPIKE, 4, 2 },
		
		{ 10, MG_THUNDERSTORM, 5, 0 },
		{ 10, WZ_HEAVENDRIVE, 5, 1 },
		{ 10, MG_THUNDERSTORM, 5, 2 },
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

#endif

#ifndef RENEWAL

static bool test_autospell_cast_pc(void)
{
	bool passed = true;

	struct PACKET_ZC_AUTOSPELLLIST *p;

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

#else

static bool test_autospell_cast_pc(void)
{
	bool passed = true;

	struct PACKET_ZC_AUTOSPELLLIST *p;

	struct pc_skill all_skills[] = {
		{ MG_FIREBOLT, 3 },
		{ MG_COLDBOLT, 3 },
		{ MG_LIGHTNINGBOLT, 3 },
		{ MG_SOULSTRIKE, 3 },
		{ MG_FIREBALL, 3 },
		{ WZ_EARTHSPIKE, 3 },
		{ MG_FROSTDIVER, 3 },
		{ MG_THUNDERSTORM, 3 },
		{ WZ_HEAVENDRIVE, 3 },
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
		context("Player uses AutoSpell Lv10 but only knows MG_FIREBOLT");
		
		struct pc_skill skills[] = { { MG_FIREBOLT, 5 } };
		int expected_skills[] = { MG_FIREBOLT };
		struct map_session_data *sd = run_pc_autospell(10, skills, ARRAYLENGTH(skills), true);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		
		expect("it should send the packet.", p->packetType, HEADER_ZC_AUTOSPELLLIST);
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));

		expect("it should set player workinprogress to 3", sd->state.workinprogress, 3);
		expect("it should set player menuskill ID to AutoSpell", sd->menuskill_id, SA_AUTOSPELL);
		expect("it should set player menuskill VAL to AutoSpell level", sd->menuskill_val, 10);

		aFree(sd);
	}

	for (int i = 1; i <= 3; i++)
	{
		context("Player knows all skills and uses AutoSpell Lv%d", i);
		int expected_skills[] = { MG_COLDBOLT, MG_FIREBOLT, MG_LIGHTNINGBOLT };
		run_pc_autospell(1, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}

	for (int i = 4; i <= 6; i++)
	{
		context("Player knows all skills and uses AutoSpell Lv%d", i);
		int expected_skills[] = { MG_COLDBOLT, MG_FIREBOLT, MG_LIGHTNINGBOLT, MG_SOULSTRIKE, MG_FIREBALL };
		run_pc_autospell(i, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}

	for (int i = 7; i <= 9; i++)
	{
		context("Player knows all skills and uses AutoSpell Lv%d", i);
		int expected_skills[] = { MG_COLDBOLT, MG_FIREBOLT, MG_LIGHTNINGBOLT, MG_SOULSTRIKE, MG_FIREBALL, WZ_EARTHSPIKE, MG_FROSTDIVER };
		run_pc_autospell(i, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}

	{
		context("Player knows all skills and uses AutoSpell Lv10");
		int expected_skills[] = { MG_COLDBOLT, MG_FIREBOLT, MG_LIGHTNINGBOLT, MG_SOULSTRIKE, MG_FIREBALL, WZ_EARTHSPIKE, MG_FROSTDIVER, MG_THUNDERSTORM, WZ_HEAVENDRIVE };
		run_pc_autospell(10, all_skills, ARRAYLENGTH(all_skills), false);
		p = (struct PACKET_ZC_AUTOSPELLLIST *) dummy_buff;
		skills_match(p, expected_skills, ARRAYLENGTH(expected_skills));
	}
	return passed;
}

#endif

void server_online(void)
{
	ShowMessage("===============================================================================\n");
	ShowStatus("Starting tests.\n");

	init_mocker();

	TEST("when monster casts AutoSpell", test_autospell_cast_mob);
	TEST("when player casts AutoSpell", test_autospell_cast_pc);

	reset_mocks();

	map->do_shutdown();
}

void plugin_final(void) {
	ShowMessage("===============================================================================\n");
	ShowStatus("All tests passed.\n");
}
