#include "stb_ds.h"
#include "widgets.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

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

	/* Original size doesn't include + 1 for NUL so we don't need to subtract. */
	buf[size] = '\0';

	return buf;
}
