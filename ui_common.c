#include "widgets.h"
#include <math.h>

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
		if (tmp_width <= 0 || tmp_width > WIDGET_CH_MAX) {
			*width = 1;
			return '?';
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
		return x;
	}

	uint32_t uc = 0;
	int width = 0;
	int original = x;

	while (*str) {
		str += tb_utf8_char_to_unicode(&uc, str);
		uc = widget_uc_sanitize(uc, &width);
		tb_set_cell(x, y, uc, fg, bg);
		x += width;

		if ((widget_should_scroll(x, WIDGET_CH_MAX, max_x))) {
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
