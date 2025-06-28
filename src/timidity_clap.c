// CLAP wrapper for Timidity
// Based on CLAP plugin template
// https://github.com/free-audio/clap/blob/main/src/plugin-template.c

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#if __STDC_VERSION__ >= 201112L && !defined (__STDC_NO_THREADS__) && defined (CLAP_HAS_THREADS_H)
#   define CLAP_HAS_THREAD
#   include <threads.h>
#endif

#include <clap/clap.h>

#include "../timidity/timid.h"

static const clap_plugin_descriptor_t s_my_plug_desc = {
   .clap_version = CLAP_VERSION_INIT,
   .id = "com.Datajake.Timidity",
   .name = "Timidity",
   .vendor = "Datajake",
   .url = "",
   .manual_url = "",
   .support_url = "",
   .version = "1.0.0",
   .description = "CLAP wrapper for Timidity.",
   .features = (const char *[]){CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_STEREO, CLAP_PLUGIN_FEATURE_SYNTHESIZER, NULL},
};

typedef struct {
   clap_plugin_t                   plugin;
   const clap_host_t              *host;
   const clap_host_latency_t      *host_latency;
   const clap_host_log_t          *host_log;
   const clap_host_thread_check_t *host_thread_check;
   const clap_host_state_t        *host_state;
   const clap_host_params_t       *host_params;

   uint32_t latency;

   Timid synth;
   double *buffer;
   uint32_t buffer_size;
} my_plug_t;

/////////////////////////////
// clap_plugin_audio_ports //
/////////////////////////////

static uint32_t my_plug_audio_ports_count(const clap_plugin_t *plugin, bool is_input) {
   // We just declare 1 audio input and 1 audio output
   return 1;
}

static bool my_plug_audio_ports_get(const clap_plugin_t    *plugin,
                                    uint32_t                index,
                                    bool                    is_input,
                                    clap_audio_port_info_t *info) {
   if (index > 0)
      return false;
   info->id = 0;
   snprintf(info->name, sizeof(info->name), "%s", "Audio Port");
   info->channel_count = 2;
   info->flags = CLAP_AUDIO_PORT_IS_MAIN | CLAP_AUDIO_PORT_SUPPORTS_64BITS | CLAP_AUDIO_PORT_PREFERS_64BITS;
   info->port_type = CLAP_PORT_STEREO;
   info->in_place_pair = CLAP_INVALID_ID;
   return true;
}

static const clap_plugin_audio_ports_t s_my_plug_audio_ports = {
   .count = my_plug_audio_ports_count,
   .get = my_plug_audio_ports_get,
};

////////////////////////////
// clap_plugin_note_ports //
////////////////////////////

static uint32_t my_plug_note_ports_count(const clap_plugin_t *plugin, bool is_input) {
   // We just declare 1 note input
   return 1;
}

static bool my_plug_note_ports_get(const clap_plugin_t   *plugin,
                                   uint32_t               index,
                                   bool                   is_input,
                                   clap_note_port_info_t *info) {
   if (index > 0)
      return false;
   info->id = 0;
   snprintf(info->name, sizeof(info->name), "%s", "Note Port");
   info->supported_dialects =
      CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
   info->preferred_dialect = CLAP_NOTE_DIALECT_MIDI;
   return true;
}

static const clap_plugin_note_ports_t s_my_plug_note_ports = {
   .count = my_plug_note_ports_count,
   .get = my_plug_note_ports_get,
};

//////////////////
// clap_latency //
//////////////////

uint32_t my_plug_latency_get(const clap_plugin_t *plugin) {
   my_plug_t *plug = plugin->plugin_data;
   return plug->latency;
}

static const clap_plugin_latency_t s_my_plug_latency = {
   .get = my_plug_latency_get,
};

/////////////////
// clap_params //
/////////////////

enum {
   param_amplification = 0,
   param_max_voices,
   param_immediate_panning,
   param_mono,
   param_fast_decay,
   param_antialiasing,
   param_control_rate,
   num_params
};

uint32_t my_plug_param_count(const clap_plugin_t *plugin) {
   return num_params;
}

bool my_plug_param_get_info(const clap_plugin_t *plugin, uint32_t index, clap_param_info_t *info) {
   my_plug_t *plug = plugin->plugin_data;
   info->id = index;
   info->flags = CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_ENUM;
   info->cookie = NULL;
   switch (index) {
   case param_amplification:
      snprintf(info->name, CLAP_NAME_SIZE, "%s", "Amplification");
      info->min_value = 0;
      info->max_value = MAX_AMPLIFICATION;
      info->default_value = DEFAULT_AMPLIFICATION;
      return true;
   case param_max_voices:
      snprintf(info->name, CLAP_NAME_SIZE, "%s", "MaxVoices");
      info->min_value = 1;
      info->max_value = MAX_VOICES;
      info->default_value = DEFAULT_VOICES;
      return true;
   case param_immediate_panning:
      snprintf(info->name, CLAP_NAME_SIZE, "%s", "ImmediatePanning");
      info->min_value = 0;
      info->max_value = 1;
      info->default_value = 1;
      return true;
   case param_mono:
      snprintf(info->name, CLAP_NAME_SIZE, "%s", "Mono");
      info->min_value = 0;
      info->max_value = 1;
      info->default_value = 0;
      return true;
   case param_fast_decay:
      snprintf(info->name, CLAP_NAME_SIZE, "%s", "FastDecay");
      info->min_value = 0;
      info->max_value = 1;
      info->default_value = 1;
      return true;
   case param_antialiasing:
      snprintf(info->name, CLAP_NAME_SIZE, "%s", "Antialiasing");
      info->min_value = 0;
      info->max_value = 1;
      info->default_value = 1;
      return true;
   case param_control_rate:
      snprintf(info->name, CLAP_NAME_SIZE, "%s", "ControlRate");
      info->min_value = timid_get_sample_rate(&plug->synth)/MAX_CONTROL_RATIO;
      info->max_value = timid_get_sample_rate(&plug->synth);
      info->default_value = CONTROLS_PER_SECOND;
      return true;
   }
   return false;
}

void my_plug_param_set_value(my_plug_t *plug, clap_id id, double value) {
   switch (id) {
   case param_amplification:
      timid_set_amplification(&plug->synth, (int)value);
      break;
   case param_max_voices:
      timid_set_max_voices(&plug->synth, (int)value);
      break;
   case param_immediate_panning:
      timid_set_immediate_panning(&plug->synth, (int)value);
      break;
   case param_mono:
      timid_set_mono(&plug->synth, (int)value);
      break;
   case param_fast_decay:
      timid_set_fast_decay(&plug->synth, (int)value);
      break;
   case param_antialiasing:
      timid_set_antialiasing(&plug->synth, (int)value);
      break;
   case param_control_rate:
      timid_set_control_rate(&plug->synth, (int)value);
      break;
   }
}

bool my_plug_param_get_value(const clap_plugin_t *plugin, clap_id id, double *value) {
   my_plug_t *plug = plugin->plugin_data;
   switch (id) {
   case param_amplification:
      *value = timid_get_amplification(&plug->synth);
      return true;
   case param_max_voices:
      *value = timid_get_max_voices(&plug->synth);
      return true;
   case param_immediate_panning:
      *value = timid_get_immediate_panning(&plug->synth);
      return true;
   case param_mono:
      *value = timid_get_mono(&plug->synth);
      return true;
   case param_fast_decay:
      *value = timid_get_fast_decay(&plug->synth);
      return true;
   case param_antialiasing:
      *value = timid_get_antialiasing(&plug->synth);
      return true;
   case param_control_rate:
      *value = timid_get_control_rate(&plug->synth);
      return true;
   }
   return false;
}

bool my_plug_param_value_to_text(const clap_plugin_t *plugin, clap_id id, double value, char *buffer, uint32_t size) {
   switch (id) {
   case param_amplification:
   case param_max_voices:
   case param_control_rate:
      snprintf(buffer, size, "%d", (uint32_t)value);
      return true;
   case param_immediate_panning:
   case param_mono:
   case param_fast_decay:
   case param_antialiasing:
      if (value)
         snprintf(buffer, size, "%s", "ON");
      else
         snprintf(buffer, size, "%s", "OFF");
      return true;
   }
   return false;
}

bool my_plug_param_text_to_value(const clap_plugin_t *plugin, clap_id id, const char *text, double *value) {
   switch (id) {
   case param_amplification:
   case param_max_voices:
   case param_control_rate:
      *value = atof(text);
      return true;
   }
   return false;
}

void my_plug_param_flush(const clap_plugin_t *plugin, const clap_input_events_t *in, const clap_output_events_t *out) {
   my_plug_t *plug = plugin->plugin_data;
   const uint32_t nev = in->size(in);
   for (uint32_t i = 0; i < nev; ++i) {
      const clap_event_header_t *hdr = in->get(in, i);
      if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_PARAM_VALUE) {
         const clap_event_param_value_t *ev = (const clap_event_param_value_t *)hdr;
         my_plug_param_set_value(plug, ev->param_id, ev->value);
      }
   }
}

static const clap_plugin_params_t s_my_plug_params = {
   .count = my_plug_param_count,
   .get_info = my_plug_param_get_info,
   .get_value = my_plug_param_get_value,
   .value_to_text = my_plug_param_value_to_text,
   .text_to_value = my_plug_param_text_to_value,
   .flush = my_plug_param_flush,
};

////////////////
// clap_state //
////////////////

typedef struct {
   uint32_t size;
   char config_file[CLAP_PATH_SIZE];
   double params[num_params];
} my_plug_state_t;

bool my_plug_state_save(const clap_plugin_t *plugin, const clap_ostream_t *stream) {
   my_plug_t *plug = plugin->plugin_data;
   my_plug_state_t state;
   memset(&state, 0, sizeof(state));
   state.size = sizeof(state);
   timid_get_config_name(&plug->synth, state.config_file, sizeof(state.config_file));
   for (uint32_t i = 0; i < num_params; ++i)
      my_plug_param_get_value(plugin, i, &state.params[i]);
   if (stream->write(stream, &state, sizeof(state)))
      return true;
   return false;
}

bool my_plug_state_load(const clap_plugin_t *plugin, const clap_istream_t *stream) {
   my_plug_t *plug = plugin->plugin_data;
   my_plug_state_t state;
   memset(&state, 0, sizeof(state));
   if (!stream->read(stream, &state, sizeof(state)))
      return false;
   if (state.size != sizeof(state))
      return false;
   if (!timid_load_config(&plug->synth, state.config_file)) {
      if (plug->host_log) {
         char logtext[CLAP_PATH_SIZE];
         memset(logtext, 0, sizeof(logtext));
         snprintf(logtext, sizeof(logtext), "Failed to load Timidity configuration file at %s.\n", state.config_file);
         plug->host_log->log(plug->host, CLAP_LOG_ERROR, logtext);
      }
   }
   for (uint32_t i = 0; i < num_params; ++i)
      my_plug_param_set_value(plug, i, state.params[i]);
   return true;
}

static const clap_plugin_state_t s_my_plug_state = {
   .save = my_plug_state_save,
   .load = my_plug_state_load,
};

/////////////////
// clap_plugin //
/////////////////

static char plugin_dir[CLAP_PATH_SIZE];
static char *resources_dir = "Timidity-Resources";

static bool my_plug_init(const struct clap_plugin *plugin) {
   my_plug_t *plug = plugin->plugin_data;

   // Fetch host's extensions here
   // Make sure to check that the interface functions are not null pointers
   plug->host_log = (const clap_host_log_t *)plug->host->get_extension(plug->host, CLAP_EXT_LOG);
   plug->host_thread_check = (const clap_host_thread_check_t *)plug->host->get_extension(plug->host, CLAP_EXT_THREAD_CHECK);
   plug->host_latency = (const clap_host_latency_t *)plug->host->get_extension(plug->host, CLAP_EXT_LATENCY);
   plug->host_state = (const clap_host_state_t *)plug->host->get_extension(plug->host, CLAP_EXT_STATE);
   plug->host_params = (const clap_host_params_t *)plug->host->get_extension(plug->host, CLAP_EXT_PARAMS);

   timid_init(&plug->synth);
   if (strlen(plugin_dir)) {
      char config_file[CLAP_PATH_SIZE];
      memset(config_file, 0, sizeof(config_file));
      snprintf(config_file, sizeof(config_file), "%s%c%s%c%s", plugin_dir, PATH_SEP, resources_dir, PATH_SEP, CONFIG_FILE);
      if (!timid_load_config(&plug->synth, config_file)) {
         if (plug->host_log) {
            char logtext[CLAP_PATH_SIZE];
            memset(logtext, 0, sizeof(logtext));
            snprintf(logtext, sizeof(logtext), "Failed to load Timidity configuration file at %s.\n", config_file);
            plug->host_log->log(plug->host, CLAP_LOG_ERROR, logtext);
         }
      }
   }
   plug->buffer = NULL;
   return true;
}

static void my_plug_destroy(const struct clap_plugin *plugin) {
   my_plug_t *plug = plugin->plugin_data;
   timid_close(&plug->synth);
   if(plug->buffer) {
      free(plug->buffer);
      plug->buffer = NULL;
   }
   free(plug);
}

static bool my_plug_activate(const struct clap_plugin *plugin,
                             double                    sample_rate,
                             uint32_t                  min_frames_count,
                             uint32_t                  max_frames_count) {
   my_plug_t *plug = plugin->plugin_data;
   timid_set_sample_rate(&plug->synth, sample_rate);
   plug->buffer_size = max_frames_count;
   plug->buffer = (double *)malloc(2*plug->buffer_size*sizeof(double));;
   if(plug->buffer)
      memset(plug->buffer, 0, 2*plug->buffer_size*sizeof(double));
   if (plug->host_params)
      plug->host_params->rescan(plug->host, CLAP_PARAM_RESCAN_ALL);
   return true;
}

static void my_plug_deactivate(const struct clap_plugin *plugin) {
   my_plug_t *plug = plugin->plugin_data;
   if(plug->buffer) {
      memset(plug->buffer, 0, 2*plug->buffer_size*sizeof(double));
      free(plug->buffer);
      plug->buffer = NULL;
   }
}

static bool my_plug_start_processing(const struct clap_plugin *plugin) {
   my_plug_t *plug = plugin->plugin_data;
   if(plug->buffer) {
      memset(plug->buffer, 0, 2*plug->buffer_size*sizeof(double));
      return true;
   }
   return false;
}

static void my_plug_stop_processing(const struct clap_plugin *plugin) {
   my_plug_t *plug = plugin->plugin_data;
   if(plug->buffer) {
      memset(plug->buffer, 0, 2*plug->buffer_size*sizeof(double));
   }
}

static void my_plug_reset(const struct clap_plugin *plugin) {
   my_plug_t *plug = plugin->plugin_data;
   timid_reset(&plug->synth);
}

static void my_plug_process_event(my_plug_t *plug, const clap_event_header_t *hdr) {
   if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID) {
      switch (hdr->type) {
      case CLAP_EVENT_NOTE_ON: {
         const clap_event_note_t *ev = (const clap_event_note_t *)hdr;
         timid_channel_note_on(&plug->synth, ev->channel, ev->key, ev->velocity*127.0);
         break;
      }

      case CLAP_EVENT_NOTE_OFF: {
         const clap_event_note_t *ev = (const clap_event_note_t *)hdr;
         timid_channel_note_off(&plug->synth, ev->channel, ev->key);
         break;
      }

      case CLAP_EVENT_NOTE_CHOKE: {
         const clap_event_note_t *ev = (const clap_event_note_t *)hdr;
         timid_channel_key_pressure(&plug->synth, ev->channel, ev->key, 0);
         break;
      }

      case CLAP_EVENT_NOTE_EXPRESSION: {
         const clap_event_note_expression_t *ev = (const clap_event_note_expression_t *)hdr;
         // TODO: handle note expression
         break;
      }

      case CLAP_EVENT_PARAM_VALUE: {
         const clap_event_param_value_t *ev = (const clap_event_param_value_t *)hdr;
         my_plug_param_set_value(plug, ev->param_id, ev->value);
         break;
      }

      case CLAP_EVENT_PARAM_MOD: {
         const clap_event_param_mod_t *ev = (const clap_event_param_mod_t *)hdr;
         // TODO: handle parameter modulation
         break;
      }

      case CLAP_EVENT_TRANSPORT: {
         const clap_event_transport_t *ev = (const clap_event_transport_t *)hdr;
         // TODO: handle transport event
         break;
      }

      case CLAP_EVENT_MIDI: {
         const clap_event_midi_t *ev = (const clap_event_midi_t *)hdr;
         timid_write_midi(&plug->synth, ev->data[0], ev->data[1], ev->data[2]);
         break;
      }

      case CLAP_EVENT_MIDI_SYSEX: {
         const clap_event_midi_sysex_t *ev = (const clap_event_midi_sysex_t *)hdr;
         timid_write_sysex(&plug->synth, (uint8 *)ev->buffer, ev->size);
         break;
      }

      case CLAP_EVENT_MIDI2: {
         const clap_event_midi2_t *ev = (const clap_event_midi2_t *)hdr;
         // TODO: handle MIDI2 event
         break;
      }
      }
   }
}

static clap_process_status my_plug_process(const struct clap_plugin *plugin,
                                           const clap_process_t     *process) {
   my_plug_t     *plug = plugin->plugin_data;
   const uint32_t nframes = process->frames_count;
   const uint32_t nev = process->in_events->size(process->in_events);
   uint32_t       ev_index = 0;
   uint32_t       next_ev_frame = nev > 0 ? 0 : nframes;

   for (uint32_t i = 0; i < nframes;) {
      /* handle every events that happrens at the frame "i" */
      while (ev_index < nev && next_ev_frame == i) {
         const clap_event_header_t *hdr = process->in_events->get(process->in_events, ev_index);
         if (hdr->time != i) {
            next_ev_frame = hdr->time;
            break;
         }

         my_plug_process_event(plug, hdr);
         ++ev_index;

         if (ev_index == nev) {
            // we reached the end of the event list
            next_ev_frame = nframes;
            break;
         }
      }

      double      *bufptr;
      if (timid_get_mono(&plug->synth))
         bufptr = plug->buffer + i;
      else
         bufptr = plug->buffer + 2*i;
      uint32_t    curframes = next_ev_frame - i;
      timid_render_double(&plug->synth, bufptr, curframes);

      /* process every samples until the next event */
      for (; i < next_ev_frame; ++i) {
         // store output samples
         if (timid_get_mono(&plug->synth)) {
            if (process->audio_outputs[0].data32) {
               process->audio_outputs[0].data32[0][i] = plug->buffer[i];
               process->audio_outputs[0].data32[1][i] = plug->buffer[i];
            }
            if (process->audio_outputs[0].data64) {
               process->audio_outputs[0].data64[0][i] = plug->buffer[i];
               process->audio_outputs[0].data64[1][i] = plug->buffer[i];
            }
         }
         else {
            if (process->audio_outputs[0].data32) {
               process->audio_outputs[0].data32[0][i] = plug->buffer[i*2+0];
               process->audio_outputs[0].data32[1][i] = plug->buffer[i*2+1];
            }
            if (process->audio_outputs[0].data64) {
               process->audio_outputs[0].data64[0][i] = plug->buffer[i*2+0];
               process->audio_outputs[0].data64[1][i] = plug->buffer[i*2+1];
            }
         }
      }
   }

   return CLAP_PROCESS_CONTINUE;
}

static const void *my_plug_get_extension(const struct clap_plugin *plugin, const char *id) {
   if (!strcmp(id, CLAP_EXT_LATENCY))
      return &s_my_plug_latency;
   if (!strcmp(id, CLAP_EXT_AUDIO_PORTS))
      return &s_my_plug_audio_ports;
   if (!strcmp(id, CLAP_EXT_NOTE_PORTS))
      return &s_my_plug_note_ports;
   if (!strcmp(id, CLAP_EXT_STATE))
      return &s_my_plug_state;
   if (!strcmp(id, CLAP_EXT_PARAMS))
      return &s_my_plug_params;
   return NULL;
}

static void my_plug_on_main_thread(const struct clap_plugin *plugin) {}

clap_plugin_t *my_plug_create(const clap_host_t *host) {
   my_plug_t *p = calloc(1, sizeof(*p));
   p->host = host;
   p->plugin.desc = &s_my_plug_desc;
   p->plugin.plugin_data = p;
   p->plugin.init = my_plug_init;
   p->plugin.destroy = my_plug_destroy;
   p->plugin.activate = my_plug_activate;
   p->plugin.deactivate = my_plug_deactivate;
   p->plugin.start_processing = my_plug_start_processing;
   p->plugin.stop_processing = my_plug_stop_processing;
   p->plugin.reset = my_plug_reset;
   p->plugin.process = my_plug_process;
   p->plugin.get_extension = my_plug_get_extension;
   p->plugin.on_main_thread = my_plug_on_main_thread;

   // Don't call into the host here

   return &p->plugin;
}

/////////////////////////
// clap_plugin_factory //
/////////////////////////

static struct {
   const clap_plugin_descriptor_t *desc;
   clap_plugin_t *(CLAP_ABI *create)(const clap_host_t *host);
} s_plugins[] = {
   {
      .desc = &s_my_plug_desc,
      .create = my_plug_create,
   },
};

static uint32_t plugin_factory_get_plugin_count(const struct clap_plugin_factory *factory) {
   return sizeof(s_plugins) / sizeof(s_plugins[0]);
}

static const clap_plugin_descriptor_t *
plugin_factory_get_plugin_descriptor(const struct clap_plugin_factory *factory, uint32_t index) {
   return s_plugins[index].desc;
}

static const clap_plugin_t *plugin_factory_create_plugin(const struct clap_plugin_factory *factory,
                                                         const clap_host_t                *host,
                                                         const char *plugin_id) {
   if (!clap_version_is_compatible(host->clap_version)) {
      return NULL;
   }

   const int N = sizeof(s_plugins) / sizeof(s_plugins[0]);
   for (int i = 0; i < N; ++i)
      if (!strcmp(plugin_id, s_plugins[i].desc->id))
         return s_plugins[i].create(host);

   return NULL;
}

static const clap_plugin_factory_t s_plugin_factory = {
   .get_plugin_count = plugin_factory_get_plugin_count,
   .get_plugin_descriptor = plugin_factory_get_plugin_descriptor,
   .create_plugin = plugin_factory_create_plugin,
};

////////////////
// clap_entry //
////////////////

static bool entry_init(const char *plugin_path) {
   // perform the plugin initialization
   if (strlen(plugin_path)) {
      memset(plugin_dir, 0, sizeof(plugin_dir));
      snprintf(plugin_dir, sizeof(plugin_dir), "%s", plugin_path);
      char *sep = strrchr(plugin_dir, PATH_SEP);
      if (sep) *sep = '\0';
   }
   return true;
}

static void entry_deinit(void) {
   // perform the plugin de-initialization
}

#ifdef CLAP_HAS_THREAD
static mtx_t g_entry_lock;
static once_flag g_entry_once = ONCE_FLAG_INIT;
#endif

static int g_entry_init_counter = 0;

#ifdef CLAP_HAS_THREAD
// Initializes the necessary mutex for the entry guard
static void entry_init_guard_init(void) {
   mtx_init(&g_entry_lock, mtx_plain);
}
#endif

// Thread safe init counter
static bool entry_init_guard(const char *plugin_path) {
#ifdef CLAP_HAS_THREAD
   call_once(&g_entry_once, entry_init_guard_init);

   mtx_lock(&g_entry_lock);
#endif

   const int cnt = ++g_entry_init_counter;
   assert(cnt > 0);

   bool succeed = true;
   if (cnt == 1) {
      succeed = entry_init(plugin_path);
      if (!succeed)
         g_entry_init_counter = 0;
   }

#ifdef CLAP_HAS_THREAD
   mtx_unlock(&g_entry_lock);
#endif

   return succeed;
}

// Thread safe deinit counter
static void entry_deinit_guard(void) {
#ifdef CLAP_HAS_THREAD
   call_once(&g_entry_once, entry_init_guard_init);

   mtx_lock(&g_entry_lock);
#endif

   const int cnt = --g_entry_init_counter;
   assert(cnt >= 0);

   bool succeed = true;
   if (cnt == 0)
      entry_deinit();

#ifdef CLAP_HAS_THREAD
   mtx_unlock(&g_entry_lock);
#endif
}

static const void *entry_get_factory(const char *factory_id) {
#ifdef CLAP_HAS_THREAD
   call_once(&g_entry_once, entry_init_guard_init);
#endif

   assert(g_entry_init_counter > 0);
   if (g_entry_init_counter <= 0)
      return NULL;

   if (!strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID))
      return &s_plugin_factory;
   return NULL;
}

// This symbol will be resolved by the host
CLAP_EXPORT const clap_plugin_entry_t clap_entry = {
   .clap_version = CLAP_VERSION_INIT,
   .init = entry_init_guard,
   .deinit = entry_deinit_guard,
   .get_factory = entry_get_factory,
};
