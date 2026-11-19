#pragma once

// Protocolo IPC del servicio "tidalnx" (sysmodule tidalnx-sys). Lo comparten el sysmodule, el
// overlay de Tesla y la app. C plano para poder incluirlo desde C y C++.
//
// Comandos CMIF (sin dominios):
//   0  GetVersion      -> u32 TNX_IPC_VERSION
//   1  GetStatus       -> TnxStatus (inline)
//   2  GetTrackInfo    -> TnxTrackInfo en un buffer de salida (HipcMapAlias | Out)
//   3  TogglePause
//   4  Play            (reanuda; si estaba detenido, arranca la cola)
//   5  Pause
//   6  Next
//   7  Previous        (si ya sonaron más de 3 s, vuelve al principio del tema)
//   8  SetVolume       <- float 0..1
//   9  Stop            (corta y se queda quieto hasta Play)
//  10  SetQueue        <- buffer de entrada (HipcMapAlias | In): TnxQueueHeader + TnxQueueEntry[count]
//  11  SetTrackUrl     <- s64 trackId; buffer de entrada: URL (texto, sin terminar en 0 hace falta)
//                         Una URL que empieza con "error:" marca el tema como fallido con ese mensaje.
//  12  Seek            <- float segundos
//  13  SetRepeat       <- u8 TnxRepeat_*
//
// URLs: el sysmodule no habla con la API de Tidal (todavía). Cuando necesita la URL de un tema
// (el actual o los dos siguientes, y las URLs del CDN vencen a la hora), lo dice en
// TnxStatus.needUrlTrack; la app la pide y la manda con SetTrackUrl.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TNX_SERVICE_NAME "tidalnx"
#define TNX_IPC_VERSION 3
#define TNX_MAX_QUEUE 100

enum
{
    TnxCmd_GetVersion = 0,
    TnxCmd_GetStatus = 1,
    TnxCmd_GetTrackInfo = 2,
    TnxCmd_TogglePause = 3,
    TnxCmd_Play = 4,
    TnxCmd_Pause = 5,
    TnxCmd_Next = 6,
    TnxCmd_Previous = 7,
    TnxCmd_SetVolume = 8,
    TnxCmd_Stop = 9,
    TnxCmd_SetQueue = 10,
    TnxCmd_SetTrackUrl = 11,
    TnxCmd_Seek = 12,
    TnxCmd_SetRepeat = 13,
};

enum
{
    TnxState_Idle = 0,     // cola vacía o detenido
    TnxState_Loading = 1,  // esperando la URL, conectando o leyendo el moov
    TnxState_Playing = 2,
    TnxState_Paused = 3,
    TnxState_Error = 4,    // el último tema falló; ver TnxTrackInfo.error
};

enum
{
    TnxRepeat_Off = 0,  // al terminar la cola, se detiene
    TnxRepeat_All = 1,
    TnxRepeat_One = 2,
};

// Entra inline en la respuesta (el mensaje IPC entero vive en 0x100 bytes de TLS).
typedef struct
{
    uint8_t state;
    uint8_t repeat;
    uint8_t reserved[2];
    uint32_t index;          // tema actual (0-based)
    uint32_t queueLen;
    uint32_t changeCounter;  // sube cuando cambia el tema, el estado, la cola o el volumen
    float position;          // segundos
    float duration;          // segundos
    float volume;            // 0..1
    uint32_t reserved2;
    int64_t needUrlTrack;    // tema (id de Tidal) que necesita URL; 0 si ninguno
    int64_t trackId;         // id de Tidal del tema actual (0 si es un archivo sin id)
} TnxStatus;

typedef struct
{
    char title[128];
    char artist[96];
    char error[160];
    char cover[128];   // URL de la carátula (https://resources.tidal.com/...), o vacío
} TnxTrackInfo;

enum
{
    // Si el tema de la posición `start` es el que ya está sonando, no lo corta (para mezclar o
    // reordenar la cola sin interrumpir).
    TnxQueueFlag_KeepCurrent = 1 << 0,
};

typedef struct
{
    uint32_t count;
    uint32_t start;   // índice por el que arrancar
    uint8_t repeat;   // TnxRepeat_*
    uint8_t reserved[3];
    uint32_t flags;   // TnxQueueFlag_*
} TnxQueueHeader;

typedef struct
{
    int64_t trackId;  // id de Tidal (0 si es solo un archivo)
    float duration;   // segundos (para mostrar antes de abrirlo)
    uint32_t reserved;
    char title[96];
    char artist[64];
    char path[256];   // archivo AAC (.m4a) en la SD ("/config/..." o "sdmc:/..."); vacío = stream
    char cover[128];  // URL de la carátula, o vacío
} TnxQueueEntry;

#ifdef __cplusplus
}
#endif
