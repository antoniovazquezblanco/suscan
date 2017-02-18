/*

  Copyright (C) 2017 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <libgen.h>
#include <ctk.h>
#include "suscan.h"

PTR_LIST_EXTERN(struct suscan_source, source); /* Declared in source.c */

struct suscan_source_widget_set;

struct suscan_source_dialog {
  ctk_widget_t *window;
  ctk_widget_t *button;
  ctk_widget_t *selbutton;
  ctk_widget_t *menu;

  struct suscan_source_widget_set *current;

  PTR_LIST(struct suscan_source_widget_set, widget_set);
  SUBOOL        exit_flag;
};

struct suscan_source_widget_set {
  struct suscan_source_dialog *dialog;
  struct suscan_source_config *config;

  /* Widget controls */
  PTR_LIST(ctk_widget_t, widget);
};


#define suscan_source_dialog_INITIALIZER        \
{                                               \
  NULL, /* window */                            \
  NULL, /* button */                            \
  NULL, /* selbutton */                         \
  NULL, /* menu */                              \
  NULL, /* current */                           \
  NULL, /* widget_set_list */                   \
  0,    /* widget_set_count */                  \
  SU_FALSE, /* exit_flag */                     \
}

/*********************** Source Widgets API **********************************/
void
suscan_source_widget_set_destroy(struct suscan_source_widget_set *widgets)
{
  unsigned int i;

  if (widgets->config != NULL)
    suscan_source_config_destroy(widgets->config);

  for (i = 0; i < widgets->widget_count; ++i)
    if (widgets->widget_list[i] != NULL)
      ctk_widget_destroy(widgets->widget_list[i]);

  if (widgets->widget_list != NULL)
    free(widgets->widget_list);

  free(widgets);
}

void
suscan_source_widget_set_show(struct suscan_source_widget_set *set)
{
  unsigned int i;

  for (i = 0; i < set->widget_count; ++i)
    if (set->widget_list[i] != NULL) {
      mvwaddstr(
          set->dialog->window->c_window,
          2 * i + SUSCAN_SOURCE_DIALOG_FIELD_Y_OFFSET,
          2,
          set->config->source->field_list[i]->desc);
      (void) ctk_widget_show(set->widget_list[i]);
    }
}

void
suscan_source_widget_set_hide(struct suscan_source_widget_set *set)
{
  unsigned int i, j;

  for (i = 0; i < set->widget_count; ++i)
    if (set->widget_list[i] != NULL) {
      /* Clear description */
      for (j = 0; j < strlen(set->config->source->field_list[i]->desc); ++j)
        mvwaddch(
            set->dialog->window->c_window,
            2 * i + SUSCAN_SOURCE_DIALOG_FIELD_Y_OFFSET,
            2 + j,
            ' ');
      (void) ctk_widget_hide(set->widget_list[i]);
    }
}

SUPRIVATE void
suscan_dialog_file_on_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  char *result = NULL;
  char *base;
  struct suscan_source_widget_set *set =
        (struct suscan_source_widget_set *) ctk_widget_get_private(widget);
  enum ctk_dialog_response response;

  if ((response = ctk_file_dialog("Open file...", &result))
      == CTK_DIALOG_RESPONSE_ERROR)
    ctk_msgbox(CTK_DIALOG_ERROR, "SUScan", "Failed to open dialog");

  if (response == CTK_DIALOG_RESPONSE_OK) {
    base = basename(result);
    if (strlen(base) > SUSCAN_SOURCE_DIALOG_MAX_BASENAME)
      strncpy(base + SUSCAN_SOURCE_DIALOG_MAX_BASENAME - 3, "...", 4);
    ctk_button_set_caption(widget, base);
    free(result);
  }
}

struct suscan_source_widget_set *
suscan_source_widget_set_new(
    struct suscan_source_dialog *dialog,
    const struct suscan_source *source)
{
  struct suscan_source_widget_set *new = NULL;
  struct ctk_widget_handlers hnd;
  ctk_widget_t *widget = NULL;
  unsigned int width = 0;
  unsigned int height = 0;
  unsigned int i = 0;
  unsigned int widget_y = 0;
  unsigned int widget_x = 0;

  if ((new = calloc(1, sizeof(struct suscan_source_widget_set))) == NULL)
    goto fail;

  new->dialog = dialog;

  if ((new->config = suscan_source_config_new(source)) == NULL)
    goto fail;

  height = SUSCAN_SOURCE_DIALOG_Y_PADDING + new->config->source->field_count;

  /* Make room for all widgets */
  if (height > dialog->window->height)
    if (!ctk_widget_resize(
        dialog->window,
        dialog->window->width,
        height))
      goto fail;

  /* Create all widgets and pray */
  for (i = 0; i < new->config->source->field_count; ++i) {
    if (new->config->source->field_list[i] != NULL) {
      width =
          strlen(new->config->source->field_list[i]->desc)
          + SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH
          + SUSCAN_SOURCE_DIALOG_X_PADDING;

      /* Make room for specially wide field */
      if (width > dialog->window->width)
        if (!ctk_widget_resize(dialog->window, width, height))
          goto fail;

      widget_x = strlen(new->config->source->field_list[i]->desc) + 3;
      widget_y = 2 * i + SUSCAN_SOURCE_DIALOG_FIELD_Y_OFFSET;

      /* Add widget */
      switch (new->config->source->field_list[i]->type) {
        case SUSCAN_FIELD_TYPE_STRING:
          widget = ctk_entry_new(
              dialog->window,
              widget_x,
              widget_y,
              SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH);
          break;

        case SUSCAN_FIELD_TYPE_INTEGER:
          if ((widget = ctk_entry_new(
              dialog->window,
              widget_x,
              widget_y,
              SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH)) != NULL)
            /* Use a 32 bit limit to avoid HUGE sampling frequencies */
            ctk_entry_set_validator(widget, ctk_entry_uint32_validator);
          break;

        case SUSCAN_FIELD_TYPE_FLOAT:
          if ((widget = ctk_entry_new(
              dialog->window,
              widget_x,
              widget_y,
              SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH)) != NULL)
            ctk_entry_set_validator(widget, ctk_entry_float_validator);
          break;

        case SUSCAN_FIELD_TYPE_FILE:
          if ((widget = ctk_button_new(
              dialog->window,
              widget_x,
              widget_y,
              "Browse...")) != NULL) {
            ctk_widget_get_handlers(widget, &hnd);
            hnd.submit_handler = suscan_dialog_file_on_submit;
            ctk_widget_set_handlers(widget, &hnd);
          }
          break;

        default:
          /* Unknown field type */
          ctk_msgbox(CTK_DIALOG_ERROR, "Source dialog", "Invalid field type");

          goto fail;
      }

      if (widget == NULL)
        goto fail;

      ctk_widget_set_private(widget, new);

      /* Indexes must be the same */
      if (PTR_LIST_APPEND_CHECK(new->widget, widget) != i) {
        ctk_widget_destroy(widget);
        goto fail;
      }
    }
  }

  return new;

fail:
  if (new != NULL)
    suscan_source_widget_set_destroy(new);

  return NULL;
}

SUPRIVATE void
suscan_dialog_switch_widget_set(
    struct suscan_source_dialog *dialog,
    struct suscan_source_widget_set *set) {

  if (dialog->current != set) {
    if (dialog->current != NULL)
      suscan_source_widget_set_hide(dialog->current);

    suscan_source_widget_set_show(set);

    dialog->current = set;
  }
}

SUPRIVATE void
suscan_dialog_source_on_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  struct suscan_source_dialog *dialog =
      (struct suscan_source_dialog *) ctk_selbutton_get_private(widget);

  suscan_dialog_switch_widget_set(dialog, item->private);
}

SUPRIVATE void
suscan_dialog_on_submit(ctk_widget_t *widget, struct ctk_item *item)
{
  struct suscan_source_dialog *dialog =
      (struct suscan_source_dialog *) ctk_widget_get_private(widget);

  dialog->exit_flag = SU_TRUE;
}

SUPRIVATE void
suscan_source_dialog_finalize(struct suscan_source_dialog *dialog)
{
  unsigned int i;

  for (i = 0; i < dialog->widget_set_count; ++i)
    if (dialog->widget_set_list[i] != NULL)
      suscan_source_widget_set_destroy(dialog->widget_set_list[i]);

  if (dialog->widget_set_list != NULL)
    free(dialog->widget_set_list);

  if (dialog->button != NULL)
    ctk_widget_destroy(dialog->button);

  if (dialog->menu != NULL)
    ctk_widget_destroy(dialog->menu);

  if (dialog->selbutton != NULL)
    ctk_widget_destroy(dialog->selbutton);

  if (dialog->window != NULL)
    ctk_widget_destroy(dialog->window);
}

SUPRIVATE void
suscan_widget_arrange_right(
    struct suscan_source_dialog *dialog,
    ctk_widget_t *widget)
{
  ctk_widget_move(
      widget,
      dialog->window->width - SUSCAN_SOURCE_DIALOG_MAX_WIDGET_WIDTH - 2,
      widget->y);
}

SUPRIVATE void
suscan_rearrange_widgets(struct suscan_source_dialog *dialog)
{
  unsigned int i, j;

  for (i = 0; i < dialog->widget_set_count; ++i)
    if (dialog->widget_set_list[i] != NULL)
      for (j = 0; j < dialog->widget_set_list[i]->widget_count; ++j)
        if (dialog->widget_set_list[i]->widget_list[j] != NULL)
          suscan_widget_arrange_right(
              dialog,
              dialog->widget_set_list[i]->widget_list[j]);
}

SUPRIVATE SUBOOL
suscan_source_dialog_init(struct suscan_source_dialog *dialog)
{
  struct ctk_widget_handlers hnd;
  struct suscan_source_widget_set *set;
  struct ctk_item *item;
  unsigned int button_width;
  unsigned int i;

  /* Create Dialog Window */
  if ((dialog->window = ctk_window_new("Open source")) == NULL)
    return SU_FALSE;

  ctk_widget_resize(dialog->window, 33, 15);
  ctk_widget_center(dialog->window);
  ctk_widget_set_shadow(dialog->window, CTK_TRUE);

  /* Create source menu list */
  if ((dialog->menu = ctk_menu_new(NULL, 0, 0)) == NULL)
    return SU_FALSE;

  /*
   * Create widget sets associated to sources and
   * add menu entries accordingly
   * */
  for (i = 0; i < source_count; ++i) {
    if ((set = suscan_source_widget_set_new(dialog, source_list[i])) == NULL)
      return SU_FALSE;

    if (!ctk_menu_add_item(
        dialog->menu,
        source_list[i]->name,
        source_list[i]->desc,
        set)) {
      suscan_source_widget_set_destroy(set);
      return SU_FALSE;
    }

    if (i == 0) {
      if ((dialog->selbutton
          = ctk_selbutton_new(dialog->window, 15, 2, dialog->menu)) == NULL)
        return SU_FALSE;

      ctk_widget_set_attrs(dialog->selbutton, COLOR_PAIR(CTK_CP_TEXTAREA));
      ctk_selbutton_set_private(dialog->selbutton, dialog);
    }

    if (PTR_LIST_APPEND_CHECK(dialog->widget_set, set) == -1) {
      suscan_source_widget_set_destroy(set);
      return SU_FALSE;
    }
  }

  /* Create source selection button */
  mvwaddstr(dialog->window->c_window, 2, 2, "Source type:");

  /* Okay button */
  button_width = 10;

  if ((dialog->button = ctk_button_new(
      dialog->window,
      dialog->window->width / 2 - button_width / 2,
      dialog->window->height - 3,
      "OK")) == NULL)
    return SU_FALSE;
  ctk_widget_set_attrs(dialog->button, COLOR_PAIR(CTK_CP_TEXTAREA));
  ctk_widget_set_private(dialog->button, dialog);

  ctk_widget_get_handlers(dialog->button, &hnd);
  hnd.submit_handler = suscan_dialog_on_submit;
  ctk_widget_set_handlers(dialog->button, &hnd);

  ctk_selbutton_set_on_submit(
      dialog->selbutton,
      suscan_dialog_source_on_submit);

  /* Rearrange all widgets to left */
  suscan_rearrange_widgets(dialog);
  suscan_widget_arrange_right(dialog, dialog->selbutton);

  ctk_widget_show(dialog->selbutton);
  ctk_widget_show(dialog->button);
  ctk_widget_show(dialog->window);

  /* Show first non-null source */
  if ((item = ctk_menu_get_item_at(dialog->menu, 1)) == NULL)
    if ((item = ctk_menu_get_first_item(dialog->menu)) == NULL)
      return SU_FALSE;

  ctk_selbutton_set_current_item(dialog->selbutton, item);

  ctk_window_focus_next(dialog->window);

  ctk_update();

  return SU_TRUE;
}

SUBOOL
suscan_open_source_dialog(void)
{
  struct suscan_source_dialog dialog = suscan_source_dialog_INITIALIZER;
  int c;
  SUBOOL ok = SU_FALSE;

  if (source_count == 0) {
    ctk_msgbox(CTK_DIALOG_ERROR, "Open source", "No signal sources available");
    return SU_TRUE;
  }

  if (!suscan_source_dialog_init(&dialog))
    goto done;

  while (!dialog.exit_flag) {
    c = getch();
    if (c == 'q')
      break;

    ctk_widget_notify_kbd(dialog.window, c);
    ctk_update();
  }

  ctk_widget_hide(dialog.window);

  ok = SU_TRUE;

done:
  suscan_source_dialog_finalize(&dialog);

  ctk_update();

  return ok;
}
