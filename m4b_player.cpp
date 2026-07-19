#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <string>
#include <fstream>

#include <map>

#include "music_backend.h"
#include "database_manager.h"
#include "keyboarddialog.hpp"
#include "openlipc/openlipc.h"

#include "icons.h"

#define DESKTOP_W_SIZE 600
#define DESKTOP_H_SIZE 800

MusicBackend backend;
DatabaseManager db;
GtkWidget *window;
GtkWidget *progress_bar;
GtkWidget *time_label;
GtkWidget *cover_image;
GtkWidget *title_label;
GtkWidget *artist_label;
GtkWidget *album_label;
GtkWidget *play_pause_btn;

bool user_is_seeking = false;
std::string current_file;
int last_timestamp = 0;
int flIntensity = 0;
bool dispUpdate=true;
bool is_hires=true;

// Layout variables
int btn_size = 80;
int play_pause_width = 100;
int cover_size_w = 350;
int cover_size_h = 450;
int title_font_size = 24;
int artist_font_size = 14;
int time_font_size = 16;
int chapter_font_size = 14;
int main_spacing = 10;
int mid_spacing = 15;
int btn_spacing = 20;
int bot_spacing = 10;
int bot_padding = 40;
int side_padding = 20;
int btn_padding = 5;

static LIPC * lipcInstance = 0;

void openLipcInstance() {
	if (lipcInstance == 0) {
		lipcInstance = LipcOpen("com.kbarni.larkplayer");
	}
}

void closeLipcInstance() {
	if (lipcInstance != 0) {
		LipcClose(lipcInstance);
	}
}

void enableSleep() {
    LipcSetIntProperty(lipcInstance,"com.lab126.powerd","preventScreenSaver",0);
}

void disableSleep() {
    LipcSetIntProperty(lipcInstance,"com.lab126.powerd","preventScreenSaver",1);
}

void toggleFrontLight(){
    int intensity = 0;
    LipcGetIntProperty(lipcInstance,"com.lab126.powerd","flIntensity",&intensity);
    if(intensity == 0) {
        LipcSetIntProperty(lipcInstance,"com.lab126.powerd","flIntensity",flIntensity);
    } else {
        flIntensity=intensity;
        LipcSetIntProperty(lipcInstance,"com.lab126.powerd","flIntensity",0);
    }
}

GtkWidget* create_button_from_icon(const guint8* icon_data, int padding) {
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_inline(-1, icon_data, FALSE, NULL);
    GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
    
    GtkWidget *button = gtk_button_new();
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_misc_set_padding(GTK_MISC(image), padding, padding);

    g_object_unref(pixbuf);
    return button;
}

void set_button_icon(GtkWidget *button, const unsigned char *icon_data) {
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_inline(-1, icon_data, FALSE, NULL);
    GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
    
    gtk_button_set_image(GTK_BUTTON(button), image);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    // Use the global padding variable
    gtk_misc_set_padding(GTK_MISC(image), btn_padding, btn_padding);
    
    g_object_unref(pixbuf);
    gtk_widget_show(image);
}

std::string get_home_dir() {
    /*const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    return std::string(home ? home : ".");*/
    return "/mnt/us";
}

std::string get_db_path() {
    return get_home_dir() + "/.lark_player.db";
}

void save_history() {
    if (!current_file.empty()) {
        gint64 pos = backend.get_position() / GST_SECOND;
        gint64 dur = backend.get_duration() / GST_SECOND;
        // If duration is 0 (e.g. stopped), try to get it from book if we can, or just pass 0.
        // updateBookProgress handles insert/update.
        db.updateBookProgress(current_file, (int)pos, (int)dur, false);
        db.setSetting("last_opened_file", current_file);
    }
}

void init_database() {
    std::string db_path = get_db_path();
    if (!db.init(db_path)) {
        g_print("Failed to initialize database at %s\n", db_path.c_str());
        return;
    }

    // Check for legacy history and migrate
    std::string legacy_path = get_home_dir() + "/.lark_history";
    std::ifstream legacy_file(legacy_path);
    if (legacy_file.good()) {
        legacy_file.close();
        g_print("Migrating legacy history from %s\n", legacy_path.c_str());
        if (db.migrateFromLegacy(legacy_path)) {
            g_print("Migration successful.\n");
        } else {
            g_print("Migration failed.\n");
        }
    }

    // Restore last opened file
    std::string last_file = db.getLastPlayedFile();
    if (!last_file.empty() && current_file.empty()) {
        current_file = last_file;
    }
}

// Basename without extension, used when the file has no embedded tags (MP3/FLAC/WAV)
std::string get_display_name(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
    return name;
}

void update_metadata_ui() {
    std::string title = backend.meta_title;
    if (title.empty() && !current_file.empty()) {
        title = get_display_name(current_file);
    }

    if (!title.empty()) {
        char *markup = g_markup_printf_escaped("<span font_desc='Sans Bold %d'>%s</span>", title_font_size, title.c_str());
        gtk_label_set_markup(GTK_LABEL(title_label), markup);
        g_free(markup);
    } else {
        gtk_label_set_text(GTK_LABEL(title_label), "");
    }

    if (!backend.meta_artist.empty()) {
        char *markup = g_markup_printf_escaped("<span font_desc='Sans Italic %d'>%s</span>", artist_font_size, backend.meta_artist.c_str());
        gtk_label_set_markup(GTK_LABEL(artist_label), markup);
        g_free(markup);
    } else {
        gtk_label_set_text(GTK_LABEL(artist_label), "");
    }

    if (!backend.cover_art.empty()) {
        GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
        gdk_pixbuf_loader_write(loader, backend.cover_art.data(), backend.cover_art.size(), NULL);
        gdk_pixbuf_loader_close(loader, NULL);
        GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pixbuf) {
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, cover_size_w, cover_size_h, GDK_INTERP_BILINEAR);
            gtk_image_set_from_pixbuf(GTK_IMAGE(cover_image), scaled);
            g_object_unref(scaled);
        }
        g_object_unref(loader);
    } else {
        // No embedded cover art: show the generic audiobook icon,
        // scaled to fit the cover area without distorting it
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_inline(-1, audiobook, FALSE, NULL);
        if (pixbuf) {
            int w = gdk_pixbuf_get_width(pixbuf);
            int h = gdk_pixbuf_get_height(pixbuf);
            double scale = MIN((double)cover_size_w / w, (double)cover_size_h / h);
            GdkPixbuf *scaled = gdk_pixbuf_scale_simple(pixbuf, (int)(w * scale), (int)(h * scale), GDK_INTERP_BILINEAR);
            gtk_image_set_from_pixbuf(GTK_IMAGE(cover_image), scaled);
            g_object_unref(scaled);
            g_object_unref(pixbuf);
        } else {
            gtk_image_clear(GTK_IMAGE(cover_image));
        }
    }
}

gboolean show_error_dialog_idle(gpointer data) {
    char *msg = (char*)data;
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                       GTK_DIALOG_DESTROY_WITH_PARENT,
                                       GTK_MESSAGE_ERROR,
                                       GTK_BUTTONS_OK,
                                       "%s", msg);
    gtk_window_set_title(GTK_WINDOW(dialog), "L:A_N:application_PC:T_ID:com.kbarni.larkplayer");
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_free(msg);
    return FALSE;
}

// Called from the decoder thread; GTK calls must happen on the main loop
void on_backend_error(const char* msg, void* data) {
    (void)data;
    g_idle_add(show_error_dialog_idle, g_strdup(msg));
}

gboolean update_ui(gpointer data) {
    (void)data;
    if (!backend.is_playing && !backend.is_paused) return TRUE;

    gint64 pos = backend.get_position();
    gint64 len = backend.get_duration();

    // Format: 00:01:23 / 02:10:20
    int pos_sec = pos / GST_SECOND;
    int len_sec = len / GST_SECOND;
    char buf[64];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d / %02d:%02d:%02d", 
             pos_sec / 3600, (pos_sec % 3600) / 60, pos_sec % 60,
             len_sec / 3600, (len_sec % 3600) / 60, len_sec % 60);
    
    if(dispUpdate)
        gtk_label_set_text(GTK_LABEL(time_label), buf);
    else
        gtk_label_set_text(GTK_LABEL(time_label), "          ");
    return TRUE;
}

void on_play_pause_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    if (backend.is_playing) {
        backend.pause();
    } else {
        if (!current_file.empty()) {
            if (backend.is_paused) {
                backend.pause();
            } else {
                backend.play_file(current_file.c_str(), last_timestamp);
            }
        }
    }
}

void jump_relative(int seconds) {
    if (current_file.empty()) return;
    
    gint64 duration = backend.get_duration() / GST_SECOND;
    
    int current_pos = last_timestamp;
    if (backend.is_playing || backend.is_paused) {
        current_pos = backend.get_position() / GST_SECOND;
    }
    
    int new_pos = current_pos + seconds;
    if (new_pos < 0) new_pos = 0;
    if (duration > 0 && new_pos > duration) new_pos = duration;
    
    last_timestamp = new_pos;
    
    backend.play_file(current_file.c_str(), new_pos);
}

void on_fl_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    toggleFrontLight();
}

void on_bluetooth_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    LipcSetStringProperty(lipcInstance,"com.lab126.btfd","BTenable","1:1");
    LipcSetStringProperty(lipcInstance,"com.lab126.pillow","customDialog","{\"name\":\"bt_wizard_dialog\", \"clientParams\": {\"show\":true, \"winmgrModal\":true, \"replySrc\":\"\"}}");
}

void on_displayUpdate_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    dispUpdate = !(dispUpdate);
}

void on_rewind_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    jump_relative(-30);
}

void on_ff_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    jump_relative(30);
}

void on_destroy(GtkWidget *widget, gpointer data) {
    (void)widget;
    (void)data;
    backend.stop();
    save_history();
    LipcSetIntProperty(lipcInstance,"com.lab126.powerd","flIntensity",flIntensity);
    LipcSetIntProperty(lipcInstance,"com.lab126.btfd","ensureBTconnection",0);
    enableSleep();
    closeLipcInstance();
    gtk_main_quit();
    exit(0);
}

void on_close_clicked(GtkWidget *widget, gpointer data) {
    on_destroy(widget, data);
}

void on_file_open(const char* filepath) {
    if (!filepath) return;
    
    if (!current_file.empty() && (backend.is_playing || backend.is_paused)) {
        gint64 pos = backend.get_position() / GST_SECOND;
        gint64 dur = backend.get_duration() / GST_SECOND;
        db.updateBookProgress(current_file, (int)pos, (int)dur, false);
    }

    backend.stop();
    
    current_file = filepath;
    db.setSetting("last_opened_file", current_file);
    
    Book book;
    if (db.getBookProgress(filepath, book)) {
        last_timestamp = book.last_position;
    } else {
        last_timestamp = 0;
    }

    g_print("Reading metadata for %s\n", filepath);
    backend.read_metadata(filepath);
    g_print("Metadata read: Title='%s', Artist='%s', Album='%s'\n",
            backend.meta_title.c_str(),
            backend.meta_artist.c_str(),
            backend.meta_album.c_str());
    update_metadata_ui();
    g_print("Starting playback for %s at %d seconds\n", filepath, last_timestamp);
    backend.play_file(filepath, last_timestamp);
}

void on_open_dialog_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    GtkWidget *dialog;
    dialog = gtk_file_chooser_dialog_new("L:A_N:application_PC:T_ID:com.kbarni.larkplayer",
                                         GTK_WINDOW(window),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                         NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        last_timestamp = 0;
        on_file_open(filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

void on_history_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    GtkWidget *dialog = gtk_dialog_new_with_buttons("L:A_N:application_PC:T_ID:com.kbarni.larkplayer",
                                                     GTK_WINDOW(window),
                                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                                     NULL);
    
    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *tree_view = gtk_tree_view_new();
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    
    std::vector<Book> history = db.getHistory();
    for (const auto& item : history) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, item.filepath.c_str(), 1, item.last_position, -1);
    }
    
    gtk_tree_view_set_model(GTK_TREE_VIEW(tree_view), GTK_TREE_MODEL(store));
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("File", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    
    
    gtk_container_add(GTK_CONTAINER(content_area), tree_view);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
        GtkTreeIter iter;
        GtkTreeModel *model;
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            char *file;
            gtk_tree_model_get(model, &iter, 0, &file, -1);
            on_file_open(file);
            g_free(file);
        }
    }
    
    gtk_widget_destroy(dialog);
}

void on_add_bookmark_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;
    if (current_file.empty()) return;
    
    bool was_playing = backend.is_playing && !backend.is_paused;
    if (was_playing) {
        backend.pause();
    }
    
    int pos = backend.get_position() / GST_SECOND;
    
    std::vector<Bookmark> bookmarks = db.getBookmarks(current_file);
    int n = bookmarks.size() + 1;
    
    char buf[80];
    snprintf(buf, sizeof(buf), "Bookmark %d", n);
    std::string text = buf;
             
    show_dialog_keyboard(text);
    
    if (!text.empty()) {
        sprintf(buf,"%s %02d:%02d:%02d", text.c_str(), pos/3600, (pos%3600)/60, pos%60);
        if (db.addBookmark(current_file, pos, buf)) {
            g_print("Bookmark added: %s\n", text.c_str());
        }
    }
    
    if (was_playing) {
        backend.pause();
    }
}

void on_bookmark_list_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;

    if (current_file.empty()) return;
    
    GtkWidget *dialog = gtk_dialog_new_with_buttons("L:A_N:application_PC:T_ID:com.kbarni.larkplayer",
                                                     GTK_WINDOW(window),
                                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                                     NULL);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *tree_view = gtk_tree_view_new();
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
    
    std::vector<Bookmark> bookmarks = db.getBookmarks(current_file);
    for (const auto& b : bookmarks) {
        GtkTreeIter iter;
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, b.name.c_str(), 1, b.position, -1);
    }
    
    gtk_tree_view_set_model(GTK_TREE_VIEW(tree_view), GTK_TREE_MODEL(store));
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Bookmark", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    
    gtk_container_add(GTK_CONTAINER(content_area), tree_view);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
        GtkTreeIter iter;
        GtkTreeModel *model;
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            int pos;
            gtk_tree_model_get(model, &iter, 1, &pos, -1);
            last_timestamp = pos;
            backend.play_file(current_file.c_str(), last_timestamp);
        }
    }
    gtk_widget_destroy(dialog);
}

void on_chapter_list_clicked(GtkWidget *widget, gpointer data) {
    (void)widget;(void)data;

    if (backend.chapters.empty()) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                           GTK_DIALOG_DESTROY_WITH_PARENT,
                                           GTK_MESSAGE_INFO,
                                           GTK_BUTTONS_OK,
                                           "No chapters found.");
        gtk_window_set_title(GTK_WINDOW(dialog), "L:A_N:application_PC:T_ID:com.kbarni.larkplayer");
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return;
    }

    GtkWidget *dialog = gtk_dialog_new_with_buttons("L:A_N:application_PC:T_ID:com.kbarni.larkplayer",
                                                     GTK_WINDOW(window),
                                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                                     NULL);
    
    gtk_window_set_default_size(GTK_WINDOW(dialog), 500, 600);

    GtkWidget *content_area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    
    GtkWidget *scrolled_window = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled_window),
                                   GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(content_area), scrolled_window);

    GtkWidget *tree_view = gtk_tree_view_new();
    GtkListStore *store = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT64);
    
    for (const auto& ch : backend.chapters) {
        GtkTreeIter iter;
        uint64_t total_seconds = ch.timestamp / 10000000;
        int hours = total_seconds / 3600;
        int minutes = (total_seconds % 3600) / 60;
        int seconds = total_seconds % 60;
        
        char buf[512];
        snprintf(buf, sizeof(buf), "%s (%02d:%02d:%02d)", ch.title.c_str(), hours, minutes, seconds);
        
        gtk_list_store_append(store, &iter);
        gtk_list_store_set(store, &iter, 0, buf, 1, (gint64)ch.timestamp, -1);
    }
    
    gtk_tree_view_set_model(GTK_TREE_VIEW(tree_view), GTK_TREE_MODEL(store));
    g_object_unref(store);
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    char font_desc[32];
    snprintf(font_desc, sizeof(font_desc), "Sans %d", chapter_font_size);
    g_object_set(G_OBJECT(renderer), "font", font_desc, NULL);
    
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Chapters", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree_view), column);
    
    gtk_container_add(GTK_CONTAINER(scrolled_window), tree_view);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_view));
        GtkTreeIter iter;
        GtkTreeModel *model;
        if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
            gint64 ts;
            gtk_tree_model_get(model, &iter, 1, &ts, -1);
            last_timestamp = (int)(ts / 10000000);
            backend.play_file(current_file.c_str(), last_timestamp);
        }
    }
    gtk_widget_destroy(dialog);
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    backend.set_error_callback(on_backend_error, NULL);

    init_database();
    if (argc > 1) {
        current_file = argv[1];
        last_timestamp = 0;
    }

    GdkScreen *screen = gdk_screen_get_default();
    gint width = gdk_screen_get_width(screen);
    gint height = gdk_screen_get_height(screen);
    is_hires = (width >= 1000);
    
    if (!is_hires) {
        btn_size = 60;
        play_pause_width = 80;
        cover_size_w = 250;
        cover_size_h = 320;
        title_font_size = 18;
        artist_font_size = 12;
        time_font_size = 14;
        chapter_font_size = 12;
        main_spacing = 5;
        mid_spacing = 5;
        btn_spacing = 5;
        bot_spacing = 5;
        bot_padding = 20;
        side_padding = 10;
        btn_padding = 2;
    }

    g_print("Detected resolution: %dx%d, using %s mode\n", width, height, (is_hires?"High res":"Low res"));

    openLipcInstance();
    disableSleep();
    LipcGetIntProperty(lipcInstance,"com.lab126.powerd","flIntensity",&flIntensity);

    LipcSetIntProperty(lipcInstance,"com.lab126.btfd","ensureBTconnection",1);
    LipcSetStringProperty(lipcInstance,"com.lab126.btfd","BTenable","1:1");

    // Window Setup
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_size_request(window, DESKTOP_W_SIZE, DESKTOP_H_SIZE);
    gtk_window_set_title(GTK_WINDOW(window), "L:A_N:application_PC:T_ID:com.kbarni.larkplayer");
    g_signal_connect(window, "destroy", G_CALLBACK(on_destroy), NULL);
    
    GtkRcStyle *style = gtk_widget_get_modifier_style(window);
    style->bg[GTK_STATE_NORMAL].red = 65535;
    style->bg[GTK_STATE_NORMAL].green = 65535;
    style->bg[GTK_STATE_NORMAL].blue = 65535;
    gtk_widget_modify_style(window, style);

    GtkWidget *main_vbox = gtk_vbox_new(FALSE, main_spacing);
    gtk_container_add(GTK_CONTAINER(window), main_vbox);

    // --- TOP TITLE IMAGE ---
    GdkPixbuf *title_pixbuf = gdk_pixbuf_new_from_inline(-1, is_hires ? title_image : title_lr, FALSE, NULL);
    GtkWidget *title_image = gtk_image_new_from_pixbuf(title_pixbuf);
    g_object_unref(title_pixbuf);
    
    GtkWidget *title_align = gtk_alignment_new(0.5, 0, 0, 0);
    gtk_container_add(GTK_CONTAINER(title_align), title_image);
    gtk_box_pack_start(GTK_BOX(main_vbox), title_align, FALSE, FALSE, main_spacing);


    // --- MIDDLE AREA (Cover, Title, Author, Timer, Play/Pause) ---
    GtkWidget *mid_vbox = gtk_vbox_new(FALSE, mid_spacing);
    gtk_box_pack_start(GTK_BOX(main_vbox), mid_vbox, TRUE, TRUE, 0);

    cover_image = gtk_image_new();
    gtk_box_pack_start(GTK_BOX(mid_vbox), cover_image, TRUE, TRUE, 0);

    title_label = gtk_label_new("");
    gtk_label_set_justify(GTK_LABEL(title_label), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap(GTK_LABEL(title_label), TRUE);
    gtk_box_pack_start(GTK_BOX(mid_vbox), title_label, FALSE, FALSE, 0);

    artist_label = gtk_label_new("");
    gtk_label_set_justify(GTK_LABEL(artist_label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(mid_vbox), artist_label, FALSE, FALSE, 0);

    GtkWidget *time_align = gtk_alignment_new(0.5, 0, 0, 0);
    GtkWidget *time_frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME(time_frame), GTK_SHADOW_ETCHED_IN);
    time_label = gtk_label_new("00:00:00 / 00:00:00");
    
    GtkWidget *time_pad = gtk_alignment_new(0.5, 0.5, 1, 1);
    gtk_alignment_set_padding(GTK_ALIGNMENT(time_pad), 5, 5, side_padding, side_padding);
    gtk_container_add(GTK_CONTAINER(time_pad), time_label);
    
    gtk_container_add(GTK_CONTAINER(time_frame), time_pad);
    gtk_container_add(GTK_CONTAINER(time_align), time_frame);
    gtk_box_pack_start(GTK_BOX(mid_vbox), time_align, FALSE, FALSE, main_spacing);
    
    char time_font_desc[32];
    snprintf(time_font_desc, sizeof(time_font_desc), "Sans %d", time_font_size);
    PangoFontDescription *time_font = pango_font_description_from_string(time_font_desc);
    gtk_widget_modify_font(time_label, time_font);
    pango_font_description_free(time_font);

    // Playback Controls (RW, Play/Pause, FF)
    GtkWidget *controls_hbox = gtk_hbox_new(FALSE, btn_spacing);
    GtkWidget *controls_align = gtk_alignment_new(0.5, 0, 0, 0);
    gtk_container_add(GTK_CONTAINER(controls_align), controls_hbox);
    gtk_box_pack_start(GTK_BOX(mid_vbox), controls_align, FALSE, FALSE, 0);

    GtkWidget *rw_btn = create_button_from_icon(is_hires?fast_rewind_icon:fast_rewind_lr_icon, btn_padding);
    gtk_widget_set_size_request(rw_btn, btn_size, btn_size);
    g_signal_connect(rw_btn, "clicked", G_CALLBACK(on_rewind_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), rw_btn, FALSE, FALSE, 0);

    play_pause_btn = create_button_from_icon(is_hires?play_pause_icon:play_pause_lr_icon, btn_padding);
    gtk_widget_set_size_request(play_pause_btn, play_pause_width, btn_size);
    g_signal_connect(play_pause_btn, "clicked", G_CALLBACK(on_play_pause_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), play_pause_btn, FALSE, FALSE, 0);

    GtkWidget *ff_btn = create_button_from_icon(is_hires?fast_forward_icon:fast_forward_lr_icon, btn_padding);
    gtk_widget_set_size_request(ff_btn, btn_size, btn_size);
    g_signal_connect(ff_btn, "clicked", G_CALLBACK(on_ff_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), ff_btn, FALSE, FALSE, 0);

    GtkWidget *btn_bookmark = create_button_from_icon(is_hires?bookmark_icon:bookmark_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_bookmark, btn_size, btn_size);
    g_signal_connect(btn_bookmark, "clicked", G_CALLBACK(on_add_bookmark_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(controls_hbox), btn_bookmark, FALSE, FALSE, 0);

    // --- BOTTOM BUTTONS ---
    GtkWidget *bot_hbox = gtk_hbox_new(FALSE, bot_spacing);
    gtk_box_pack_start(GTK_BOX(main_vbox), bot_hbox, FALSE, FALSE, bot_padding);

    GtkWidget *btn_open = create_button_from_icon(is_hires?open_icon:open_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_open, btn_size, btn_size);
    g_signal_connect(btn_open, "clicked", G_CALLBACK(on_open_dialog_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bot_hbox), btn_open, FALSE, FALSE, side_padding);

    GtkWidget *btn_history = create_button_from_icon(is_hires?history_icon:history_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_history, btn_size, btn_size);
    g_signal_connect(btn_history, "clicked", G_CALLBACK(on_history_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bot_hbox), btn_history, FALSE, FALSE, 0);

    GtkWidget *btn_chapters = create_button_from_icon(is_hires?chapters_icon:chapters_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_chapters, btn_size, btn_size);
    g_signal_connect(btn_chapters, "clicked", G_CALLBACK(on_chapter_list_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bot_hbox), btn_chapters, FALSE, FALSE, 0);

    GtkWidget *btn_bookmark_list = create_button_from_icon(is_hires?bookmarklist_icon:bookmarklist_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_bookmark_list, btn_size, btn_size);
    g_signal_connect(btn_bookmark_list, "clicked", G_CALLBACK(on_bookmark_list_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bot_hbox), btn_bookmark_list, FALSE, FALSE, 0);

    GtkWidget *spacer = gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(bot_hbox), spacer, TRUE, TRUE, 0);

    GtkWidget *btn_light = create_button_from_icon(is_hires?sunny_icon:sunny_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_light, btn_size, btn_size);
    g_signal_connect(btn_light, "clicked", G_CALLBACK(on_fl_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bot_hbox), btn_light, FALSE, FALSE, 0);

    GtkWidget *btn_display = create_button_from_icon(is_hires?display_icon:display_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_display, btn_size, btn_size);
    g_signal_connect(btn_display, "clicked", G_CALLBACK(on_displayUpdate_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bot_hbox), btn_display, FALSE, FALSE, 0);

    GtkWidget *btn_bt = create_button_from_icon(is_hires?bluetooth_icon:bluetooth_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_bt, btn_size, btn_size);
    g_signal_connect(btn_bt, "clicked", G_CALLBACK(on_bluetooth_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bot_hbox), btn_bt, FALSE, FALSE, 0);

    GtkWidget *btn_close = create_button_from_icon(is_hires?close_icon:close_lr_icon, btn_padding);
    gtk_widget_set_size_request(btn_close, btn_size, btn_size);
    g_signal_connect(btn_close, "clicked", G_CALLBACK(on_close_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(bot_hbox), btn_close, FALSE, FALSE, side_padding);


    gtk_widget_show_all(window);

    if (!current_file.empty()) {
        on_file_open(current_file.c_str());
    }


    g_timeout_add(1000, update_ui, NULL);

    gtk_main();

    return 0;
}
