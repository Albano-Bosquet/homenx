#pragma once
#include "Spotify.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace cards {

// Tidal NX en el reproductor de HOME (fork local): lo que suena en el sysmodule tidalnx-sys
// (https://github.com/Albano-Bosquet/TidalNX, servicio IPC "tidalnx", ver tidalnx_ipc.h) y sus botones.
// A diferencia de Spotify, la música suena en la propia Switch y no hace falta iniciar sesión:
// todo es local, por IPC. Solo la carátula se baja de resources.tidal.com.
//
// Devuelve lo mismo que Spotify (Spotify::Now / Spotify::Cmd) para reusar el panel de HOME.
class TidalNx {
public:
    ~TidalNx() { stop(); }
    void start(); // su hilo: sondea el servicio (seguido si se está mirando, de a ratos si no)
    void stop();
    bool started() const { return m_thread.joinable(); }

    // Hay algo cargado en el sysmodule (sonando o en pausa): el reproductor de HOME lo muestra.
    bool active() const;
    void setWatching(bool on);
    Spotify::Now now() const;
    void command(Spotify::Cmd c, int value = 0);
    bool takeCover(std::vector<std::uint8_t>& rgba, int& w, int& h);

private:
    void run();
    bool poll(); // false si el servicio no responde

    mutable std::mutex m_mu;
    std::thread m_thread;
    bool m_quit = false, m_watching = false;
    bool m_connected = false, m_active = false;
    Spotify::Now m_now;
    std::uint32_t m_counter = UINT32_MAX;
    std::string m_coverFor, m_coverWant;
    std::vector<std::uint8_t> m_cover;
    int m_coverW = 0, m_coverH = 0;
    bool m_coverNew = false;
};

} // namespace cards
