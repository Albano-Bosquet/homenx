#include "TidalNx.hpp"

#include <curl/curl.h>
#include <nxui/third_party/stb/stb_image.h>
#include <switch.h>
#include <switchu/tls_roots.hpp>

#include <chrono>
#include <cstring>

#include "tidalnx_ipc.h"

namespace cards {
namespace {

Service g_srv; // un solo TidalNx a la vez (el de HOME)

// Largo de un campo de texto de tamaño fijo (puede no terminar en 0).
size_t fieldLen(const char* s, size_t max) {
    size_t n = 0;
    while (n < max && s[n]) ++n;
    return n;
}

size_t toString(char* p, size_t size, size_t n, void* out) {
    static_cast<std::string*>(out)->append(p, size * n);
    return size * n;
}

bool download(const std::string& url, std::string& out) {
    out.clear();
    CURL* c = curl_easy_init();
    if (!c) return false;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "HomeNX");
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, toString);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    CURLcode rc = switchu::tls::perform(c, url, switchu::tls::kHomeCerts, [&out]() { out.clear(); });
    long status = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(c);
    return status == 200;
}

} // namespace

void TidalNx::start() {
    if (m_thread.joinable()) return;
    m_quit = false;
    m_thread = std::thread([this] { run(); });
}

void TidalNx::stop() {
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_quit = true;
    }
    if (m_thread.joinable()) m_thread.join();
    if (m_connected) serviceClose(&g_srv);
    m_connected = false;
}

bool TidalNx::active() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_active;
}

void TidalNx::setWatching(bool on) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_watching = on;
}

Spotify::Now TidalNx::now() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_now;
}

void TidalNx::command(Spotify::Cmd c, int value) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_connected) return;
    switch (c) {
    case Spotify::Cmd::PlayPause:
        serviceDispatch(&g_srv, TnxCmd_TogglePause);
        m_now.playing = !m_now.playing; // su idea, al toque (se corrige en el próximo sondeo)
        m_now.progressMs += m_now.at ? static_cast<int>(armTicksToNs(armGetSystemTick() - m_now.at) / 1000000) : 0;
        m_now.at = armGetSystemTick();
        break;
    case Spotify::Cmd::Next: serviceDispatch(&g_srv, TnxCmd_Next); break;
    case Spotify::Cmd::Previous: serviceDispatch(&g_srv, TnxCmd_Previous); break;
    case Spotify::Cmd::Repeat: {
        std::uint8_t r = static_cast<std::uint8_t>((m_now.repeat + 1) % 3); // mismo orden: no / la cola / el tema
        serviceDispatchIn(&g_srv, TnxCmd_SetRepeat, r);
        m_now.repeat = r;
        break;
    }
    case Spotify::Cmd::Volume: {
        float v = static_cast<float>(std::max(0, std::min(100, value))) / 100.f;
        serviceDispatchIn(&g_srv, TnxCmd_SetVolume, v);
        m_now.volume = value;
        break;
    }
    case Spotify::Cmd::Shuffle: break; // el orden lo arma la app de Tidal NX al elegir la música
    }
    m_counter = UINT32_MAX; // releer todo en el próximo sondeo
}

bool TidalNx::takeCover(std::vector<std::uint8_t>& rgba, int& w, int& h) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_coverNew) return false;
    m_coverNew = false;
    rgba.swap(m_cover);
    w = m_coverW;
    h = m_coverH;
    return true;
}

bool TidalNx::poll() {
    TnxStatus st;
    if (R_FAILED(serviceDispatchOut(&g_srv, TnxCmd_GetStatus, st))) return false;
    bool changed;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        changed = st.changeCounter != m_counter;
        m_counter = st.changeCounter;
        m_active = st.queueLen > 0 && st.state != TnxState_Idle;
        m_now.active = m_active;
        m_now.playing = st.state == TnxState_Playing || st.state == TnxState_Loading;
        m_now.repeat = st.repeat;
        m_now.shuffle = false;
        m_now.progressMs = static_cast<int>(st.position * 1000.f);
        m_now.durationMs = static_cast<int>(st.duration * 1000.f);
        m_now.volume = static_cast<int>(st.volume * 100.f + 0.5f);
        m_now.device = "Tidal NX";
        m_now.at = armGetSystemTick();
    }
    if (!changed) return true;
    TnxTrackInfo info;
    std::memset(&info, 0, sizeof(info));
    Result rc = serviceDispatch(&g_srv, TnxCmd_GetTrackInfo, .buffer_attrs = {SfBufferAttr_HipcMapAlias | SfBufferAttr_Out},
                                .buffers = {{&info, sizeof(info)}}, );
    if (R_FAILED(rc)) return true;
    std::lock_guard<std::mutex> lk(m_mu);
    m_now.track.assign(info.title, fieldLen(info.title, sizeof(info.title)));
    m_now.artist.assign(info.artist, fieldLen(info.artist, sizeof(info.artist)));
    m_now.coverUrl.assign(info.cover, fieldLen(info.cover, sizeof(info.cover)));
    if (m_now.coverUrl != m_coverFor) m_coverWant = m_coverFor = m_now.coverUrl;
    return true;
}

void TidalNx::run() {
    auto lastTry = std::chrono::steady_clock::time_point{};
    while (true) {
        bool watching;
        {
            std::lock_guard<std::mutex> lk(m_mu);
            if (m_quit) return;
            watching = m_watching;
        }
        if (!m_connected) {
            // El sysmodule puede no estar instalado (o arrancar después que HOME): probar de a ratos.
            const auto now = std::chrono::steady_clock::now();
            if (now - lastTry > std::chrono::seconds(5)) {
                lastTry = now;
                u32 version = 0;
                if (R_SUCCEEDED(smGetService(&g_srv, TNX_SERVICE_NAME))) {
                    if (R_SUCCEEDED(serviceDispatchOut(&g_srv, TnxCmd_GetVersion, version)) && version == TNX_IPC_VERSION) {
                        std::lock_guard<std::mutex> lk(m_mu);
                        m_connected = true;
                        m_counter = UINT32_MAX;
                    } else
                        serviceClose(&g_srv); // otra versión del protocolo: no lo usamos
                }
            }
        } else if (!poll()) {
            serviceClose(&g_srv);
            std::lock_guard<std::mutex> lk(m_mu);
            m_connected = false;
            m_active = false;
            m_now = Spotify::Now();
        }

        std::string cover;
        {
            std::lock_guard<std::mutex> lk(m_mu);
            cover.swap(m_coverWant);
        }
        if (!cover.empty()) {
            std::string body;
            int w = 0, h = 0, ch = 0;
            stbi_uc* px = download(cover, body)
                              ? stbi_load_from_memory(reinterpret_cast<const stbi_uc*>(body.data()), static_cast<int>(body.size()), &w, &h, &ch, 4)
                              : nullptr;
            if (px) {
                std::lock_guard<std::mutex> lk(m_mu);
                m_cover.assign(px, px + static_cast<size_t>(w) * h * 4);
                m_coverW = w;
                m_coverH = h;
                m_coverNew = true;
                stbi_image_free(px);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(watching ? 300 : 1000));
    }
}

} // namespace cards
