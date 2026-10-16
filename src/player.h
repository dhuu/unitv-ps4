#pragma once
// Reprodutor baseado no libSceAvPlayer do PS4 (substitui o Media3/ExoPlayer do Android).
// Suporta HLS (.m3u8) e MP4 via HTTP/HTTPS. Vídeo NV12 convertido para a tela via CPU.
#include <string>
#include <cstdint>
#include <SDL2/SDL.h>

namespace player {

enum class State { Idle, Opening, Buffering, Playing, Paused, Ended, Error };

bool open(const std::string& url);
void close();
bool isOpen();
State state();
std::string errorText();

uint64_t positionMs();
uint64_t durationMs();      // 0 para ao vivo
bool hasVideoFrame();
void pause();
void resume();
void togglePause();
void seekTo(uint64_t ms);

// Atualiza o frame de vídeo (se houver novo) e desenha na superfície da tela.
void render(SDL_Surface* dst);

} // namespace player
