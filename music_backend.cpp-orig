#include "music_backend.h"
#include <glib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <errno.h>

#include <fstream>
#include <vector>
#include <mutex>

extern "C" {
#include <faad/neaacdec.h>
#include "mpeg4/mp4read.h"
}

// Global mutex to protect the non-reentrant mp4read library
static std::mutex mp4_mutex;

const char* PIPE_PATH = "/tmp/kinamp_audio_pipe";

// =================================================================================
// Decoder Implementation
// =================================================================================

Decoder::Decoder() : stop_flag(false), running(false), thread_id(0) {
    unlink(PIPE_PATH);
    if (mkfifo(PIPE_PATH, 0666) == -1) {
        perror("Decoder: Failed to create named pipe");
    }
}

Decoder::~Decoder() {
    stop();
    unlink(PIPE_PATH);
}

bool Decoder::start(const char* filepath, int start_time) {
    if (running) {
        stop();
    }

    current_filepath = filepath;
    this->start_time = start_time;
    stop_flag = false;
    running = true;

    if (pthread_create(&thread_id, NULL, thread_func, this) != 0) {
        perror("Decoder: Failed to create thread");
        running = false;
        return false;
    }
    return true;
}

void Decoder::stop() {
    if (!running) return;

    stop_flag = true;

    // We assume the caller (MusicBackend) has already broken the pipe 
    // by setting GStreamer state to NULL. This unblocks the write().
    
    // Unblock potential open() in thread if it's waiting for a reader
    int fd = open(PIPE_PATH, O_RDONLY | O_NONBLOCK);
    if (fd >= 0) close(fd);

    if (thread_id != 0) {
        pthread_join(thread_id, NULL);
        thread_id = 0;
    }

    running = false;
}

bool Decoder::is_running() const {
    return running;
}

void* Decoder::thread_func(void* arg) {
    Decoder* self = static_cast<Decoder*>(arg);
    self->decode_loop();
    return NULL;
}

void Decoder::decode_loop() {
    g_print("Decoder: Starting for %s\n", current_filepath.c_str());

    std::lock_guard<std::mutex> lock(mp4_mutex);

    if (mp4read_open(const_cast<char*>(current_filepath.c_str())) != 0) {
        g_printerr("Decoder: Failed to open file with mp4read: %s\n", current_filepath.c_str());
        return;
    }

    NeAACDecHandle hDecoder = NeAACDecOpen();
    if (!hDecoder) {
        g_printerr("Decoder: Failed to open FAAD2 decoder\n");
        mp4read_close();
        return;
    }

    NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(hDecoder);
    config->outputFormat = FAAD_FMT_16BIT;
    config->downMatrix = 1;
    NeAACDecSetConfiguration(hDecoder, config);

    unsigned long samplerate;
    unsigned char channels;
    if ((int8_t)NeAACDecInit2(hDecoder, mp4config.asc.buf, mp4config.asc.size, &samplerate, &channels) < 0) {
        g_printerr("Decoder: Failed to initialize FAAD2 with ASC\n");
        NeAACDecClose(hDecoder);
        mp4read_close();
        return;
    }
    g_print("Decoder: Starting for %d %d\n", samplerate, channels);

    if (this->start_time > 0) {
        unsigned long samples_per_frame = 1024;
        if (mp4config.frame.nsamples > 0 && mp4config.samples > 0) {
             samples_per_frame = mp4config.samples / mp4config.frame.nsamples;
        }
        
        unsigned long target_frame = (unsigned long)((double)this->start_time * samplerate / samples_per_frame);
        
        if (target_frame < mp4config.frame.nsamples) {
             if (mp4read_seek(target_frame) == 0) {
                 g_print("Decoder: Seeked to %d seconds (frame %lu)\n", this->start_time, target_frame);
             } else {
                 g_printerr("Decoder: Failed to seek to frame %lu\n", target_frame);
             }
        }
    } else {
        mp4read_seek(0);
    }

    if (stop_flag) {
        NeAACDecClose(hDecoder);
        mp4read_close();
        return;
    }

    int fd = open(PIPE_PATH, O_WRONLY);
    if (fd == -1) {
        perror("Decoder: Failed to open pipe");
        NeAACDecClose(hDecoder);
        mp4read_close();
        return;
    }

    if (stop_flag) {
        close(fd);
        NeAACDecClose(hDecoder);
        mp4read_close();
        return;
    }

    while (!stop_flag) {
        if (mp4read_frame() != 0) {
            break;
        }

        NeAACDecFrameInfo frameInfo;
        void* sample_buffer = NeAACDecDecode(hDecoder, &frameInfo, 
                                             mp4config.bitbuf.data, 
                                             mp4config.bitbuf.size);

        if (frameInfo.error > 0) {
             g_printerr("Decoder: FAAD Warning: %s\n", NeAACDecGetErrorMessage(frameInfo.error));
             continue;
        }

        if (frameInfo.samples > 0) {
            // frameInfo.samples is the total number of samples (channels * samples_per_channel)
            // We configured FAAD_FMT_16BIT, so each sample is 2 bytes (int16_t).
            ssize_t to_write = frameInfo.samples * 2; 
            
            ssize_t written = write(fd, sample_buffer, to_write);

            if (written == -1) {
                if (errno == EPIPE) {
                    // Reader closed pipe, expected during stop
                    break;
                }
                perror("Decoder: write error");
                break;
            }
        }
    }

    close(fd);
    NeAACDecClose(hDecoder);
    mp4read_close();
    g_print("Decoder: Thread exiting.\n");
}


// =================================================================================
// MusicBackend Implementation
// =================================================================================

MusicBackend::MusicBackend() 
    : is_playing(false), is_paused(false), pipeline(NULL), bus(NULL), bus_watch_id(0),
      stopping(false), on_eos_callback(NULL), eos_user_data(NULL), last_position(0), current_samplerate(44100), total_duration(0)
{
    signal(SIGPIPE, SIG_IGN);
    
    gst_init(NULL, NULL);
    decoder = std::unique_ptr<Decoder>(new Decoder());
}

MusicBackend::~MusicBackend() {
    stop();
}

bool MusicBackend::is_shutting_down() const {
    return stopping;
}

const char* MusicBackend::get_current_filepath() {
    return current_filepath_str.c_str();
}

void MusicBackend::set_eos_callback(EosCallback callback, void* user_data) {
    on_eos_callback = callback;
    eos_user_data = user_data;
}

gint64 MusicBackend::get_duration() {
    if (total_duration > 0) return total_duration;

    if (pipeline) {
        GstFormat format = GST_FORMAT_TIME;
        gint64 duration;
        if (gst_element_query_duration(pipeline, &format, &duration)) {
            return duration;
        }
    }
    return 0;
}

gint64 MusicBackend::get_position() {
    if (is_paused) {
        return last_position;
    }

    if (pipeline && is_playing) {
        GstClock *clock = gst_element_get_clock(pipeline);
        if (clock) {
            GstClockTime current_time = gst_clock_get_time(clock);
            GstClockTime base_time = gst_element_get_base_time(pipeline);
            gst_object_unref(clock);

            if (GST_CLOCK_TIME_IS_VALID(base_time) && current_time > base_time) {
                return (gint64)(current_time - base_time) + last_position;
            }
        }
    }
    return last_position;
}

void MusicBackend::read_metadata(const char* filepath) {
    std::lock_guard<std::mutex> lock(mp4_mutex);
    meta_title.clear();
    meta_artist.clear();
    meta_album.clear();
    cover_art.clear();
    chapters.clear();
    if (filepath == nullptr) return;

    mp4config.verbose.tags = 1;

    if (mp4read_open((char*)filepath) == 0) {
        if (mp4config.meta_title) meta_title = mp4config.meta_title;
        if (mp4config.meta_artist) meta_artist = mp4config.meta_artist;
        if (mp4config.meta_album) meta_album = mp4config.meta_album;
        if (mp4config.cover_art.data && mp4config.cover_art.size > 0) {
            cover_art.assign(mp4config.cover_art.data, mp4config.cover_art.data + mp4config.cover_art.size);
        }
        
        if (mp4config.chapters && mp4config.chapter_count > 0) {
            for (uint32_t i = 0; i < mp4config.chapter_count; ++i) {
                Chapter ch;
                ch.timestamp = mp4config.chapters[i].timestamp;
                ch.title = mp4config.chapters[i].title ? mp4config.chapters[i].title : "";
                chapters.push_back(ch);
            }
        }
        
        // Get sample rate from FAAD (NeAACDecInit2) as MP4 header value might be unreliable
        NeAACDecHandle hDecoder = NeAACDecOpen();
        if (hDecoder) {
             NeAACDecConfigurationPtr config = NeAACDecGetCurrentConfiguration(hDecoder);
             config->outputFormat = FAAD_FMT_16BIT;
             NeAACDecSetConfiguration(hDecoder, config);

             unsigned long rate = 0;
             unsigned char channels = 0;
             if ((int8_t)NeAACDecInit2(hDecoder, mp4config.asc.buf, mp4config.asc.size, &rate, &channels) >= 0) {
                 if (rate > 0) {
                     current_samplerate = (int)rate;
                 }
             }
             NeAACDecClose(hDecoder);
        }
        
        if (mp4config.samplerate > 0 && mp4config.samples > 0) {
            total_duration = (gint64)mp4config.samples * GST_SECOND / mp4config.samplerate;
        } else {
            total_duration = 0;
        }

        mp4read_close();
    } else {
        g_printerr("Backend: Failed to read metadata for %s\n", filepath);
    }
    
    // Disable tag parsing to avoid overhead during playback
    mp4config.verbose.tags = 0;
}

void MusicBackend::play_file(const char* filepath, int start_time) {
    if (stopping) return;

    // If already playing, stop first.
    // Note: This calls our synchronous stop(), which waits for the decoder thread.
    // If this takes too long, it might freeze UI briefly.
    if (is_playing || is_paused) {
        stop();
    }

    g_print("Backend: Playing %s from %d\n", filepath, start_time);
    current_filepath_str = filepath;
    is_playing = true;
    is_paused = false;
    last_position = start_time * GST_SECOND;

    int rate = (current_samplerate > 0) ? current_samplerate : 44100;

    // filesrc reads from named pipe
    gchar *pipeline_desc = g_strdup_printf(
        "filesrc location=\"%s\" ! audio/x-raw-int, endianness=1234, signed=true, width=16, depth=16, rate=%d, channels=2 ! queue ! mixersink",
        PIPE_PATH, rate
    );
    pipeline = gst_parse_launch(pipeline_desc, NULL);
    g_free(pipeline_desc);

    if (!pipeline) {
        g_printerr("Backend: Failed to create pipeline\n");
        is_playing = false;
        return;
    }

    bus = gst_element_get_bus(pipeline);
    bus_watch_id = gst_bus_add_watch(bus, bus_callback_func, this);
    gst_object_unref(bus);

    if (!decoder->start(filepath, start_time)) {
        cleanup_pipeline();
        return;
    }

    gst_element_set_state(pipeline, GST_STATE_PLAYING);
}

void MusicBackend::pause() {
    if (!pipeline || !is_playing) return;

    if (is_paused) {
        GstClock *clock = gst_element_get_clock(pipeline);
        if (clock) {
            GstClockTime current_time = gst_clock_get_time(clock);
            GstClockTime base_time = gst_element_get_base_time(pipeline);
            gst_object_unref(clock);

            if (GST_CLOCK_TIME_IS_VALID(base_time) && current_time > base_time) {
                gint64 running_time = (gint64)(current_time - base_time);
                last_position -= running_time;
            }
        }
        
        gst_element_set_state(pipeline, GST_STATE_PLAYING);
        is_paused = false;
    } else {
        last_position = get_position();
        gst_element_set_state(pipeline, GST_STATE_PAUSED);
        is_paused = true;
    }
}

void MusicBackend::stop() {
    if (stopping) return;
    stopping = true;

    // 1. Break the pipe connection.
    // Setting pipeline to NULL closes the file descriptor in filesrc.
    // This causes the writer (Decoder) to receive EPIPE on next write.
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
    }

    // 2. Stop Decoder
    // This joins the thread. It should return quickly now that pipe is broken.
    decoder->stop();

    cleanup_pipeline();
    
    stopping = false;
    is_playing = false;
    is_paused = false;
}

void MusicBackend::cleanup_pipeline() {
    if (bus_watch_id > 0) {
        g_source_remove(bus_watch_id);
        bus_watch_id = 0;
    }
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = NULL;
    }
}

gboolean MusicBackend::bus_callback_func(GstBus *bus, GstMessage *msg, gpointer data) {
    MusicBackend* self = static_cast<MusicBackend*>(data);

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("Backend: EOS reached.\n");
            self->stop(); 

            if (self->on_eos_callback) {
                self->on_eos_callback(self->eos_user_data);
            }
            break;
        case GST_MESSAGE_ERROR: {
            GError *err;
            gchar *debug;
            gst_message_parse_error(msg, &err, &debug);
            g_printerr("Backend: Error: %s\n", err->message);
            g_error_free(err);
            g_free(debug);
            self->stop();
            break;
        }
        default:
            break;
    }
    return TRUE;
}