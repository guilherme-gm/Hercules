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

// we may not use all functions in every test, and that's fine
#pragma GCC diagnostic ignored "-Wunused-function"

// ====================== Socket mocking =====================
int fd_counter = 100;
int (*og_socket_wfifoset)(int fd, size_t len, bool validate);

uint8 dummy_buff[1000] = { 0 };

int null_fn(int fd)
{
	return 0;
}

static int fake_wfifoset(int fd, size_t len, bool validate)
{
	return 0;
}

void use_fake_sockets(void)
{
	sockt->wfifoset = fake_wfifoset;
	memset(&dummy_buff, 0, sizeof(dummy_buff));
}

// ====================== Random mocking =====================
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

// =================== Unit Mocking ====================
void set_pos(struct block_list *bl, int x, int y)
{
	nullpo_retv(bl);

	bl->x = x;
	bl->y = y;
}

// =================== Monster Mocking =================
struct mob_data *make_dummy_mob(void)
{
	struct spawn_data data;
	memset(&data, 0, sizeof(struct spawn_data));

	struct mob_data *md = mob->spawn_dataset(&data, 0);

	struct status_data *std = status->get_status_data(&md->bl);
	std->max_hp = 100;
	std->hp = 100;

	return md;
}

void clear_mob(struct mob_data *md)
{
	unit->free(&md->bl, CLR_DEAD);
}

// =================== Player Mocking ==================
struct pc_skill {
	int skill_id;
	int skill_lv;
};

struct map_session_data *make_dummy_pc(void)
{
	struct map_session_data *sd = pc->get_dummy_sd();
	sd->fd = fd_counter++;
	sockt->create_session(sd->fd, null_fn, null_fn, null_fn, null_fn, null_fn);
	sockt->session[sd->fd]->client_addr = 0;
	sockt->session[sd->fd]->flag.validate = sockt->validate;
	sockt->session[sd->fd]->func_client_connected(sd->fd);
	pc->setnewpc(sd, 100, 100, 0, 0, SEX_MALE, sd->fd);

	sd->parse_cmd_func = clif->parse_cmd;

	struct status_data *std = status->get_status_data(&sd->bl);
	std->max_hp = 100;
	std->hp = 100;

	return sd;
}

void force_pc_addskill(struct map_session_data *sd, struct pc_skill *skill_)
{
	nullpo_retv(sd);

	sd->status.skill[skill->get_index(skill_->skill_id)].id = skill_->skill_id;
	sd->status.skill[skill->get_index(skill_->skill_id)].lv = skill_->skill_lv;
}

void clear_pc(struct map_session_data *sd)
{
	sockt->delete_session(sd->fd);
	aFree(sd);
}


// ====================== SC mocking ==================
int (*og_sc_start) (struct block_list *src, struct block_list *bl, enum sc_type type, int rate, int val1, int val2, int val3, int val4, int tick, int flag, int skill_id);

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

static void use_fake_sc_start(void)
{
	status->change_start = fake_sc_start;
	memset(&fake_sc_start_val, 0, sizeof(fake_sc_start_val));
}

// =======================================================

void init_mocker(void)
{
	og_sc_start = status->change_start;
	og_random = rnd->random;
	og_socket_wfifoset = sockt->wfifoset;
}

void reset_mocks(void)
{
	rnd->random = og_random;
	status->change_start = og_sc_start;
	sockt->wfifoset = og_socket_wfifoset;
}
