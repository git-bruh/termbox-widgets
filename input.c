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
input_init(struct input *input) {
	if (!input) {
		return -1;
	}

	*input = (struct input) {0};

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
	if (!input || !points) {
		return;
	}

	size_t buf_len = arrlenu(input->buf);

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
			tb_set_cell(x, y - input->start_y, uc, TB_DEFAULT, TB_DEFAULT);
		}

		line += widget_adjust_xy(width, points, &x, &y);
	}

	*rows = (lines_fit_in_height ? (line + 1) : max_height);
}

enum widget_error
input_handle_event(struct input *input, enum input_event event, ...) {
	switch (event) {
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
	const size_t max_codepoint_len = 7;
	char *buf = NULL;

	size_t size
	  = ((max_codepoint_len * arrlenu(input->buf)) + 1) * sizeof(*buf);

	if ((arrlenu(input->buf)) == 0 || !(buf = malloc(size))) {
		return NULL;
	}

	size_t buf_index = 0;

	for (size_t i = 0, len = arrlenu(input->buf); i < len; i++) {
		buf_index
		  += (size_t) tb_utf8_unicode_to_char(&buf[buf_index], input->buf[i]);
		assert(buf_index < size);
	}

	buf[buf_index] = '\0';

	return buf;
}
