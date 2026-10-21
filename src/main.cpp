#include "app.hpp"
#include "path_utils.hpp"

#include <glib.h>

#include <filesystem>
#include <iostream>

namespace {
std::filesystem::path databasePath() {
    const char* base = g_get_user_data_dir();
    const auto directory = pathFromUtf8(base ? base : ".") / "GestionStock";
    std::filesystem::create_directories(directory);
    return directory / "gestion-stock.sqlite3";
}

void onActivate(GtkApplication*, gpointer data) {
    static_cast<App*>(data)->activate();
}
}

int main(int argc, char** argv) {
    try {
        auto* application = gtk_application_new("fr.gestionstock.desktop", G_APPLICATION_DEFAULT_FLAGS);
        App app(application, databasePath());
        g_signal_connect(application, "activate", G_CALLBACK(onActivate), &app);
        const int result = g_application_run(G_APPLICATION(application), argc, argv);
        g_object_unref(application);
        return result;
    } catch (const std::exception& error) {
        std::cerr << "Gestion des stocks : " << error.what() << '\n';
        return 1;
    }
}
