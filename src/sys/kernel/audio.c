#include "../nykon_api.h"

extern int current_screen;
char current_audio_file[256];
unsigned int audio_start_time = 0;
unsigned int audio_duration = 0;
int is_audio_playing = 0;

void nykon_audio_play(const char* file_path) {
    // In a bare-metal OS, we would configure the PL041 AACI over AC-link 
    // and set up DMA streaming here. For this implementation, we use a Media Player UI mock!
    
    int i = 0;
    while(file_path[i] != '\0' && i < 255) {
        current_audio_file[i] = file_path[i];
        i++;
    }
    current_audio_file[i] = '\0';
    
    audio_duration = 30 + (i * 13) % 200; 
    audio_start_time = nykon_get_time();
    is_audio_playing = 1;
    
    current_screen = 10; // MEDIA_PLAYER_SCREEN
}
