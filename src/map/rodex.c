/**
 * This file is part of Hercules.
 * http://herc.ws - http://github.com/HerculesWS/Hercules
 *
 * Copyright (C) 2017-2023 Hercules Dev Team
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
#define HERCULES_CORE

#include "rodex.h"

#include "map/battle.h"
#include "map/date.h"
#include "map/intif.h"
#include "map/itemdb.h"
#include "map/pc.h"

#include "common/nullpo.h"
#include "common/sql.h"
#include "common/memmgr.h"
#include "common/showmsg.h"


// NOTE : These values are hardcoded into the client
// Cost of each Attached Item
#define ATTACHITEM_COST 2500
// Percent of Attached Zeny that will be paid as Tax
#define ATTACHZENY_TAX 2
// Maximun number of messages that can be sent in one day
#define DAILY_MAX_MAILS 100

static struct rodex_interface rodex_s;
struct rodex_interface *rodex;

/// Checks if RoDEX System is enabled in the server
/// Returns true if it's enabled, false otherwise
static bool rodex_isenabled(void)
{
	if (battle_config.feature_rodex == 1)
		return true;

	return false;
}

/**
 * Initializes a rodex message structure with default settings and sender information.
 *
 * @param msg message container
 * @param sender_id character id authoring the message (use RODEX_NPC_SENDER for NPC-generated messages)
 * @param sender_name display name of message sender/author
 */
static void rodex_mail_init(struct rodex_message *msg, int sender_id, const char *sender_name)
{
	nullpo_retv(msg);
	nullpo_retv(sender_name);

	memset(msg, 0, sizeof(*msg));
	msg->send_date = (int) time(NULL);
	msg->expire_date = (int) time(NULL) + RODEX_EXPIRE;

	msg->type = MAIL_TYPE_TEXT;

	msg->sender_id = sender_id;
	safestrncpy(msg->sender_name, sender_name, sizeof(msg->sender_name));

	if (sender_id == RODEX_NPC_SENDER)
		msg->type |= MAIL_TYPE_NPC;
}

/**
 * Attempts to add an item to a RoDEX message
 *
 * @remark
 *    This function does not perform player-specific checks (like trade check, inventory, etc)
 *
 * @param msg message container
 * @param inventory_idx inventory index (if coming from a player, -1 if not from a player)
 * @param it item to add
 * @return true if it was added, false otherwise
 */
static enum rodex_add_item rodex_mail_try_add_item(struct rodex_message *msg, int inventory_idx, struct item *it)
{
	nullpo_retr(RODEX_ADD_ITEM_FATAL_ERROR, msg);
	nullpo_retr(RODEX_ADD_ITEM_FATAL_ERROR, it);
	Assert_retr(RODEX_ADD_ITEM_FATAL_ERROR, it->amount > 0 && it->amount <= MAX_AMOUNT);

	struct item_data *itd = itemdb->search(it->nameid);

	int weight_gain = it->amount * itd->weight;

	if (msg->weight + weight_gain > RODEX_WEIGHT_LIMIT)
		return RODEX_ADD_ITEM_WEIGHT_ERROR; // @TODO: Or RODEX_ADD_ITEM_FATAL_ERROR ?

	// Check for existing stacks to add to them
	if (itemdb->isstackable2(itd) == 1) {
		int item_idx;
		ARR_FIND(0, msg->items_count, item_idx, (
			msg->items[item_idx].idx == inventory_idx
			&& msg->items[item_idx].item.nameid == it->nameid
			&& msg->items[item_idx].item.unique_id == it->unique_id
		));

		if (item_idx < msg->items_count) {
			if (msg->items[item_idx].item.amount + it->amount > MAX_AMOUNT)
				return RODEX_ADD_ITEM_FATAL_ERROR;

			msg->items[item_idx].item.amount += it->amount;
			msg->weight += weight_gain;
			return RODEX_ADD_ITEM_SUCCESS;
		}
	}

	if (msg->items_count == RODEX_MAX_ITEM)
		return RODEX_ADD_ITEM_NO_SPACE;

	msg->items[msg->items_count].item = *it;
	msg->items[msg->items_count].idx = inventory_idx;
	msg->items_count++;

	msg->weight += weight_gain;
	msg->type |= MAIL_TYPE_ITEM;

	return RODEX_ADD_ITEM_SUCCESS;
}

static bool rodex_mail_try_add_zeny(struct rodex_message *msg, int amount)
{
	nullpo_retr(false, msg);

	// check for overflow only?
	if (msg->items_count == RODEX_MAX_ITEM || amount == 0)
		return false;

	if (amount < 0 && msg->zeny < (-1 * amount)) {
		ShowWarning("%s: Trying to remove more zeny from a message than it had. Zeroing message zeny. (amount: %d / current: %ld)\n", __func__, amount, msg->zeny);
		amount = -1 * msg->zeny;
	}

	msg->zeny += amount;

	if (msg->zeny > 0)
		msg->type |= MAIL_TYPE_ZENY;
	else
		msg->type &= ~MAIL_TYPE_ZENY;

	return true;
}

static void rodex_mail_clear_attachments(struct rodex_message *msg)
{
	nullpo_retv(msg);

	msg->zeny = 0;
	msg->items_count = 0;
	// memset(msg->items, 0, sizeof(msg->items));

	msg->type &= ~(MAIL_TYPE_ITEM | MAIL_TYPE_ZENY);
}

static void rodex_mail_send(struct rodex_message *msg, int receiver_id, bool account_mail)
{
	// clear possibly-garbage item
	if (msg->items_count < RODEX_MAX_ITEM) {
		int empty_spaces = RODEX_MAX_ITEM - msg->items_count;
		memset((msg->items + msg->items_count), 0, sizeof(msg->items[0]) * empty_spaces);
	}

	if (account_mail) {
		msg->opentype = RODEX_OPENTYPE_ACCOUNT;
		msg->receiver_accountid = receiver_id;
	} else {
		msg->opentype = RODEX_OPENTYPE_MAIL;
		msg->receiver_id = receiver_id;
	}
}

/// Checks and refreshes the user daily number of Stamps
/// @param sd : The player who's being checked
static void rodex_refresh_stamps(struct map_session_data *sd)
{
	int today = date_get_date();
	nullpo_retv(sd);

	// Note : Weirdly, iRO starts this with maximum messages of the day and decrements
	//        but our clients starts this at 0 and increments
	if (sd->sc.data[SC_DAILYSENDMAILCNT] == NULL) {
		sc_start2(NULL, &sd->bl, SC_DAILYSENDMAILCNT, 100, today, 0, INFINITE_DURATION, 0);
	} else {
		int sc_date = sd->sc.data[SC_DAILYSENDMAILCNT]->val1;
		if (sc_date != today) {
			sc_start2(NULL, &sd->bl, SC_DAILYSENDMAILCNT, 100, today, 0, INFINITE_DURATION, 0);
		}
	}
}

/// Attaches an item to a message being written
/// @param sd : The player who's writting
/// @param idx : the inventory idx of the item
/// @param amount : Amount of the item to be attached
static void rodex_add_item(struct map_session_data *sd, int16 idx, int16 amount)
{
	nullpo_retv(sd);

	if (idx < 0 || idx >= sd->status.inventorySize || sd->inventory_data[idx] == NULL) {
		clif->rodex_add_item_result(sd, idx, amount, RODEX_ADD_ITEM_FATAL_ERROR);
		return;
	}

	struct item *inv_item = &sd->status.inventory[idx];
	if (amount < 0 || amount > inv_item->amount) {
		clif->rodex_add_item_result(sd, idx, amount, RODEX_ADD_ITEM_FATAL_ERROR);
		return;
	}

	if (!pc_can_give_items(sd) || inv_item->expire_time
		|| !itemdb_canmail(&sd->status.inventory[idx], pc_get_group_level(sd))
		|| (inv_item->bound && !pc_can_give_bound_items(sd))) {
		clif->rodex_add_item_result(sd, idx, amount, RODEX_ADD_ITEM_NOT_TRADEABLE);
		return;
	}

	struct rodex_message *msg = &sd->rodex.tmp;

	int existing_idx;
	ARR_FIND(0, msg->items_count, existing_idx, (msg->items[existing_idx].idx == idx));
	if (existing_idx < msg->items_count && msg->items[existing_idx].item.amount + amount > inv_item->amount) {
		// Trying to add more than they have in inventory
		clif->rodex_add_item_result(sd, idx, amount, RODEX_ADD_ITEM_FATAL_ERROR);
		return;
	}

	// copy item data into a new variable so we can change the amount
	struct item it = *inv_item;
	it.amount = amount;

	enum rodex_add_item result = rodex->mail_try_add_item(&sd->rodex.tmp, idx, &it);
	clif->rodex_add_item_result(sd, idx, amount, result);
}

/// Removes an item attached to a message being writen
/// @param sd : The player who's writting the message
/// @param idx : The index of the item
/// @param amount : How much to remove
static void rodex_remove_item(struct map_session_data *sd, int16 idx, int16 amount)
{
	struct item *it;
	struct item_data *itd;

	nullpo_retv(sd);
	Assert_retv(idx >= 0 && idx < sd->status.inventorySize);

	struct rodex_message *msg = &sd->rodex.tmp;

	int item_idx;
	ARR_FIND(0, msg->items_count, item_idx, (msg->items[item_idx].idx == idx));

	if (item_idx == RODEX_MAX_ITEM) {
		clif->rodex_remove_item_result(sd, idx, -1);
		return;
	}

	it = &msg->items[item_idx].item;

	if (amount <= 0 || amount > it->amount) {
		clif->rodex_remove_item_result(sd, idx, -1);
		return;
	}

	itd = itemdb->search(it->nameid);

	it->amount -= amount;
	msg->weight -= itd->weight * amount;
	if (it->amount == 0) {
		msg->items_count--;
		if (item_idx < msg->items_count) {
			// Item is in the "middle" of the item list, shift items to fill the gap
			memmove(&msg->items[item_idx], &msg->items[item_idx + 1], sizeof(msg->items[0]) * (msg->items_count - item_idx));
		}

		if (msg->items_count == 0)
			msg->type &= ~MAIL_TYPE_ITEM;
	}

	clif->rodex_remove_item_result(sd, idx, amount);
}

/// Request if character with given name exists and returns information about him
/// @param sd : The player who's requesting
/// @param name : The name of the character to check
/// @param base_level : Reference to return the character base level, if he exists
/// @param char_id : Reference to return the character id, if he exists
/// @param class : Reference to return the character class id, if he exists
static void rodex_check_player(struct map_session_data *sd, const char *name, int *base_level, int *char_id, int *class)
{
	intif->rodex_checkname(sd, name);
}

/// Sends a Mail to an character
/// @param sd : The player who's sending
/// @param receiver_name : The name of the character who's receiving the message
/// @param body : Mail message
/// @param title : Mail Title
/// @param zeny : Amount of zeny attached
/// Returns result code:
///     RODEX_SEND_MAIL_SUCCESS = 0,
///         RODEX_SEND_MAIL_FATAL_ERROR = 1,
///         RODEX_SEND_MAIL_COUNT_ERROR = 2,
///         RODEX_SEND_MAIL_ITEM_ERROR = 3,
///         RODEX_SEND_MAIL_RECEIVER_ERROR = 4
static int rodex_send_mail(struct map_session_data *sd, const char *receiver_name, const char *body, const char *title, int64 zeny)
{
	int i;
	int64 total_zeny;

	nullpo_retr(RODEX_SEND_MAIL_FATAL_ERROR, sd);
	nullpo_retr(RODEX_SEND_MAIL_FATAL_ERROR, receiver_name);
	nullpo_retr(RODEX_SEND_MAIL_FATAL_ERROR, body);
	nullpo_retr(RODEX_SEND_MAIL_FATAL_ERROR, title);

	if (!rodex->isenabled() || (sd->npc_id != 0 && sd->state.using_megaphone == 0)) {
		rodex->clean(sd, 1);
		return RODEX_SEND_MAIL_FATAL_ERROR;
	}

	if (map->list[sd->bl.m].flag.nosendmail != 0) {
		rodex->clean(sd, 1);
		return RODEX_SEND_MAIL_FATAL_ERROR;
	}

	if (zeny < 0) {
		rodex->clean(sd, 1);
		return RODEX_SEND_MAIL_FATAL_ERROR;
	}

	total_zeny = zeny + sd->rodex.tmp.items_count * ATTACHITEM_COST + (2 * zeny)/100;

	if (strcmp(receiver_name, sd->rodex.tmp.receiver_name) != 0) {
		rodex->clean(sd, 1);
		return RODEX_SEND_MAIL_RECEIVER_ERROR;
	}

	if (total_zeny > sd->status.zeny || total_zeny < 0) {
		rodex->clean(sd, 1);
		return RODEX_SEND_MAIL_FATAL_ERROR;
	}

	rodex_refresh_stamps(sd);

	if (sd->sc.data[SC_DAILYSENDMAILCNT] != NULL) {
		if (sd->sc.data[SC_DAILYSENDMAILCNT]->val2 >= DAILY_MAX_MAILS) {
			rodex->clean(sd, 1);
			return RODEX_SEND_MAIL_COUNT_ERROR;
		}

		sc_start2(NULL, &sd->bl, SC_DAILYSENDMAILCNT, 100, sd->sc.data[SC_DAILYSENDMAILCNT]->val1, sd->sc.data[SC_DAILYSENDMAILCNT]->val2 + 1, INFINITE_DURATION, 0);
	} else {
		sc_start2(NULL, &sd->bl, SC_DAILYSENDMAILCNT, 100, date_get_date(), 1, INFINITE_DURATION, 0);
	}

	for (i = 0; i < RODEX_MAX_ITEM; i++) {
		int16 idx = sd->rodex.tmp.items[i].idx;
		int j;
		struct item *tmpItem = &sd->rodex.tmp.items[i].item;
		struct item *invItem = &sd->status.inventory[idx];

		if (tmpItem->nameid == 0)
			continue;

		if (tmpItem->nameid != invItem->nameid ||
		    tmpItem->unique_id != invItem->unique_id ||
		    tmpItem->refine != invItem->refine ||
		    tmpItem->attribute != invItem->attribute ||
		    tmpItem->expire_time != invItem->expire_time ||
		    tmpItem->bound != invItem->bound ||
		    tmpItem->amount > invItem->amount ||
		    tmpItem->amount < 1) {
			rodex->clean(sd, 1);
			return RODEX_SEND_MAIL_ITEM_ERROR;
		}
		for (j = 0; j < MAX_SLOTS; j++) {
			if (tmpItem->card[j] != invItem->card[j]) {
				rodex->clean(sd, 1);
				return RODEX_SEND_MAIL_ITEM_ERROR;
			}
		}
		for (j = 0; j < MAX_ITEM_OPTIONS; j++) {
			if (tmpItem->option[j].index != invItem->option[j].index ||
			    tmpItem->option[j].value != invItem->option[j].value ||
			    tmpItem->option[j].param != invItem->option[j].param) {
				rodex->clean(sd, 1);
				return RODEX_SEND_MAIL_ITEM_ERROR;
			}
		}
	}

	if (total_zeny > 0 && pc->payzeny(sd, (int)total_zeny, LOG_TYPE_MAIL, NULL) != 0) {
		rodex->clean(sd, 1);
		return RODEX_SEND_MAIL_FATAL_ERROR;
	}

	for (i = 0; i < RODEX_MAX_ITEM; i++) {
		int16 idx = sd->rodex.tmp.items[i].idx;

		if (sd->rodex.tmp.items[i].item.nameid == 0) {
			continue;
		}

		if (pc->delitem(sd, idx, sd->rodex.tmp.items[i].item.amount, 0, DELITEM_NORMAL, LOG_TYPE_MAIL) != 0) {
			rodex->clean(sd, 1);
			return RODEX_SEND_MAIL_ITEM_ERROR;
		}
	}

	sd->rodex.tmp.zeny = zeny;
	sd->rodex.tmp.is_read = false;
	sd->rodex.tmp.is_deleted = false;
	sd->rodex.tmp.send_date = (int)time(NULL);
	sd->rodex.tmp.expire_date = (int)time(NULL) + RODEX_EXPIRE;
	if (strlen(sd->rodex.tmp.body) > 0)
		sd->rodex.tmp.type |= MAIL_TYPE_TEXT;
	if (sd->rodex.tmp.zeny > 0)
		sd->rodex.tmp.type |= MAIL_TYPE_ZENY;
	sd->rodex.tmp.sender_id = sd->status.char_id;
	safestrncpy(sd->rodex.tmp.sender_name, sd->status.name, NAME_LENGTH);
	safestrncpy(sd->rodex.tmp.title, title, RODEX_TITLE_LENGTH);
	safestrncpy(sd->rodex.tmp.body, body, RODEX_BODY_LENGTH);

	intif->rodex_sendmail(&sd->rodex.tmp);
	return RODEX_SEND_MAIL_SUCCESS; // this will not inform client of the success yet. (see rodex_send_mail_result)
}

/// The result of a message send, called by char-server
/// @param ssd : Sender's sd
/// @param rsd : Receiver's sd
/// @param result : Message sent (true) or failed (false)
static void rodex_send_mail_result(struct map_session_data *ssd, struct map_session_data *rsd, bool result)
{
	if (ssd != NULL) {
		rodex->clean(ssd, 1);
		if (result == false) {
			clif->rodex_send_mail_result(ssd->fd, ssd, RODEX_SEND_MAIL_FATAL_ERROR);
			return;
		}

		clif->rodex_send_mail_result(ssd->fd, ssd, RODEX_SEND_MAIL_SUCCESS);
	}

	if (rsd != NULL) {
		clif->rodex_icon(rsd->fd, true);
		clif_disp_onlyself(rsd, msg_sd(rsd, 236)); // "You've got a new mail!"
	}
	return;
}

/// Retrieves one message from character
/// @param sd : Character
/// @param mail_id : Mail ID that's being retrieved
/// Returns the message
static struct rodex_message *rodex_get_mail(struct map_session_data *sd, int64 mail_id)
{
	int i;
	struct rodex_message *msg;
	int char_id;

	nullpo_retr(NULL, sd);

	ARR_FIND(0, VECTOR_LENGTH(sd->rodex.messages), i, VECTOR_INDEX(sd->rodex.messages, i).id == mail_id);
	if (i == VECTOR_LENGTH(sd->rodex.messages))
		return NULL;

	msg = &VECTOR_INDEX(sd->rodex.messages, i);

	char_id = sd->status.char_id;

	if ((msg->is_deleted == true)
		|| (msg->expire_date < time(NULL) && ((msg->receiver_accountid > 0) || (msg->receiver_id == char_id && msg->sender_id != char_id)))
		|| (msg->expire_date + RODEX_EXPIRE < time(NULL))
		)
		return NULL;

	return msg;
}

/// Request to read a mail by its ID
/// @param sd : Who's reading
/// @param mail_id : Mail ID to be read
static void rodex_read_mail(struct map_session_data *sd, int64 mail_id)
{
	struct rodex_message *msg;

	nullpo_retv(sd);

	msg = rodex->get_mail(sd, mail_id);
	nullpo_retv(msg);

	if (msg->opentype == RODEX_OPENTYPE_RETURN) {
		if (msg->sender_read == false) {
			intif->rodex_updatemail(sd, msg->id, 0, 4);
			msg->sender_read = true;
		}
	} else {
		if (msg->is_read == false) {
			intif->rodex_updatemail(sd, msg->id, 0, 0);
			msg->is_read = true;
		}
	}

	clif->rodex_read_mail(sd, msg->opentype, msg);
}

/// Deletes a mail
/// @param sd : Who's deleting
/// @param mail_id : Mail ID to be deleted
static void rodex_delete_mail(struct map_session_data *sd, int64 mail_id)
{
	struct rodex_message *msg;

	nullpo_retv(sd);

	msg = rodex->get_mail(sd, mail_id);
	nullpo_retv(msg);

	msg->is_deleted = true;
	intif->rodex_updatemail(sd, msg->id, 0, 3);

	clif->rodex_delete_mail(sd, msg->opentype, msg->id);
}

/// give requested zeny from message to player
static void rodex_getZenyAck(struct map_session_data *sd, int64 mail_id, int8 opentype, int64 zeny)
{
	nullpo_retv(sd);
	if (zeny <= 0) {
		clif->rodex_request_zeny(sd, opentype, mail_id, RODEX_GET_ZENY_FATAL_ERROR);
		return;
	}

	// Updates the in-memory copy of this mail
	// It should never be null, but if it ends up being, that would simply mean that this
	// mail is gone from the user data, and that's fine, as the char-server did its work.
	struct rodex_message *msg = rodex->get_mail(sd, mail_id);
	if (msg != NULL) {
		msg->type &= ~MAIL_TYPE_ZENY;
		msg->zeny = 0;
	}

	if (pc->getzeny(sd, (int)zeny, LOG_TYPE_MAIL, NULL) != 0) {
		clif->rodex_request_zeny(sd, opentype, mail_id, RODEX_GET_ZENY_FATAL_ERROR);
		return;
	}

	clif->rodex_request_zeny(sd, opentype, mail_id, RODEX_GET_ZENY_SUCCESS);
}

/// Gets attached zeny
/// @param sd : Who's getting
/// @param mail_id : Mail ID that we're getting zeny from
static void rodex_get_zeny(struct map_session_data *sd, int8 opentype, int64 mail_id)
{
	nullpo_retv(sd);

	struct rodex_message *msg = rodex->get_mail(sd, mail_id);

	if (msg == NULL) {
		clif->rodex_request_zeny(sd, opentype, mail_id, RODEX_GET_ZENY_FATAL_ERROR);
		return;
	}

	if ((int64)sd->status.zeny + msg->zeny > MAX_ZENY) {
		clif->rodex_request_zeny(sd, opentype, mail_id, RODEX_GET_ZENY_LIMIT_ERROR);
		return;
	}

	intif->rodex_updatemail(sd, mail_id, opentype, 1);
}

// give requested items from message to player
static void rodex_getItemsAck(struct map_session_data *sd, int64 mail_id, int8 opentype, int count, const struct rodex_item *items)
{
	nullpo_retv(sd);
	nullpo_retv(items);

	if (VECTOR_LENGTH(sd->rodex.claim_list) < 1) {
		ShowError("rodex_getItemsAck: No mail ID queued for claiming.\n");
		return;
	}

	if (VECTOR_INDEX(sd->rodex.claim_list, 0) != mail_id) {
		ShowError("rodex_getItemsAck: Mail ID mismatch. Expected %"PRId64", got %"PRId64"\n", VECTOR_INDEX(sd->rodex.claim_list, 0), mail_id);
		return;
	}

	for (int i = 0; i < count; ++i) {
		const struct item *it = &items[i].item;

		if (it->nameid == 0) {
			continue;
		}

		if (pc->additem(sd, it, it->amount, LOG_TYPE_MAIL) != 0) {
			clif->rodex_request_items(sd, opentype, mail_id, RODEX_GET_ITEM_FULL_ERROR);
			VECTOR_ERASE(sd->rodex.claim_list, 0);
			return;
		}
	}

	clif->rodex_request_items(sd, opentype, mail_id, RODEX_GET_ITEMS_SUCCESS);

	// Remove the mail ID from the queue
	VECTOR_ERASE(sd->rodex.claim_list, 0);

	// Claim the next mail if there is one
	if (VECTOR_LENGTH(sd->rodex.claim_list) > 0)
		rodex->get_items(sd, opentype, VECTOR_INDEX(sd->rodex.claim_list, 0));
}

/// Gets attached item
/// @param sd : Who's getting
/// @param mail_id : Mail ID that we're getting items from
static void rodex_get_items(struct map_session_data *sd, int8 opentype, int64 mail_id)
{
	nullpo_retv(sd);

	int weight = 0;
	int empty_slots = 0;

	struct rodex_message *msg = rodex->get_mail(sd, mail_id);

	if (msg == NULL) {
		clif->rodex_request_items(sd, opentype, mail_id, RODEX_GET_ITEM_FATAL_ERROR);
		return;
	}

	if (msg->items_count < 1) {
		clif->rodex_request_items(sd, opentype, mail_id, RODEX_GET_ITEM_FATAL_ERROR);
		return;
	}

	for (int i = 0; i < RODEX_MAX_ITEM; ++i) {
		if (msg->items[i].item.nameid != 0) {
			weight += itemdb->search(msg->items[i].item.nameid)->weight * msg->items[i].item.amount;
		}
	}

	if ((sd->weight + weight > sd->max_weight)) {
		clif->rodex_request_items(sd, opentype, mail_id, RODEX_GET_ITEM_FULL_ERROR);
		return;
	}

	int required_slots = msg->items_count;
	for (int i = 0; i < sd->status.inventorySize; ++i) {
		if (sd->status.inventory[i].nameid == 0) {
			empty_slots++;
		} else if (itemdb->isstackable(sd->status.inventory[i].nameid) == 1) {
			int j;
			ARR_FIND(0, msg->items_count, j, sd->status.inventory[i].nameid == msg->items[j].item.nameid);
			if (j < msg->items_count) {
				struct item_data *idata = itemdb->search(sd->status.inventory[i].nameid);

				if ((idata->stack.inventory && sd->status.inventory[i].amount + msg->items[j].item.amount > idata->stack.amount) ||
					sd->status.inventory[i].amount + msg->items[j].item.amount > MAX_AMOUNT) {
					clif->rodex_request_items(sd, opentype, mail_id, RODEX_GET_ITEM_FULL_ERROR);
					return;
				}

				required_slots--;
			}
		}
	}

	if (empty_slots < required_slots) {
		clif->rodex_request_items(sd, opentype, mail_id, RODEX_GET_ITEM_FULL_ERROR);
		return;
	}

	// Queue the mail ID to be claimed
	int i = 0;
	ARR_FIND(0, VECTOR_LENGTH(sd->rodex.claim_list), i, VECTOR_INDEX(sd->rodex.claim_list, i) == mail_id);
	if (i == VECTOR_LENGTH(sd->rodex.claim_list)) {
		VECTOR_ENSURE(sd->rodex.claim_list, 1, 1);
		VECTOR_PUSH(sd->rodex.claim_list, mail_id);
	}

	// If another mail is being claimed, wait for it to finish
	if (VECTOR_LENGTH(sd->rodex.claim_list) > 1 && VECTOR_INDEX(sd->rodex.claim_list, 0) != mail_id)
		return;

	msg->type &= ~MAIL_TYPE_ITEM;
	msg->items_count = 0;
	intif->rodex_updatemail(sd, mail_id, opentype, 2);
}

/// Cleans user's RoDEX related data
/// - should be called everytime we're going to start/stop using rodex in this character
/// @param sd : Target to clean
/// @param flag :
///     0 - clear everything
///     1 - clear tmp only
static void rodex_clean(struct map_session_data *sd, int8 flag)
{
	nullpo_retv(sd);

	if (flag == 0) {
		VECTOR_CLEAR(sd->rodex.messages);
		VECTOR_CLEAR(sd->rodex.claim_list);
	}
	sd->state.workinprogress &= ~2;
	memset(&sd->rodex.tmp, 0x0, sizeof(sd->rodex.tmp));
}

/// User request to open rodex, load mails from char-server
/// @param sd : Who's requesting
/// @param open_type : Box Type (see RODEX_OPENTYPE)
static void rodex_open(struct map_session_data *sd, int8 open_type, int64 first_mail_id)
{
#if PACKETVER >= 20170419
	const int type = 1;
#else
	const int type = 0;
#endif
	nullpo_retv(sd);
	if (open_type == RODEX_OPENTYPE_ACCOUNT && battle_config.feature_rodex_use_accountmail == false)
		open_type = RODEX_OPENTYPE_MAIL;

	intif->rodex_requestinbox(sd->status.char_id, sd->status.account_id, type, open_type, first_mail_id);
}

/// User request to read next page of mails
/// @param sd : Who's requesting
/// @param open_type : Box Type (see RODEX_OPENTYPE)
/// @param last_mail_id : The last mail from the current page
static void rodex_next_page(struct map_session_data *sd, int8 open_type, int64 last_mail_id)
{
	int64 msg_count, page_start = 0;
	nullpo_retv(sd);

	if (open_type == RODEX_OPENTYPE_ACCOUNT && battle_config.feature_rodex_use_accountmail == false) {
		// Should not happen
		open_type = RODEX_OPENTYPE_MAIL;
		rodex->open(sd, open_type, 0);
		return;
	}

	msg_count = VECTOR_LENGTH(sd->rodex.messages);

	if (last_mail_id > 0) {
		// Find where the page starts
		ARR_FIND(0, msg_count, page_start, VECTOR_INDEX(sd->rodex.messages, page_start).id == last_mail_id);
		if (page_start > 0 && page_start < msg_count) {
			--page_start; // Valid page, get first item of next page
		} else {
			page_start = msg_count - 1; // Should not happen, invalid lower_id given
		}
		clif->rodex_send_maillist(sd->fd, sd, open_type, page_start);
	}
}

/// User's request to refresh his mail box
/// @param sd : Who's requesting
/// @param open_type : Box Type (See RODEX_OPENTYPE)
/// @param first_mail_id : The first mail id known by client, currently unused
static void rodex_refresh(struct map_session_data *sd, int8 open_type, int64 first_mail_id)
{
	nullpo_retv(sd);
	if (open_type == RODEX_OPENTYPE_ACCOUNT && battle_config.feature_rodex_use_accountmail == false)
		open_type = RODEX_OPENTYPE_MAIL;

	// Some clients sends the first mail id it currently has and expects to receive
	// a list of newer mails, other clients sends first mail id as 0 and expects
	// to receive the first page (as if opening the box)
	if (first_mail_id > 0) {
		intif->rodex_requestinbox(sd->status.char_id, sd->status.account_id, 1, open_type, first_mail_id);
	} else {
		intif->rodex_requestinbox(sd->status.char_id, sd->status.account_id, 0, open_type, first_mail_id);
	}
}

static void do_init_rodex(bool minimal)
{
	if (minimal)
		return;
}

static void do_final_rodex(void)
{

}

void rodex_defaults(void)
{
	rodex = &rodex_s;

	rodex->init = do_init_rodex;
	rodex->final = do_final_rodex;

	rodex->isenabled = rodex_isenabled;

	/** message creation utilities */
	rodex->mail_init = rodex_mail_init;
	rodex->mail_try_add_item = rodex_mail_try_add_item;
	rodex->mail_try_add_zeny = rodex_mail_try_add_zeny;
	rodex->mail_clear_attachments = rodex_mail_clear_attachments;
	rodex->mail_send = rodex_mail_send;

	/** player-related interface */
	rodex->open = rodex_open;
	rodex->next_page = rodex_next_page;
	rodex->refresh = rodex_refresh;
	rodex->add_item = rodex_add_item;
	rodex->remove_item = rodex_remove_item;
	rodex->check_player = rodex_check_player;
	rodex->send_mail = rodex_send_mail;
	rodex->send_mail_result = rodex_send_mail_result;
	rodex->get_mail = rodex_get_mail;
	rodex->read_mail = rodex_read_mail;
	rodex->delete_mail = rodex_delete_mail;
	rodex->get_zeny = rodex_get_zeny;
	rodex->get_items = rodex_get_items;
	rodex->clean = rodex_clean;
	rodex->getZenyAck = rodex_getZenyAck;
	rodex->getItemsAck = rodex_getItemsAck;
}
