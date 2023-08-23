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

static bool test_bladestop_player_v_player_check(void)
{
	bool passed = true;

	{
		context("A player attacks another player who is not in Blade Stop Wait mode");
		
		struct map_session_data *src = make_dummy_pc();
		struct map_session_data *tgt = make_dummy_pc();

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 152, 150);

		expect("it should not cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), false);

		clear_pc(src);
		clear_pc(tgt);
	}

	{
		context("A player attacks another player who IS in Blade Stop Wait mode from 1 cell distance");

		struct map_session_data *src = make_dummy_pc();
		struct map_session_data *tgt = make_dummy_pc();

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 151, 150);

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		expect("it should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), true);

		clear_pc(src);
		clear_pc(tgt);
	}

	{
		context("A player attacks another player who IS in Blade Stop Wait mode from 2 cell distance");

		struct map_session_data *src = make_dummy_pc();
		struct map_session_data *tgt = make_dummy_pc();

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 152, 150);

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		expect("it should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), true);

		clear_pc(src);
		clear_pc(tgt);
	}

	{ // FIXME: Is that right?
		context("A player attacks another player who IS in Blade Stop Wait mode from 3 cell distance");

		struct map_session_data *src = make_dummy_pc();
		struct map_session_data *tgt = make_dummy_pc();

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 153, 150);

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		expect("it should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), true);

		clear_pc(src);
		clear_pc(tgt);
	}

	return passed;
}

static bool test_bladestop_monster_v_player_check(void)
{
	bool passed = true;

	{
		context("A monster attacks a player who is not in Blade Stop Wait mode");
		
		struct mob_data *src = make_dummy_mob();
		struct map_session_data *tgt = make_dummy_pc();

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 151, 150);

		expect("it should not cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), false);

		clear_mob(src);
		clear_pc(tgt);
	}

	{
		context("A monster attacks a player who IS in Blade Stop Wait mode from 1 cell distance");
		
		struct mob_data *src = make_dummy_mob();
		struct map_session_data *tgt = make_dummy_pc();

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 151, 150);

		expect("it should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), true);

		clear_mob(src);
		clear_pc(tgt);
	}

#ifndef RENEWAL
	{
		context("A BOSS monster attacks a player who IS in Blade Stop Wait mode from 1 cell distance");
		
		struct mob_data *src = make_dummy_mob();
		struct map_session_data *tgt = make_dummy_pc();

		status->get_status_data(&src->bl)->mode |= MD_BOSS;

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 151, 150);

		expect("it should NOT cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), false);

		clear_mob(src);
		clear_pc(tgt);
	}
#else
	{
		context("A BOSS monster attacks a player who IS in Blade Stop Wait mode from 1 cell distance");
		
		struct mob_data *src = make_dummy_mob();
		struct map_session_data *tgt = make_dummy_pc();

		status->get_status_data(&src->bl)->mode |= MD_BOSS;

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 151, 150);

		expect("it should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), true);

		clear_mob(src);
		clear_pc(tgt);
	}
#endif

	{
		context("A monster attacks a player who IS in Blade Stop Wait mode from 2 cell distance and without weapons");
		
		struct mob_data *src = make_dummy_mob();
		struct map_session_data *tgt = make_dummy_pc();

		tgt->weapontype = W_FIST;

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 152, 150);

		expect("it should NOT cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), false);

		clear_mob(src);
		clear_pc(tgt);
	}

	{
		context("A monster attacks a player who IS in Blade Stop Wait mode from 2 cell distance and using a Knuckle");
		
		struct mob_data *src = make_dummy_mob();
		struct map_session_data *tgt = make_dummy_pc();

		tgt->weapontype = W_KNUCKLE;

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 152, 150);

		expect("it should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), true);

		clear_mob(src);
		clear_pc(tgt);
	}

	{
		context("A monster attacks a player who IS in Blade Stop Wait mode from 3 cell distance");
		
		struct mob_data *src = make_dummy_mob();
		struct map_session_data *tgt = make_dummy_pc();

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 153, 150);

		expect("it NOT should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), false);

		clear_mob(src);
		clear_pc(tgt);
	}

	return passed;
}

static bool test_bladestop_player_v_monster_check(void)
{
	bool passed = true;

	{
		context("A player attacks a monster who is not in Blade Stop Wait mode");
		
		struct map_session_data *src = make_dummy_pc();
		struct mob_data *tgt = make_dummy_mob();

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 151, 150);

		expect("it should not cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), false);

		clear_pc(src);
		clear_mob(tgt);
	}

	{
		context("A player attacks a monster who IS in Blade Stop Wait mode from 1 cell distance");
		
		struct map_session_data *src = make_dummy_pc();
		struct mob_data *tgt = make_dummy_mob();

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 151, 150);

		expect("it should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), true);

		clear_pc(src);
		clear_mob(tgt);
	}

	{
		context("A player attacks a monster who IS in Blade Stop Wait mode from 10 cell distance");
		
		struct map_session_data *src = make_dummy_pc();
		struct mob_data *tgt = make_dummy_mob();

		sc_start(&tgt->bl, &tgt->bl, SC_BLADESTOP_WAIT, 100, 5, 10000, MO_BLADESTOP);

		set_pos(&src->bl, 150, 150);
		set_pos(&tgt->bl, 160, 150);

		expect("it should cause blade stop effect",
			battle->should_bladestop_attacker(&src->bl, &tgt->bl), true);

		clear_pc(src);
		clear_mob(tgt);
	}

	return passed;
}

void server_online(void)
{
	ShowMessage("===============================================================================\n");
	ShowStatus("Starting tests.\n");

	init_mocker();

	TEST("BladeStop : Player vs Player", test_bladestop_player_v_player_check);
	TEST("BladeStop : Monster vs Player", test_bladestop_monster_v_player_check);
	TEST("BladeStop : Player vs Monster", test_bladestop_player_v_monster_check);

	reset_mocks();

	map->do_shutdown();
}

void plugin_final(void) {
	ShowMessage("===============================================================================\n");
	ShowStatus("All tests passed.\n");
}
