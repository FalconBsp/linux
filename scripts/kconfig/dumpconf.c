/*
 *  Dump Kconfig ala tree command
 *
 *  Copyright 2008 Sony Corporation.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  version 2 of the  License.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <locale.h>

#define LKC_DIRECT_LINK
#include "lkc.h"

static int show_hidden = 0;
static int show_val = 1;
static int show_file = 0;
static int show_help = 0;

struct prefix {
	const char *str;
	struct prefix *right;
	struct prefix *left;
};

struct prefix prefix_start = {
	.str = "",
	.right = NULL,
	.left = NULL,
};

static void tree_menu(struct menu *menu, struct prefix *prfx);

struct prefix *push_prefix(struct prefix *prfx, const char *str)
{
	struct prefix *next = prfx->right;
	if (!next) {
		next = malloc(sizeof(struct prefix));
		if (!next) {
			fprintf(stderr, "dumpconfig: out of memory\n");
			exit(1);
		}
	}
	next->right = NULL;
	next->left = prfx;
	next->str = str;
	prfx->right = next;
	return next;
}

static void destroy_prefix(void)
{
	struct prefix *cur = prefix_start.right;
	struct prefix *next;
	while (cur) {
		next = cur->right;
		free(cur);
		cur = next;
	}
}

static void print_prefix(struct prefix *prfx)
{
	struct prefix *cur = &prefix_start;
	for (;;) {
		printf("%s", cur->str);
		if (cur == prfx)
			break;
		cur = cur->right;
	}
}

static void print_value(struct menu *menu)
{
	int type;
	char ch;
	tristate val;
	struct symbol *sym = menu->sym;

	if (!sym)
		return;
	if (sym_is_choice(sym) && !sym_is_changable(sym))
		return;
	type = sym_get_type(sym);
	val = sym_get_tristate_value(sym);
	if (sym_is_choice_value(sym)) {
		if (val == yes)
			printf("(X) ");
		else
			printf("    ");
		return;
	}
	if (type == S_BOOLEAN) {
		printf("[%c]", sym_get_tristate_value(sym) == no ? ' ' : '*');
	}
	else if (type == S_TRISTATE) {
		switch (sym_get_tristate_value(sym)) {
		case yes: ch = '*'; break;
		case mod: ch = 'M'; break;
		default:  ch = ' '; break;
		}
		printf("<%c>", ch);
	}
	else {
		printf("(%s)", sym_get_string_value(sym));
	}
	if (!sym_is_changable(sym)) {
		printf("%%");
	}
	printf(" ");
}

static void tree_title(struct menu *menu)
{
	if (show_val)
		print_value(menu);
	if (!menu->sym && !menu->prompt) {
		printf("(?)\n");
		return;
	}
	if (menu->prompt) {
		printf("%s", menu->prompt->text);
	}
	if (menu->sym && menu->sym->name) {
		if (menu->prompt)
			printf(" ");
		printf("(CONFIG_%s)", menu->sym->name);
	}
	if (   (menu->prompt && menu->prompt->type == P_MENU)
	    || (menu->sym && sym_is_choice(menu->sym))) {
		printf("  --->");
	}
	printf("\n");
}

static const char *print_oneline(const char *str)
{
	while (*str && *str != '\n') {
		putc(*str, stdout);
		str++;
	}
	putc('\n', stdout);
	if (*str)
		return str + 1;
	else
		return str;
}

static void print_multiline(const char *str, struct prefix *prfx)
{
	str = print_oneline(str);
	while (*str) {
		print_prefix(prfx);
		str = print_oneline(str);
	}
}

static void tree_attr(struct menu *menu, struct prefix *prfx)
{
	if (show_file && menu->file) {
		print_prefix(prfx);
		printf("file: %s\n", menu->file->name);
	}
	if (show_help && menu->sym && menu->help) {
		print_prefix(prfx);
		printf("help: ");
		print_multiline(menu->help, push_prefix(prfx, "      "));
	}
}

static int hidden(struct menu *menu)
{
	if (!show_hidden && !menu_is_visible(menu))
		return 1;
	if (!menu->sym && !menu->prompt && !menu->list)
		return 1;
	return 0;
}

static struct menu *last_visible_child(struct menu *menu)
{
	struct menu *child;
	struct menu *ans = NULL;
	for (child = menu->list; child; child = child->next) {
		if (!hidden(child))
			ans = child;
	}
	return ans;
}

static int has_visible_child(struct menu *menu)
{
	return last_visible_child(menu) != NULL;
}

static void tree_child(struct menu *menu, struct prefix *prfx)
{
	struct menu *child;
	struct menu *last = last_visible_child(menu);
	struct prefix *new_prefix;
	for (child = menu->list; child; child = child->next) {
		if (hidden(child))
			continue;
		print_prefix(prfx);
		if (child == last) {
			/* last child */
			printf("`-- ");
			new_prefix = push_prefix(prfx, "    ");
		}
		else {
			printf("|-- ");
			new_prefix = push_prefix(prfx, "|   ");
		}
		tree_menu(child, new_prefix);
	}
}

static void tree_menu(struct menu *menu, struct prefix *prfx)
{
	tree_title(menu);
	tree_attr(menu, push_prefix(prfx, has_visible_child(menu)
				           ? "|    " : "     "));
	tree_child(menu, prfx);
}

int main(int ac, char **av)
{
	int i;
	for (i = 1; i < ac; ++i) {
		if (av[i][0] != '-')
			break;
		if (!strcmp(av[i], "--hidden")) {
			show_hidden = 1;
		}
		else if (!strcmp(av[i], "--no-val")) {
			show_val = 0;
		}
		else if (!strcmp(av[i], "--file")) {
			show_file = 1;
		}
		else if (!strcmp(av[i], "--help")) {
			show_help = 1;
		}
		else if (!strcmp(av[i], "--all")) {
			show_hidden = 1;
			show_file = 1;
			show_help = 1;
		}
		else {
			fprintf(stderr, "unknown option: %s\n", av[i]);
		}
	}
	if (i >= ac) {
		fprintf(stderr, "usage: %s [options] FILE\n", av[0]);
		exit(1);
	}
	setbuf(stdout, 0);
	conf_parse(av[i]);
	conf_read(NULL);
	tree_menu(&rootmenu, &prefix_start);
	destroy_prefix();
	return 0;
}
