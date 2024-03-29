/*
 *  Copyright (C) 2009 Sony Computer Entertainment Inc.
 *  Copyright 2009 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#define _GNU_SOURCE

#include <assert.h>
#include <errno.h>
#include <string.h>

#include "log/log.h"
#include "talloc/talloc.h"
#include "ui/common/ui-system.h"
#include "nc-menu.h"

/**
 * pmenu_exit_cb - Callback helper that runs run menu.on_exit().
 */

int pmenu_exit_cb(struct pmenu_item *item)
{
	assert(item->pmenu->on_exit);
	item->pmenu->on_exit(item->pmenu);
	return 0;
}

/**
 * pmenu_find_selected - Find the selected pmenu_item.
 */

struct pmenu_item *pmenu_find_selected(struct pmenu *menu)
{
	return pmenu_item_from_arg(item_userptr(current_item(menu->ncm)));
}

static int pmenu_post(struct nc_scr *scr)
{
	int result;
	struct pmenu *menu = pmenu_from_scr(scr);

	result = post_menu(menu->ncm);

	nc_scr_frame_draw(scr);
	redrawwin(menu->scr.main_ncw);
	wrefresh(menu->scr.main_ncw);

	return result;
}

static int pmenu_unpost(struct nc_scr *scr)
{
	return unpost_menu(pmenu_from_scr(scr)->ncm);
}

static void pmenu_resize(struct nc_scr *scr)
{
	/* FIXME: menus can't be resized, need to recreate here */
	pmenu_unpost(scr);
	pmenu_post(scr);
}

/**
 * pmenu_item_init - Allocate and initialize a new pmenu_item instance.
 *
 * Returns a pointer the the initialized struct pmenu_item instance or NULL
 * on error. The caller is responsible for calling talloc_free() for the
 * returned instance.
 */

struct pmenu_item *pmenu_item_alloc(struct pmenu *menu)
{
	/* Items go with the menu, not the pointer array. */

	struct pmenu_item *i = talloc_zero(menu, struct pmenu_item);

	return i;
}

struct pmenu_item *pmenu_item_setup(struct pmenu *menu, struct pmenu_item *i,
	unsigned int index, const char *name)
{
	assert(i);
	assert(name);

	if (!i)
		return NULL;

	i->i_sig = pb_item_sig;
	i->pmenu = menu;
	i->nci = new_item(name, NULL);

	if (!i->nci) {
		talloc_free(i);
		return NULL;
	}

	set_item_userptr(i->nci, i);

	menu->items[index] = i->nci;

	return i;
}

static int pmenu_item_get_index(const struct pmenu_item *item)
{
	unsigned int i;

	for (i = 0; i < item->pmenu->item_count; i++)
		if (item->pmenu->items[i] == item->nci)
			return i;

	pb_log("%s: not found: %p %s\n", __func__, item,
		(item ? item->nci->name.str : "(null)"));
	return -1;
}

/**
 * pmenu_item_replace - Replace the menu item with a new one.
 *
 * Use this routine to change a menu item's text.
 */

int pmenu_item_replace(struct pmenu_item *i, const char *name)
{
	struct pmenu *menu;
	ITEM *nci;
	int index;

	assert(name);
	assert(i->nci);

	menu = i->pmenu;
	index = pmenu_item_get_index(i);

	if (index < 0) {
		assert(0 && "get_index failed");
		return -1;
	}

	nci = new_item(name, NULL);

	if (!nci) {
		assert(0 && "new_item failed");
		return -1;
	}

	set_item_userptr(nci, i);

	menu->scr.unpost(&menu->scr);
	set_menu_items(menu->ncm, NULL);

	// FIXME: need to assure item name is a talloc string.
	/* talloc_free((char *)item_name(i->nci)); */

	free_item(i->nci);
	menu->items[index] = nci;
	i->nci = nci;

	set_menu_items(menu->ncm, menu->items);
	menu->scr.post(&menu->scr);

	return 0;
}

/**
 * pmenu_move_cursor - Move the cursor.
 * @req: An ncurses request or char to send to menu_driver().
 */

static void pmenu_move_cursor(struct pmenu *menu, int req)
{
	menu_driver(menu->ncm, req);
	wrefresh(menu->scr.main_ncw);
}

/**
 * pmenu_process_key - Process a user keystroke.
 */

static void pmenu_process_key(struct nc_scr *scr, int key)
{
	struct pmenu *menu = pmenu_from_scr(scr);
	struct pmenu_item *item = pmenu_find_selected(menu);

	nc_scr_status_free(&menu->scr);

	if (menu->hot_key)
		key = menu->hot_key(menu, item, key);

	switch (key) {
	case 27: /* ESC */
		if (menu->on_exit)
			menu->on_exit(menu);
		nc_flush_keys();
		return;

	case KEY_PPAGE:
		pmenu_move_cursor(menu, REQ_SCR_UPAGE);
		break;
	case KEY_NPAGE:
		pmenu_move_cursor(menu, REQ_SCR_DPAGE);
		break;
	case KEY_HOME:
		pmenu_move_cursor(menu, REQ_FIRST_ITEM);
		break;
	case KEY_END:
		pmenu_move_cursor(menu, REQ_LAST_ITEM);
		break;
	case KEY_UP:
		pmenu_move_cursor(menu, REQ_UP_ITEM);
		break;
	case KEY_DOWN:
		pmenu_move_cursor(menu, REQ_DOWN_ITEM);
		break;
	case 'e':
		if (item->on_edit)
			item->on_edit(item);
		break;
	case 'o':
		DBGS("on_open: %p\n", menu->on_open);
		if (menu->on_open)
			menu->on_open(menu);
		break;
	case '\n':
	case '\r':
		if (item->on_execute)
			item->on_execute(item);
		break;
	default:
		menu_driver(menu->ncm, key);
		break;
	}
}

/**
 * pmenu_grow - Grow the item array.
 * @count: The count of new items.
 *
 * The item array must be disconnected prior to calling pmenu_grow().
 * Returns the insert point index.
 */

unsigned int pmenu_grow(struct pmenu *menu, unsigned int count)
{
	unsigned int tmp;

	assert(item_count(menu->ncm) == 0 && "not disconnected");

	pb_log("%s: %u current + %u new = %u\n", __func__, menu->item_count,
		count, menu->item_count + count);

	/* Note that items array has a null terminator. */

	menu->items = talloc_realloc(menu, menu->items, ITEM *,
		menu->item_count + count + 1);

	memmove(menu->items + menu->insert_pt + count,
		menu->items + menu->insert_pt,
		(menu->item_count - menu->insert_pt + 1) * sizeof(ITEM *));

	memset(menu->items + menu->insert_pt, 0, count * sizeof(ITEM *));

	tmp = menu->insert_pt;
	menu->insert_pt += count;
	menu->item_count += count;

	return tmp;
}

/**
 * pmenu_remove - Remove an item from the item array.
 *
 * The item array must be disconnected prior to calling pmenu_remove()
 */

int pmenu_remove(struct pmenu *menu, struct pmenu_item *item)
{
	int index;

	assert(item_count(menu->ncm) == 0 && "not disconnected");

	assert(menu->item_count);

	pb_log("%s: %u\n", __func__, menu->item_count);

	index = pmenu_item_get_index(item);

	if (index < 0)
		return -1;

	free_item(item->nci);
	talloc_free(item);

	/* Note that items array has a null terminator. */

	menu->insert_pt--;
	menu->item_count--;

	memmove(&menu->items[index], &menu->items[index + 1],
		(menu->item_count - index + 1) * sizeof(ITEM *));
	menu->items = talloc_realloc(menu, menu->items, ITEM *,
		menu->item_count + 1);

	return 0;
}

/**
 * pmenu_init - Allocate and initialize a new menu instance.
 *
 * Returns a pointer the the initialized struct pmenu instance or NULL on error.
 * The caller is responsible for calling talloc_free() for the returned
 * instance.
 */

struct pmenu *pmenu_init(void *ui_ctx, unsigned int item_count,
	void (*on_exit)(struct pmenu *))
{
	struct pmenu *menu = talloc_zero(ui_ctx, struct pmenu);

	if (!menu)
		return NULL;

	/* note items array has a null terminator */

	menu->items = talloc_zero_array(menu, ITEM *, item_count + 1);

	if (!menu->items) {
		talloc_free(menu);
		return NULL;
	}

	nc_scr_init(&menu->scr, pb_pmenu_sig, 0, ui_ctx, pmenu_process_key,
		pmenu_post, pmenu_unpost, pmenu_resize);

	menu->item_count = item_count;
	menu->insert_pt = 0; /* insert from top */
	menu->on_exit = on_exit;

	return menu;
}

/**
 * pmenu_setup - Create nc menu, setup nc windows.
 *
 */

int pmenu_setup(struct pmenu *menu)
{
	assert(!menu->ncm);

	menu->ncm = new_menu(menu->items);

	if (!menu->ncm) {
		pb_log("%s:%d: new_menu failed: %s\n", __func__, __LINE__,
			strerror(errno));
		return -1;
	}

	set_menu_win(menu->ncm, menu->scr.main_ncw);
	set_menu_sub(menu->ncm, menu->scr.sub_ncw);

	/* Makes menu scrollable. */
	set_menu_format(menu->ncm, LINES - nc_scr_frame_lines, 1);

	return 0;
}

/**
 * pmenu_delete - Delete a menu instance.
 *
 */

void pmenu_delete(struct pmenu *menu)
{
	unsigned int i;

	assert(menu->scr.sig == pb_pmenu_sig);
	menu->scr.sig = pb_removed_sig;

	for (i = item_count(menu->ncm); i; i--)
		free_item(menu->items[i - 1]);

	free_menu(menu->ncm);
	delwin(menu->scr.sub_ncw);
	delwin(menu->scr.main_ncw);
	talloc_free(menu);
}
