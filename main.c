#include <gst/gst.h>
#include <gst/video/video.h>

#define NB_BUFFER_DISCARD 20

GST_DEBUG_CATEGORY (launch_drop_debug);
#define GST_CAT_DEFAULT launch_drop_debug

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
  if (count <= NB_BUFFER_DISCARD) {
    GST_LOG ("Buffer %u/%u produced by encoder, discard", count,
        NB_BUFFER_DISCARD);

    if (count == NB_BUFFER_DISCARD) {
      GstEvent *event;

      GST_LOG ("All buffers have been discarded, request a keyframe");

      event =
          gst_video_event_new_upstream_force_key_unit (GST_CLOCK_TIME_NONE,
          TRUE, 1);
      g_assert (gst_pad_send_event (pad, event));
    }
    return GST_PAD_PROBE_DROP;
  }

  return GST_PAD_PROBE_OK;
}

static GstElement *
create_pipeline (const gchar ** pipeline_desc)
{
  g_autoptr (GstElement) pipeline = NULL, encoder = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GstPad) pad = NULL;

  pipeline = gst_parse_launchv (pipeline_desc, &error);
  if (!pipeline) {
    GST_ERROR ("Failed to create pipeline: %s", error->message);
    return NULL;
  }

  encoder = gst_bin_get_by_name (GST_BIN (pipeline), "encoder");
  g_assert (encoder);

  pad = gst_element_get_static_pad (encoder, "src");
  g_assert (pad);

  gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BUFFER, encoder_buffer_probe_cb,
      NULL, NULL);

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
    default:
      break;
  }
  return TRUE;
}

static void
run (const gchar ** pipeline_desc)
{
  g_autoptr (GstElement) pipeline = NULL;
  g_autoptr (GstBus) bus = NULL;
  g_autoptr (GMainLoop) loop = NULL;

  pipeline = create_pipeline (pipeline_desc);
  g_assert (pipeline);

  loop = g_main_loop_new (NULL, FALSE);
  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  gst_bus_add_watch (bus, (GstBusFunc) bus_message, loop);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (bus);
}

int
main (int argc, char **argv)
{
  g_autofree gchar **argvn = NULL;

  gst_init (&argc, &argv);

  GST_DEBUG_CATEGORY_INIT (launch_drop_debug, "launch-drop", 0,
      "gst-launch-drop tool category");

  /* make a null-terminated version of argv */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));

  run ((const gchar **) argvn);

  gst_deinit ();
  return 0;
}
