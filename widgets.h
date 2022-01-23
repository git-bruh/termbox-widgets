#ifndef WIDGETS_H
#define WIDGETS_H
#include "termbox.h"

#include <stdbool.h>

enum { WIDGET_CH_MAX = 2 }; /* Max width. */

enum widget_error { WIDGET_NOOP = 0, WIDGET_REDRAW };

/* The rectangle in which the widget will be drawn. */
struct widget_points {
	int x1; /* x of top-left corner. */
	int x2; /* x of bottom-right corner. */
	int y1; /* y of top-left corner. */
	int y2; /* y of bottom-right corner. */
};

uint32_t
widget_uc_sanitize(uint32_t uc, int *width);
int
widget_str_width(const char *str);
bool
widget_points_in_bounds(const struct widget_points *points, int x, int y);
void
widget_points_set(struct widget_points *points, int x1, int x2, int y1, int y2);
bool
widget_should_forcebreak(int width);
bool
widget_should_scroll(int x, int width, int max_width);
/* Returns the number of times y was advanced. */
int
widget_adjust_xy(int width, const struct widget_points *points, int *x, int *y);
int
widget_print_str(
  int x, int y, int max_x, uintattr_t fg, uintattr_t bg, const char *str);
int
widget_pad_center(int part, int total);

/* Input. */
enum input_event {
	INPUT_CLEAR = 0,
	INPUT_DELETE,
	INPUT_DELETE_WORD,
	INPUT_RIGHT,
	INPUT_RIGHT_WORD,
	INPUT_LEFT,
	INPUT_LEFT_WORD,
	INPUT_ADD /* Must pass an uint32_t argument. */
};

struct input {
	bool scroll_horizontal;
	int start_y;
	uintattr_t bg;
	size_t cur_buf; /* Current position inside buf. */
	uint32_t *buf;	/* We use a basic array instead of something like a linked
					 * list of small arrays  or a gap buffer as pretty much all
					 * messages are small enough that array
					 * insertion / deletion performance isn't an issue. */
};

int
input_init(struct input *input, uintattr_t bg, bool scroll_horizontal);
void
input_finish(struct input *input);
/* rows will be filled with the number of rows taken by the input field. */
void
input_redraw(struct input *input, struct widget_points *points, int *rows);
enum widget_error
input_handle_event(struct input *input, enum input_event event, ...);
char *
input_buf(struct input *input);

/* Treeview. */

/* Called to draw the data. */
typedef void (*treeview_draw_cb)(
  void *data, struct widget_points *points, bool is_selected);
/* Called when destroying the node. */
typedef void (*treeview_free_cb)(void *data);

enum treeview_event {
	TREEVIEW_EXPAND = 0,
	TREEVIEW_UP,
	TREEVIEW_DOWN,
	/* Must pass a treeview_node struct for INSERT*
	 * If WIDGET_NOOP is returned here then the operation was invalid and the
	 * passed node should be freed to avoid leaks. */
	TREEVIEW_INSERT,		/* Add a child node to the selected node. */
	TREEVIEW_INSERT_PARENT, /* Add a node to the selected node's parent. */
	TREEVIEW_DELETE, /* Delete the selected node along with it's children. The
						root node cannot be deleted. */
};

struct treeview_node {
	bool is_expanded; /* Whether it's children are visible. */
	size_t index;	  /* Index in the nodes array. */
	struct treeview_node *parent;
	struct treeview_node **nodes;
	void *data; /* Any user data. */
	treeview_draw_cb draw_cb;
	treeview_free_cb free_cb;
};

struct treeview {
	int skipped; /* A hack used to skip lines in recursive rendering. */
	int start_y;
	struct treeview_node root;
	struct treeview_node *selected;
};

/* Pass NULL as the free_cb if the data is stack allocated. */
struct treeview_node *
treeview_node_alloc(
  void *data, treeview_draw_cb draw_cb, treeview_free_cb free_cb);
int
treeview_node_init(struct treeview_node *node, void *data,
  treeview_draw_cb draw_cb, treeview_free_cb free_cb);
void
treeview_node_destroy(struct treeview_node *node);
int
treeview_node_add_child(
  struct treeview_node *parent, struct treeview_node *child);
int
treeview_init(struct treeview *treeview);
void
treeview_finish(struct treeview *treeview);
void
treeview_redraw(struct treeview *treeview, struct widget_points *points);
enum widget_error
treeview_event(struct treeview *treeview, enum treeview_event event, ...);
#endif /* !WIDGETS_H */

#ifdef WIDGETS_IMPL
#include "stb_ds.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

static int
min(int x, int y) {
	return x < y ? x : y;
}

static int
max(int x, int y) {
	return x > y ? x : y;
}

uint32_t
widget_uc_sanitize(uint32_t uc, int *width) {
	int tmp_width = wcwidth((wchar_t) uc);

	switch (uc) {
	case '\n':
		*width = 0;
		return uc;
	case '\t':
		*width = 1;
		return ' ';
	default:
		if (tmp_width < 0) {
			*width = 1;
			return uc;
		}

		if (tmp_width == 0 || tmp_width > WIDGET_CH_MAX) {
			*width = wcwidth(L'�');
			return L'�';
		}

		*width = tmp_width;
		return uc;
	}
}

int
widget_str_width(const char *str) {
	int width = 0;

	if (str) {
		for (size_t i = 0; str[i];) {
			int ch_width = 0;
			uint32_t uc = 0;

			i += (size_t) tb_utf8_char_to_unicode(&uc, &str[i]);
			widget_uc_sanitize(uc, &ch_width);

			width += ch_width;
		}
	}

	return width;
}

bool
widget_points_in_bounds(const struct widget_points *points, int x, int y) {
	return (
	  x >= points->x1 && x < points->x2 && y >= points->y1 && y < points->y2);
}

/* Ensure that no point is negative or out of bounds. */
void
widget_points_set(
  struct widget_points *points, int x1, int x2, int y1, int y2) {
	if (!points) {
		return;
	}

	int height = tb_height();
	int width = tb_width();

	*points = (struct widget_points) {.x1 = min(max(0, x1), width),
	  .x2 = min(max(0, x2), width),
	  .y1 = min(max(0, y1), height),
	  .y2 = min(max(0, y2), height)};
}

bool
widget_should_forcebreak(int width) {
	return width == 0;
}

bool
widget_should_scroll(int x, int width, int max_width) {
	return (x >= (max_width - width) || (widget_should_forcebreak(width)));
}

/* Returns the number of times y was advanced. */
int
widget_adjust_xy(
  int width, const struct widget_points *points, int *x, int *y) {
	int original_y = *y;

	if ((widget_should_scroll(*x, width, points->x2))) {
		*x = points->x1;
		(*y)++;
	}

	/* Newline, already scrolled. */
	if ((widget_should_forcebreak(width))) {
		return *y - original_y;
	}

	*x += width;

	/* We must accomodate for another character to move the cursor to the next
	 * line, which prevents us from adding an unreachable character. */
	if ((widget_should_scroll(*x, WIDGET_CH_MAX, points->x2))) {
		*x = points->x1;
		(*y)++;
	}

	return *y - original_y;
}

int
widget_print_str(
  int x, int y, int max_x, uintattr_t fg, uintattr_t bg, const char *str) {
	if (!str) {
		return 0;
	}

	uint32_t uc = 0;
	int width = 0;
	int original = x;

	while (*str) {
		str += tb_utf8_char_to_unicode(&uc, str);
		uc = widget_uc_sanitize(uc, &width);

		if (width == 0) {
			break;
		}

		tb_set_cell(x, y, uc, fg, bg);

		if ((x += width) > max_x) {
			break;
		}
	}

	return x - original;
}

int
widget_pad_center(int part, int total) {
	int padding = (int) round(((double) (total - part)) / 2);

	if (padding < 0) {
		return 0;
	}

	return padding;
}

enum {
	BUF_MAX = 2000,
};

static enum widget_error
buf_add(struct input *input, uint32_t ch) {
	if (((arrlenu(input->buf)) + 1) >= BUF_MAX) {
		return WIDGET_NOOP;
	}

	arrins(input->buf, input->cur_buf, ch);
	input->cur_buf++;

	return WIDGET_REDRAW;
}

static enum widget_error
buf_left(struct input *input) {
	if (input->cur_buf > 0) {
		input->cur_buf--;

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_leftword(struct input *input) {
	if (input->cur_buf > 0) {
		do {
			input->cur_buf--;
		} while (input->cur_buf > 0
				 && ((iswspace((wint_t) input->buf[input->cur_buf]))
					 || !(iswspace((wint_t) input->buf[input->cur_buf - 1]))));

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_right(struct input *input) {
	if (input->cur_buf < arrlenu(input->buf)) {
		input->cur_buf++;

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_rightword(struct input *input) {
	size_t buf_len = arrlenu(input->buf);

	if (input->cur_buf < buf_len) {
		do {
			input->cur_buf++;
		} while (input->cur_buf < buf_len
				 && !((iswspace((wint_t) input->buf[input->cur_buf]))
					  && !(iswspace((wint_t) input->buf[input->cur_buf - 1]))));

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_del(struct input *input) {
	if (input->cur_buf > 0) {
		--input->cur_buf;

		arrdel(input->buf, input->cur_buf);

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

static enum widget_error
buf_delword(struct input *input) {
	size_t original_cur = input->cur_buf;

	if ((buf_leftword(input)) == WIDGET_REDRAW) {
		arrdeln(input->buf, input->cur_buf, original_cur - input->cur_buf);

		return WIDGET_REDRAW;
	}

	return WIDGET_NOOP;
}

int
input_init(struct input *input, uintattr_t bg, bool scroll_horizontal) {
	if (!input) {
		return -1;
	}

	*input = (struct input) {.bg = bg, .scroll_horizontal = scroll_horizontal};

	return 0;
}

void
input_finish(struct input *input) {
	if (!input) {
		return;
	}

	arrfree(input->buf);
	memset(input, 0, sizeof(*input));
}

void
input_redraw(struct input *input, struct widget_points *points, int *rows) {
	if (!input || !points || !rows) {
		return;
	}

	size_t buf_len = arrlenu(input->buf);

	if (input->scroll_horizontal) {
		int width = 0;
		int max_width = points->x2 - points->x1;
		int start_width = -1;

		for (size_t i = 0; i < (input->cur_buf + 1) && i < buf_len; i++) {
			int ch_width = 0;
			widget_uc_sanitize(input->buf[i], &ch_width);

			width += ch_width;
		}

		if (width >= max_width) {
			start_width = width - max_width;
		}

		int x = points->x1;

		width = 0;
		size_t start = 0;

		for (; start < buf_len && width <= start_width; start++) {
			int ch_width = 0;
			widget_uc_sanitize(input->buf[start], &ch_width);

			width += ch_width;
		}

		tb_set_cursor(points->x1, points->y1);

		for (size_t i = start; i < buf_len; i++) {
			int ch_width = 0;
			uint32_t uc = widget_uc_sanitize(input->buf[i], &ch_width);

			if ((x + ch_width) >= points->x2) {
				break;
			}

			if (!widget_should_forcebreak(ch_width)) {
				tb_set_cell(x, points->y1, uc, TB_DEFAULT, input->bg);
			}

			x += ch_width;

			if (i + 1 == input->cur_buf) {
				tb_set_cursor(x, points->y1);
			}

			assert((widget_points_in_bounds(points, x, points->y1)));
		}

		*rows = 1;
		return;
	}

	int max_height = points->y2 - points->y1;
	int cur_x = points->x1;
	int cur_line = 1;
	int lines = 1;

	{
		int x = points->x1;
		int y = 0;
		int width = 0;

		for (size_t written = 0; written < buf_len; written++) {
			widget_uc_sanitize(input->buf[written], &width);

			lines += widget_adjust_xy(width, points, &x, &y);

			if ((written + 1) == input->cur_buf) {
				cur_x = x;
				cur_line = lines;
			}
		}
	}

	/* Don't mess up when coming back to the start after deleting a lot of
	 * text. */
	if (lines < max_height) {
		input->start_y = 0;
	}

	int diff_forward = cur_line - (input->start_y + max_height);
	int diff_backward = input->start_y - (cur_line - 1);

	if (diff_backward > 0) {
		input->start_y -= diff_backward;
	} else if (diff_forward > 0) {
		input->start_y += diff_forward;
	}

	assert(input->start_y >= 0);
	assert(input->start_y < lines);

	int width = 0;
	int line = 0;
	size_t written = 0;

	bool lines_fit_in_height = (lines < max_height);

	/* Calculate starting index. */
	int y = lines_fit_in_height ? (points->y2 - lines) : points->y1;

	for (int x = points->x1; written < buf_len; written++) {
		if (line >= input->start_y) {
			break;
		}

		widget_uc_sanitize(input->buf[written], &width);

		line += widget_adjust_xy(width, points, &x, &y);
	}

	int x = points->x1;

	tb_set_cursor(cur_x, lines_fit_in_height
						   ? (y + cur_line - 1)
						   : (points->y1 + (cur_line - (input->start_y + 1))));

	while (written < buf_len) {
		if (line >= lines || (y - input->start_y) >= points->y2) {
			break;
		}

		assert((widget_points_in_bounds(points, x, y - input->start_y)));

		uint32_t uc = widget_uc_sanitize(input->buf[written++], &width);

		/* Don't print newlines directly as they mess up the screen. */
		if (!widget_should_forcebreak(width)) {
			tb_set_cell(x, y - input->start_y, uc, TB_DEFAULT, input->bg);
		}

		line += widget_adjust_xy(width, points, &x, &y);
	}

	*rows = (lines_fit_in_height ? (line + 1) : max_height);
}

enum widget_error
input_handle_event(struct input *input, enum input_event event, ...) {
	if (!input) {
		return WIDGET_NOOP;
	}

	switch (event) {
	case INPUT_CLEAR:
		if ((arrlenu(input->buf)) == 0) {
			return WIDGET_NOOP;
		}

		input->cur_buf = 0;
		arrsetlen(input->buf, 0);
		return WIDGET_REDRAW;
	case INPUT_DELETE:
		return buf_del(input);
	case INPUT_DELETE_WORD:
		return buf_delword(input);
	case INPUT_RIGHT:
		return buf_right(input);
	case INPUT_RIGHT_WORD:
		return buf_rightword(input);
	case INPUT_LEFT:
		return buf_left(input);
	case INPUT_LEFT_WORD:
		return buf_leftword(input);
	case INPUT_ADD:
		{
			va_list vl = {0};
			va_start(vl, event);
			/* https://bugs.llvm.org/show_bug.cgi?id=41311
			 * NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
			uint32_t ch = va_arg(vl, uint32_t);
			va_end(vl);

			return buf_add(input, ch);
		}
	default:
		assert(0);
	}

	return WIDGET_NOOP;
}

char *
input_buf(struct input *input) {
	size_t len = arrlenu(input->buf);
	enum { max_codepoint_len = 6 };

	if (len == 0) {
		return NULL;
	}

	size_t size = 0;

	char tmp[max_codepoint_len];

	/* Calculate length. */
	for (size_t i = 0; i < len; i++) {
		size += (size_t) tb_utf8_unicode_to_char(tmp, input->buf[i]);
	}

	if (size == 0) {
		return NULL;
	}

	char *buf = malloc((size + 1) * sizeof(*buf));

	if (!buf) {
		return NULL;
	}

	for (size_t i = 0, i_utf8 = 0; i < size && i_utf8 < len; i_utf8++) {
		i += (size_t) tb_utf8_unicode_to_char(&buf[i], input->buf[i_utf8]);
	}

	/* Original size doesn't include + 1 for NUL so we don't need to subtract.
	 */
	buf[size] = '\0';

	return buf;
}

/* If node is the parent's last child. */
static bool
is_last(const struct treeview_node *node) {
	size_t len = node && node->parent ? arrlenu(node->parent->nodes) : 0;
	return (len > 0 && node == node->parent->nodes[len - 1]);
}

static struct treeview_node *
leaf(struct treeview_node *node) {
	if (node->is_expanded && (arrlenu(node->nodes)) > 0) {
		size_t len = arrlenu(node->nodes);
		return leaf(node->nodes[len > 0 ? len - 1 : 0]);
	}

	return node;
}

static struct treeview_node *
parent_next(struct treeview_node *node) {
	if (node->parent) {
		if ((node->parent->index + 1) < arrlenu(node->parent->nodes)) {
			return node->parent->nodes[++node->parent->index];
		}

		if (node->parent->parent) {
			return parent_next(node->parent);
		}
	}

	return node;
}

static int
node_height_up_to_bottom(struct treeview_node *node) {
	assert(node);

	int height = 1;

	if (!node->is_expanded) {
		return height;
	}

	for (size_t i = 0, len = arrlenu(node->nodes); i < len; i++) {
		height += node_height_up_to_bottom(node->nodes[i]);
	}

	return height;
}

static int
node_height_bottom_to_up(struct treeview_node *node) {
	assert(node);

	int height = 1;

	if (!node->parent) {
		return height;
	}

	assert(node->parent->is_expanded);

	for (size_t i = 0, len = arrlenu(node->parent->nodes); i < len; i++) {
		if (node->parent->nodes[i] == node) {
			break;
		}

		height += node_height_up_to_bottom(node->parent->nodes[i]);
	}

	return height + node_height_bottom_to_up(node->parent);
}

static int
redraw(struct treeview *treeview, const struct treeview_node *node,
  const struct widget_points *points, int x, int y) {
	if (!node) {
		return y;
	}

	assert((widget_points_in_bounds(points, x, y)));

	/* Stolen from tview's semigraphics. */
	const char symbol[] = "├──";
	const char symbol_end[] = "└──";
	const char symbol_continued[] = "│";
	const int gap_size = 3; /* Width of the above symbols (first 3). */

	bool is_end = is_last(node);
	bool is_not_top_level = (node->parent && node->parent->parent);
	int symbol_printed_width = (is_not_top_level ? gap_size : 0);

	/* Skip the given offset before actually printing stuff. */
	if (node->parent && treeview->skipped++ >= treeview->start_y) {
		if (is_not_top_level) {
			widget_print_str(x, y, points->x2, TB_DEFAULT, TB_DEFAULT,
			  (is_end ? symbol_end : symbol));
		}

		struct widget_points user_points = {0};
		widget_points_set(
		  &user_points, x + symbol_printed_width, points->x2, y, points->y2);

		node->draw_cb(node->data, &user_points, (node == treeview->selected));
		y++; /* Next node will be on another line. */
	}

	if (!node->is_expanded || (x + symbol_printed_width) >= points->x2) {
		return y;
	}

	for (size_t i = 0, len = arrlenu(node->nodes); i < len && y < points->y2;
		 i++) {
		int delta = redraw(treeview, node->nodes[i], points,
					  x + symbol_printed_width, y)
				  - y;

		/* We can cheat here and avoid backtracking to show the parent-child
		 * relation by just filling the gaps as we would if we inspected them
		 * ourselves. */
		if (is_not_top_level && !is_end) {
			for (int j = 0; j < delta; j++) {
				widget_print_str(
				  x, y++, points->x2, TB_DEFAULT, TB_DEFAULT, symbol_continued);
			}
		} else {
			y += delta;
		}
	}

	return y;
}

int
treeview_node_init(struct treeview_node *node, void *data,
  treeview_draw_cb draw_cb, treeview_free_cb free_cb) {
	if (!node || !draw_cb) {
		return -1;
	}

	*node = (struct treeview_node) {.is_expanded = true,
	  .data = data,
	  .draw_cb = draw_cb,
	  .free_cb = free_cb};

	return 0;
}

struct treeview_node *
treeview_node_alloc(
  void *data, treeview_draw_cb draw_cb, treeview_free_cb free_cb) {
	struct treeview_node *node = draw_cb ? malloc(sizeof(*node)) : NULL;

	if (node) {
		treeview_node_init(node, data, draw_cb, free_cb);
	}

	return node;
}

int
treeview_node_add_child(
  struct treeview_node *parent, struct treeview_node *child) {
	if (!parent || !child || parent == child) {
		return -1;
	}

	child->parent = parent;
	arrput(parent->nodes, child);

	return 0;
}

static void
node_children_destroy(struct treeview_node *node) {
	if (node->nodes) {
		for (size_t i = 0, len = arrlenu(node->nodes); i < len; i++) {
			treeview_node_destroy(node->nodes[i]);
		}

		arrfree(node->nodes);
	}
}

void
treeview_node_destroy(struct treeview_node *node) {
	if (!node) {
		return;
	}

	node_children_destroy(node);

	if (node->free_cb) {
		node->free_cb(node);
	}

	free(node);
}

int
treeview_init(struct treeview *treeview) {
	if (!treeview) {
		return -1;
	}

	*treeview = (struct treeview) {
	  .root = {
	  	.is_expanded = true,
	  },
	};

	return 0;
}

void
treeview_finish(struct treeview *treeview) {
	if (treeview) {
		node_children_destroy(&treeview->root);
		memset(treeview, 0, sizeof(*treeview));
	}
}

void
treeview_redraw(struct treeview *treeview, struct widget_points *points) {
	if (!treeview || !treeview->selected || !points) {
		return;
	}

	int selected_height = node_height_bottom_to_up(treeview->selected) - 1;
	assert(selected_height > 0);

	int diff_forward
	  = selected_height - (treeview->start_y + (points->y2 - points->y1));
	int diff_backward = treeview->start_y - (selected_height - 1);

	if (diff_backward > 0) {
		treeview->start_y -= diff_backward;
	} else if (diff_forward > 0) {
		treeview->start_y += diff_forward;
	}

	assert(treeview->start_y >= 0);
	assert(treeview->start_y < selected_height);

	redraw(treeview, &treeview->root, points, points->x1, points->y1);

	/* Reset the number of skipped lines. */
	treeview->skipped = 0;
}

enum widget_error
treeview_event(struct treeview *treeview, enum treeview_event event, ...) {
	if (!treeview) {
		return WIDGET_NOOP;
	}

	switch (event) {
	case TREEVIEW_EXPAND:
		if (!treeview->selected) {
			break;
		}

		treeview->selected->is_expanded = !treeview->selected->is_expanded;
		return WIDGET_REDRAW;
	case TREEVIEW_UP:
		if (!treeview->selected) {
			break;
		}

		if (treeview->selected->parent->index > 0) {
			treeview->selected = treeview->selected->parent
								   ->nodes[--treeview->selected->parent->index];
			treeview->selected = leaf(treeview->selected); /* Bottom node. */
		} else if (treeview->selected->parent->parent) {
			treeview->selected = treeview->selected->parent;
		} else if (treeview->selected == treeview->root.nodes[0]) {
			treeview->start_y = 0; /* Scroll up to the title if we're already at
								  the top-most node. */
		} else {
			break;
		}

		return WIDGET_REDRAW;
	case TREEVIEW_DOWN:
		if (!treeview->selected) {
			break;
		}

		if (treeview->selected->is_expanded
			&& (arrlenu(treeview->selected->nodes)) > 0) {
			treeview->selected = treeview->selected->nodes[0]; /* First node. */
		} else {
			/* Ensure that we don't create a loop between the end-most node of
			 * the tree and it's parent at the root. */
			if (treeview->selected != (leaf(&treeview->root))) {
				treeview->selected = parent_next(treeview->selected);
			}
		}

		return WIDGET_REDRAW;
	case TREEVIEW_INSERT:
		{
			if (!treeview->selected) {
				break;
			}

			va_list vl = {0};
			va_start(vl, event);
			/* https://bugs.llvm.org/show_bug.cgi?id=41311
			 * NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
			struct treeview_node *nnode = va_arg(vl, struct treeview_node *);
			va_end(vl);

			if (!nnode) {
				break;
			}

			nnode->parent = treeview->selected;
			arrput(treeview->selected->nodes, nnode);

			return WIDGET_REDRAW;
		}
	case TREEVIEW_INSERT_PARENT:
		{
			va_list vl = {0};
			va_start(vl, event);
			/* https://bugs.llvm.org/show_bug.cgi?id=41311
			 * NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized) */
			struct treeview_node *nnode = va_arg(vl, struct treeview_node *);
			va_end(vl);

			if (!nnode) {
				break;
			}

			nnode->parent = !treeview->selected ? &treeview->root
												: treeview->selected->parent;
			arrput(nnode->parent->nodes, nnode);

			/* We don't adjust indexes or set the selected tree unless it's the
			 * first entry. This is done to avoid accounting for the cases where
			 * we ascend to the top of a node, add a new node below it in the
			 * parent's trees and then try moving back to the node where all
			 * indices are set to 0. */
			if (!treeview->selected) {
				treeview->selected = nnode;
			}

			return WIDGET_REDRAW;
		}
	case TREEVIEW_DELETE:
		{
			struct treeview_node *current = treeview->selected;

			if (!current) {
				break;
			}

			arrdel(current->parent->nodes, current->parent->index);

			if (current->parent->index < arrlenu(current->parent->nodes)) {
				treeview->selected
				  = current->parent->nodes[current->parent->index];
			} else if (current->parent->index > 0) {
				treeview->selected
				  = current->parent->nodes[--current->parent->index];
			} else if (current->parent->parent) {
				/* Move up a level. */
				treeview->selected = current->parent;
			} else {
				/* At top level and all nodes deleted. */
				treeview->selected = NULL;
			}

			treeview_node_destroy(current);

			return WIDGET_REDRAW;
		}
	default:
		assert(0);
		break;
	}

	return WIDGET_NOOP;
}
#endif /* WIDGETS_IMPL */
