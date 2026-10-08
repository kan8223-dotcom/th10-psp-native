#pragma once
// TH10 recording (TH10_REC=1 only): SELECT starts and stops a Motion JPEG AVI
// (480x272, 15 fps, q50, 16-bit LPCM 22.05 kHz mono; the XMB-proven B0
// layout) written to ms0:/VIDEO/TH10/ while the game runs. JPEGs wait in a
// ring (ME eDRAM per psp/MeEdramMap.hpp, or Main RAM) and leave through two
// 64 KiB Main RAM write buffers. A red dot shows in
// the HUD's top-right corner while recording (not in the video).
// psp/Recorder.cpp is the SC side (control, capture bookkeeping, ME jobs,
// writer thread, log), psp/RecMeKernel.inl the ME side, psp/RecCore.hpp the
// portable AVI/mux code, psp/RecJpeg.hpp the encoder.
#include <cstdint>
extern "C" {
// native/main.cpp
void th10_rec_init(const char* game_dir);                              // once, before the game allocates
void th10_rec_tick(unsigned tick,unsigned late,unsigned presents);     // every tick, after the pacing wait
void th10_rec_shutdown(void);                                          // stop and finish the file (exit, fail_stop)
// psp/GeRenderer.cpp, inside Renderer::commit before the frame's FINISH
void* th10_rec_capture_want(void);              // capture buffer for this frame, or nullptr
void th10_rec_capture_emitted(unsigned fence);  // the copy is in the list that closes as FINISH `fence`
void th10_rec_capture_skipped(void);            // wanted, but the list had no room
int th10_rec_indicator(void);                   // 1: draw the red dot
int th10_rec_ge_done(unsigned fence);           // (GeRenderer.cpp) FINISH `fence` has run
// native/HeadlessFiles.cpp (game thread): every game file read, its size and time
void th10_rec_game_read(unsigned bytes,unsigned us);
// The BGM side (any thread): pause our M2 writes around its big read. While
// paused, frames keep going into the JPEG ring (dropped, and counted, when it
// is full) and audio into the audio ring; paused() is 1 once no write of ours
// is in flight. Longest pause and counts go to the recording's log.
void th10_rec_writer_pause(int pause);
int th10_rec_writer_paused(void);
// psp/MeAudio.cpp
void th10_rec_audio_block(const int16_t* stereo);   // (audio thread) the 1024-frame 44.1 kHz stereo block being played
int th10_rec_me_ready(void);
int th10_rec_audio_running(void);
const char* th10_rec_me_state(void);
void th10_rec_audio_counters(unsigned* out);   // underruns, stretched, dropped, me_jobs, sc_jobs, level_min, started, 0
void th10_rec_audio_level_reset(void);
}
