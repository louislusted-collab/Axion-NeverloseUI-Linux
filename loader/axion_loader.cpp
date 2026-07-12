#include <gtk/gtk.h>

#include <array>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace
{
struct LoaderState
{
    GtkWidget* status = nullptr;
    GtkWidget* inject_button = nullptr;
    GtkWidget* update_button = nullptr;
    std::filesystem::path root;
};

std::filesystem::path ExecutableDirectory()
{
    std::array<char, 4096> path{};
    const ssize_t length = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if (length <= 0)
        return std::filesystem::current_path();

    path[static_cast<std::size_t>(length)] = '\0';
    return std::filesystem::path(path.data()).parent_path();
}

void SetStatus(LoaderState* state, const char* text, const char* css_class)
{
    gtk_label_set_text(GTK_LABEL(state->status), text);
    gtk_widget_remove_css_class(state->status, "status-ready");
    gtk_widget_remove_css_class(state->status, "status-working");
    gtk_widget_remove_css_class(state->status, "status-error");
    gtk_widget_add_css_class(state->status, css_class);
}

void SetButtonsSensitive(LoaderState* state, bool sensitive)
{
    gtk_widget_set_sensitive(state->inject_button, sensitive);
    gtk_widget_set_sensitive(state->update_button, sensitive);
}

void InjectionFinished(GObject* source, GAsyncResult* result, gpointer user_data)
{
    auto* state = static_cast<LoaderState*>(user_data);
    gchar* output = nullptr;
    gchar* error_output = nullptr;
    GError* error = nullptr;

    const gboolean communicated = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), result, &output, &error_output, &error);
    const gboolean succeeded = communicated && g_subprocess_get_successful(G_SUBPROCESS(source));

    if (succeeded) {
        SetStatus(state, "Injected — Insert toggles the menu", "status-ready");
    } else {
        const char* message = error ? error->message : error_output;
        SetStatus(state, (message && *message) ? message : "Injection failed — check CS2 and logs", "status-error");
    }

    SetButtonsSensitive(state, true);
    gtk_button_set_label(GTK_BUTTON(state->inject_button), "Inject");
    g_clear_error(&error);
    g_free(output);
    g_free(error_output);
    g_object_unref(source);
}

void InjectClicked(GtkButton*, gpointer user_data)
{
    auto* state = static_cast<LoaderState*>(user_data);
    const auto injector = state->root / "run_inject.sh";
    const auto library = state->root / "cs2_axion.so";

    if (!std::filesystem::exists(injector)) {
        SetStatus(state, "inject.sh is missing", "status-error");
        return;
    }
    if (!std::filesystem::exists(library)) {
        SetStatus(state, "Build first: make", "status-error");
        return;
    }

    GError* error = nullptr;
    GSubprocess* process = g_subprocess_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error,
        injector.c_str(),
        nullptr);

    if (!process) {
        SetStatus(state, error ? error->message : "Could not start injector", "status-error");
        g_clear_error(&error);
        return;
    }

    SetButtonsSensitive(state, false);
    gtk_button_set_label(GTK_BUTTON(state->inject_button), "Injecting…");
    SetStatus(state, "Waiting for authorization…", "status-working");
    g_subprocess_communicate_utf8_async(process, nullptr, nullptr, InjectionFinished, state);
}

void UpdateFinished(GObject* source, GAsyncResult* result, gpointer user_data)
{
    auto* state = static_cast<LoaderState*>(user_data);
    gchar* output = nullptr;
    gchar* error_output = nullptr;
    GError* error = nullptr;

    const gboolean communicated = g_subprocess_communicate_utf8_finish(
        G_SUBPROCESS(source), result, &output, &error_output, &error);
    const gboolean succeeded = communicated && g_subprocess_get_successful(G_SUBPROCESS(source));

    if (succeeded) {
        const char* message = (output && *output) ? output : "Native Linux manifest is current";
        SetStatus(state, message, "status-ready");
    } else {
        const char* message = error ? error->message : error_output;
        SetStatus(state, (message && *message) ? message : "Signature update failed", "status-error");
    }

    SetButtonsSensitive(state, true);
    gtk_button_set_label(GTK_BUTTON(state->update_button), "Update full Linux dump");
    g_clear_error(&error);
    g_free(output);
    g_free(error_output);
    g_object_unref(source);
}

void UpdateClicked(GtkButton*, gpointer user_data)
{
    auto* state = static_cast<LoaderState*>(user_data);
    const auto updater = state->root / "tools" / "update_offsets.sh";

    if (!std::filesystem::exists(updater)) {
        SetStatus(state, "Updater is missing", "status-error");
        return;
    }

    GError* error = nullptr;
    GSubprocess* process = g_subprocess_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE),
        &error,
        updater.c_str(),
        nullptr);

    if (!process) {
        SetStatus(state, error ? error->message : "Could not start updater", "status-error");
        g_clear_error(&error);
        return;
    }

    SetButtonsSensitive(state, false);
    gtk_button_set_label(GTK_BUTTON(state->update_button), "Dumping…");
    SetStatus(state, "Updating signatures, offsets, interfaces, and schemas…", "status-working");
    g_subprocess_communicate_utf8_async(process, nullptr, nullptr, UpdateFinished, state);
}

void Activate(GtkApplication* application, gpointer user_data)
{
    auto* state = static_cast<LoaderState*>(user_data);

    GtkCssProvider* css = gtk_css_provider_new();
    gtk_css_provider_load_from_string(css,
        "window { background: #0c0d10; }"
        ".card { background: #12151b; border: 1px solid #252b35; border-radius: 14px; padding: 28px; }"
        ".title { color: #f7f9fc; font-size: 26px; font-weight: 800; }"
        ".subtitle { color: #7f8a9b; font-size: 13px; }"
        ".status-ready { color: #42d392; }"
        ".status-working { color: #43a6ff; }"
        ".status-error { color: #ff657a; }"
        ".inject { color: white; background: #168eea; border-radius: 8px; padding: 11px 28px; font-weight: 700; }"
        ".inject:hover { background: #29a2ff; }"
        ".inject:disabled { background: #263342; color: #8290a3; }"
        ".update { color: #a9b7c9; background: #1b2029; border: 1px solid #303846; border-radius: 8px; padding: 9px 20px; font-weight: 600; }"
        ".update:hover { background: #252d39; color: white; }"
        ".update:disabled { color: #667386; background: #171b22; }");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(), GTK_STYLE_PROVIDER(css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(css);

    GtkWidget* window = gtk_application_window_new(application);
    gtk_window_set_title(GTK_WINDOW(window), "Axion Loader");
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 300);
    gtk_window_set_resizable(GTK_WINDOW(window), false);

    GtkWidget* outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_margin_start(outer, 18);
    gtk_widget_set_margin_end(outer, 18);
    gtk_widget_set_margin_top(outer, 18);
    gtk_widget_set_margin_bottom(outer, 18);
    gtk_window_set_child(GTK_WINDOW(window), outer);

    GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(card, "card");
    gtk_widget_set_vexpand(card, true);
    gtk_box_append(GTK_BOX(outer), card);

    GtkWidget* title = gtk_label_new("AXION");
    gtk_widget_add_css_class(title, "title");
    gtk_label_set_xalign(GTK_LABEL(title), 0.f);
    gtk_box_append(GTK_BOX(card), title);

    GtkWidget* subtitle = gtk_label_new("Native Linux loader");
    gtk_widget_add_css_class(subtitle, "subtitle");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.f);
    gtk_box_append(GTK_BOX(card), subtitle);

    state->status = gtk_label_new("Ready — start CS2 normally first");
    gtk_widget_add_css_class(state->status, "status-ready");
    gtk_label_set_xalign(GTK_LABEL(state->status), 0.f);
    gtk_label_set_ellipsize(GTK_LABEL(state->status), PANGO_ELLIPSIZE_END);
    gtk_widget_set_vexpand(state->status, true);
    gtk_widget_set_valign(state->status, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(card), state->status);

    state->update_button = gtk_button_new_with_label("Update full Linux dump");
    gtk_widget_add_css_class(state->update_button, "update");
    gtk_widget_set_halign(state->update_button, GTK_ALIGN_FILL);
    g_signal_connect(state->update_button, "clicked", G_CALLBACK(UpdateClicked), state);
    gtk_box_append(GTK_BOX(card), state->update_button);

    state->inject_button = gtk_button_new_with_label("Inject");
    gtk_widget_add_css_class(state->inject_button, "inject");
    gtk_widget_set_halign(state->inject_button, GTK_ALIGN_FILL);
    g_signal_connect(state->inject_button, "clicked", G_CALLBACK(InjectClicked), state);
    gtk_box_append(GTK_BOX(card), state->inject_button);

    gtk_window_present(GTK_WINDOW(window));
}
} // namespace

int main(int argc, char** argv)
{
    LoaderState state;
    state.root = ExecutableDirectory();

    GtkApplication* application = gtk_application_new("io.axion.loader", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(application, "activate", G_CALLBACK(Activate), &state);
    const int result = g_application_run(G_APPLICATION(application), argc, argv);
    g_object_unref(application);
    return result;
}
