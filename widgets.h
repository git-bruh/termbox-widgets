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
typedef void (*treeview_draw_cb)(void *data, struct widget_points *points, bool is_selected);
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
treeview_node_init(struct treeview_node *node, void *data, treeview_draw_cb draw_cb,
treeview_free_cb free_cb);
void
treeview_node_destroy(struct treeview_node *node);
int
treeview_node_add_child(struct treeview_node *parent, struct treeview_node *child);
int
treeview_init(struct treeview *treeview);
void
treeview_finish(struct treeview *treeview);
void
treeview_redraw(struct treeview *treeview, struct widget_points *points);
enum widget_error
treeview_event(struct treeview *treeview, enum treeview_event event, ...);
#endif /* !WIDGETS_H */
