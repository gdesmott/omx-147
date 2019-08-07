/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000 Wim Taymans <wtay@chello.be>
 *               2004 Thomas Vander Stichele <thomas@apestaart.org>
 *
 * Some part of the code have been copied from gst-launch.c from GStreamer.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include <string.h>

#include <gst/gst.h>
#include <gst/video/video.h>

GST_DEBUG_CATEGORY (launch_drop_debug);
#define GST_CAT_DEFAULT launch_drop_debug

static gchar *drop_element = NULL;
static guint nb_buffer_discard = 20;
static guint nb_buffer_allowed = 0;
static gboolean request_key_frame = FALSE;
static gboolean verbose = FALSE;

static GOptionEntry entries[] = {
  {"element", 'e', 0, G_OPTION_ARG_STRING, &drop_element,
      "Name of the element whose output should be dropped", NULL},
  {"drop-buffers", 'n', 0, G_OPTION_ARG_INT, &nb_buffer_discard,
      "Number of buffers to drop (default: 20)", NULL},
  {"allow-buffers", 'a', 0, G_OPTION_ARG_INT, &nb_buffer_allowed,
      "Number of buffers to allow before starting to drop (default: 0)", NULL},
  {"key-frame", 'k', 0, G_OPTION_ARG_NONE, &request_key_frame,
      "Request a key frame when done dropping", NULL},
  {"verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
      "Output status information and property notifications", NULL},
  {NULL}
};

static GstPadProbeReturn
encoder_buffer_probe_cb (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  static guint count = 0;
  GstBuffer *buffer;

  buffer = gst_pad_probe_info_get_buffer (info);
  g_assert (buffer);

  GST_LOG ("Received buffer pts %" GST_TIME_FORMAT " delta: %d header: %d",
      GST_TIME_ARGS (GST_BUFFER_PTS (buffer)),
      gst_buffer_has_flags (buffer, GST_BUFFER_FLAG_DELTA_UNIT),
      gst_buffer_has_flags (buffer, GST_BUFFER_FLAG_HEADER));

  count++;

  if (count <= nb_buffer_allowed) {
    return GST_PAD_PROBE_OK;
  }

  if (count <= nb_buffer_discard + nb_buffer_allowed) {
    GST_LOG ("Buffer %u/%u produced by encoder, discard", count,
        nb_buffer_discard);

    if (count == nb_buffer_discard + nb_buffer_allowed) {
      g_print ("All buffers have been dropped\n");

      if (request_key_frame) {
        GstEvent *event;

        g_print ("Request key frame\n");

        event =
            gst_video_event_new_upstream_force_key_unit (GST_CLOCK_TIME_NONE,
            TRUE, 1);
        g_assert (gst_pad_send_event (pad, event));
      }
    }
    return GST_PAD_PROBE_DROP;
  }

  return GST_PAD_PROBE_OK;
}

static GstElement *
create_pipeline (const gchar ** pipeline_desc)
{
  g_autoptr (GstElement) pipeline = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GstPad) pad = NULL;

  pipeline = gst_parse_launchv (pipeline_desc, &error);
  if (!pipeline) {
    GST_ERROR ("Failed to create pipeline: %s", error->message);
    return NULL;
  }

  if (drop_element) {
    g_autoptr (GstElement) element = NULL;

    element = gst_bin_get_by_name (GST_BIN (pipeline), drop_element);
    if (!element) {
      g_printerr ("Did not find element '%s'", drop_element);
      return NULL;
    }

    pad = gst_element_get_static_pad (element, "src");
    g_assert (pad);

    g_print ("Add drop probe on element '%s'. Drop %d buffers\n", drop_element,
        nb_buffer_discard);
    gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, encoder_buffer_probe_cb,
        NULL, NULL);
  }

  return g_steal_pointer (&pipeline);
}

static gboolean
bus_message (GstBus * bus, GstMessage * msg, GMainLoop * loop)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_ERROR:
    {
      g_autoptr (GError) error = NULL;
      g_autofree gchar *dbg_info = NULL;

      gst_message_parse_error (msg, &error, &dbg_info);
      g_printerr ("ERROR from element %s: %s\n",
          GST_OBJECT_NAME (msg->src), error->message);
      g_printerr ("Debugging info: %s\n", (dbg_info) ? dbg_info : "none");

      g_main_loop_quit (loop);
    }
      break;
    case GST_MESSAGE_EOS:
      g_print ("eos\n");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_PROPERTY_NOTIFY:{
      const GValue *val;
      const gchar *name;
      GstObject *obj;
      gchar *val_str = NULL;
      gchar *obj_name;

      if (!verbose)
        break;

      gst_message_parse_property_notify (msg, &obj, &name, &val);

      obj_name = gst_object_get_path_string (GST_OBJECT (obj));
      if (val != NULL) {
        if (G_VALUE_HOLDS_STRING (val))
          val_str = g_value_dup_string (val);
        else if (G_VALUE_TYPE (val) == GST_TYPE_CAPS)
          val_str = gst_caps_to_string (g_value_get_boxed (val));
        else if (G_VALUE_TYPE (val) == GST_TYPE_TAG_LIST)
          val_str = gst_tag_list_to_string (g_value_get_boxed (val));
        else if (G_VALUE_TYPE (val) == GST_TYPE_STRUCTURE)
          val_str = gst_structure_to_string (g_value_get_boxed (val));
        else
          val_str = gst_value_serialize (val);
      } else {
        val_str = g_strdup ("(no value)");
      }

      g_print ("%s: %s = %s\n", obj_name, name, val_str);
      g_free (obj_name);
      g_free (val_str);
      break;
    }
    default:
      break;
  }
  return TRUE;
}

static gboolean
run (const gchar ** pipeline_desc)
{
  g_autoptr (GstElement) pipeline = NULL;
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GMainLoop) loop = NULL;
  gulong deep_notify_id = 0;

  pipeline = create_pipeline (pipeline_desc);
  if (!pipeline)
    return FALSE;

  if (verbose)
    deep_notify_id =
        gst_element_add_property_deep_notify_watch (pipeline, NULL, TRUE);

  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  gst_bus_add_watch (bus, (GstBusFunc) bus_message, loop);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  /* No need to see all those pad caps going to NULL etc., it's just noise */
  if (deep_notify_id != 0)
    g_signal_handler_disconnect (pipeline, deep_notify_id);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (bus);

  return TRUE;
}

int
main (int argc, char **argv)
{
  g_autofree gchar **argvn = NULL;
  g_autoptr (GOptionContext) ctx = NULL;
  g_autoptr (GError) error = NULL;
  gboolean result;

  GST_DEBUG_CATEGORY_INIT (launch_drop_debug, "launch-drop", 0,
      "gst-launch-drop tool category");

  ctx = g_option_context_new ("PIPELINE-DESCRIPTION");
  g_option_context_add_main_entries (ctx, entries, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_printerr ("Failed to parse arguments: %s", error->message);
    return 1;
  }

  /* make a null-terminated version of argv */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));

  result = run ((const gchar **) argvn);

  g_free (drop_element);
  gst_deinit ();
  return result ? 1 : -1;
}
