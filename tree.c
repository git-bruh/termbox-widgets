#include "stb_ds.h"
#include "widgets.h"

#include <assert.h>
#include <stdbool.h>

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

/* Get the height of the tree upto the givcn node.
 * This is very hacky but it's the only way to do this recursively. */
static int
node_height(struct treeview_node *parent, struct treeview_node *target,
  int height, int *realheight) {
	/* Break out of recursion if we found the target. */
	if (*realheight > 0) {
		return *realheight;
	}

	if (!parent) {
		return height;
	}

	height++;

	if (!parent->is_expanded) {
		return height;
	}

	for (size_t i = 0, len = arrlenu(parent->nodes); i < len; i++) {
		if (parent->nodes[i] == target) {
			*realheight = height + 1;
			return *realheight;
		}

		height = node_height(parent->nodes[i], target, height, realheight);
	}

	return *realheight > 0 ? *realheight : height;
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
		widget_points_set(&user_points, x + symbol_printed_width, points->x2, y, points->y2);

		node->draw_cb(node->data, &user_points, (node == treeview->selected));
		y++; /* Next node will be on another line. */
	}

	if (!node->is_expanded || (x + symbol_printed_width) >= points->x2) {
		return y;
	}

	for (size_t i = 0, len = arrlenu(node->nodes); i < len && y < points->y2;
		 i++) {
		int delta
		  = redraw(treeview, node->nodes[i], points, x + symbol_printed_width, y) - y;

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

struct treeview_node *
treeview_node_alloc(
  void *data, treeview_draw_cb draw_cb, treeview_free_cb free_cb) {
	struct treeview_node *node = draw_cb ? malloc(sizeof(*node)) : NULL;

	if (node) {
		*node = (struct treeview_node) {
		  .is_expanded = true,
		  .data = data,
		  .draw_cb = draw_cb,
		  .free_cb = free_cb,
		};
	}

	return node;
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

	{
		int tmp = 0;
		int old_selected_height
	  		= node_height(&treeview->root, treeview->selected, 0, &tmp) - 1;
		assert(selected_height == old_selected_height);
	}

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
