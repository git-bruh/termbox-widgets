# termbox-widgets

Simple reusable widgets for [termbox2](https://github.com/termbox/termbox2).

# Usage

Like termbox2, no build system is provided and the library is provided as a single header. Additionally, this library requires the `stb_ds.h` header file from [stb](https://github.com/nothings/stb) so that must be provided by your project, along with termbox.

In a single `.c` file, do the following to build the implementation:

```
#define WIDGETS_IMPL
#include "widgets.h"
```

`stb_ds.h` needs to be built in a similar manner in a _SEPARATE_ `.c` file with `#define STB_DS_IMPLEMENTATION`.

The API is defined in `widgets.h`. Each widget takes a `widget_points` structure containing the coordinates of the rectangle in which it can draw. This makes the library entirely agnostic to user-defined widgets as you only need to ensure that widgets don't overlap and are not forced into defining them in a specific manner like full-fledged UI toolkits do. However, some utility functions like `widget_print_str` and `widget_pad_center` are provided to optionally assist in writing user-defined widgets.

The widgets defined here depend on the user providing them events instead of using callbacks, which makes the widgets agnostic to key bindings etc. aswell.

Here is an example of a basic input field:

```c
#define TB_IMPL
#define WIDGETS_IMPL
#define STB_DS_IMPLEMENTATION
#define _GNU_SOURCE
#include "widgets.h"

int
main(void) {
	if ((tb_init()) != TB_OK) {
		return EXIT_FAILURE;
	}
	tb_set_input_mode(TB_INPUT_ALT);

	struct tb_event event;
	struct input input;

	if ((input_init(&input, TB_DEFAULT, false)) != 0) {
		return EXIT_FAILURE;
	}

	for (;;) {
		int tb_ret = tb_poll_event(&event);

		/* poll() can error out if SIGWINCH was received. */
		if (tb_ret != TB_OK && tb_ret != TB_ERR_POLL) {
			break;
		}

		/* Stop on Ctrl + C */
		if (event.key == TB_KEY_CTRL_C) {
			break;
		}

		enum widget_error ret = WIDGET_NOOP;

		if (!event.key && event.ch) {
			ret = input_handle_event(&input, INPUT_ADD, event.ch);
		} else if (event.type == TB_EVENT_KEY) {
			/* Shift + key should jump across a word. */
			bool mod = (event.mod & TB_MOD_SHIFT);

			switch (event.key) {
			case TB_KEY_ENTER:
				ret = input_handle_event(&input, INPUT_ADD, '\n');
				break;
			case TB_KEY_BACKSPACE:
			case TB_KEY_BACKSPACE2:
				ret = input_handle_event(
				  &input, mod ? INPUT_DELETE_WORD : INPUT_DELETE);
				break;
			case TB_KEY_ARROW_RIGHT:
				ret = input_handle_event(
				  &input, mod ? INPUT_RIGHT_WORD : INPUT_RIGHT);
				break;
			case TB_KEY_ARROW_LEFT:
				ret = input_handle_event(
				  &input, mod ? INPUT_LEFT_WORD : INPUT_LEFT);
				break;
			}
		}

		/* No operation was valid. */
		if (ret != WIDGET_REDRAW && event.type != TB_EVENT_RESIZE) {
			continue;
		}

		int rows = 0; /* Rows taken by current input field after redrawing. */
		struct widget_points points = {0};
		int height = tb_height();
		const int input_max_height = 5;

		/* x1, x2, y1, y2 */
		widget_points_set(
		  &points, 0, tb_width(), height - input_max_height, height);

		tb_clear();
		input_redraw(&input, &points, &rows);
		/* Possibly more widget redraws here
		 * ... */
		tb_present();
	}

	input_finish(&input);
	tb_shutdown();
}
```
