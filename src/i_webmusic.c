//
// Copyright(C) 1993-1996 Id Software, Inc.
// Copyright(C) 2005-2014 Simon Howard
// Copyright(C) 2018-2019 Viktor Lázár
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// DESCRIPTION:
//	System interface for music.
//


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "SDL.h"
#include "SDL_mixer.h"

#include "config.h"
#include "doomtype.h"
#include "memio.h"
#include "mus2mid.h"

#include "deh_str.h"
#include "i_sound.h"
#include "i_system.h"
#include "i_swap.h"
#include "m_argv.h"
#include "m_config.h"
#include "m_misc.h"
#include "sha1.h"
#include "w_wad.h"
#include "z_zone.h"

#include <emscripten.h>

#define MAXMIDLENGTH (96 * 1024)
#define MID_HEADER_MAGIC "MThd"
#define MUS_HEADER_MAGIC "MUS\x1a"

#define FLAC_HEADER "fLaC"
#define OGG_HEADER "OggS"

// Looping Vorbis metadata tag names. These have been defined by ZDoom
// for specifying the start and end positions for looping music tracks
// in .ogg and .flac files.
// More information is here: http://zdoom.org/wiki/Audio_loop
#define LOOP_START_TAG "LOOP_START"
#define LOOP_END_TAG   "LOOP_END"

// FLAC metadata headers that we care about.
#define FLAC_STREAMINFO      0
#define FLAC_VORBIS_COMMENT  4

// Ogg metadata headers that we care about.
#define OGG_ID_HEADER        1
#define OGG_COMMENT_HEADER   3

static music_module_t *music_module;
extern music_module_t music_opl_module;

// Structure for music substitution.
// We store a mapping based on SHA1 checksum -> filename of substitute music
// file to play, so that substitution occurs based on content rather than
// lump name. This has some inherent advantages:
//  * Music for Plutonia (reused from Doom 1) works automatically.
//  * If a PWAD replaces music, the replacement music is used rather than
//    the substitute music for the IWAD.
//  * If a PWAD reuses music from an IWAD (even from a different game), we get
//    the high quality version of the music automatically (neat!)

typedef struct
{
    sha1_digest_t hash;
    char *filename;
} subst_music_t;

// Structure containing parsed metadata read from a digital music track:
typedef struct
{
    boolean valid;
    unsigned int samplerate_hz;
    int start_time, end_time;
} file_metadata_t;

static subst_music_t *subst_music = NULL;
static unsigned int subst_music_len = 0;

static const char *subst_config_filenames[] =
{
    "doom1-music.cfg",
    "doom2-music.cfg",
    "tnt-music.cfg",
    "heretic-music.cfg",
    "hexen-music.cfg",
    "strife-music.cfg",
};

static boolean music_initialized = false;

// If this is true, this module initialized SDL sound and has the 
// responsibility to shut it down

static boolean sdl_was_initialized = false;

static boolean musicpaused = false;
static int current_music_volume;
static void *current_music_handle;
static boolean current_music_looping = false;
static boolean current_music_playing = false;

char *music_pack_path = ".";

// If true, we are playing a substitute digital track rather than in-WAD
// MIDI/MUS track, and file_metadata contains loop metadata.
static boolean playing_substitute = false;
static char *current_filename = NULL;
static file_metadata_t file_metadata;

// Position (in samples) that we have reached in the current track.
// This is updated by the TrackPositionCallback function.
// static unsigned int current_track_pos;

// If true, the currently playing track is being played on loop.
// static boolean current_track_loop;

// Given a time string (for LOOP_START/LOOP_END), parse it and return
// the time (in # samples since start of track) it represents.
static unsigned int ParseVorbisTime(unsigned int samplerate_hz, char *value)
{
    char *num_start, *p;
    unsigned int result = 0;
    char c;

    if (strchr(value, ':') == NULL)
    {
	return atoi(value);
    }

    result = 0;
    num_start = value;

    for (p = value; *p != '\0'; ++p)
    {
        if (*p == '.' || *p == ':')
        {
            c = *p; *p = '\0';
            result = result * 60 + atoi(num_start);
            num_start = p + 1;
            *p = c;
        }

        if (*p == '.')
        {
            return result * samplerate_hz
	         + (unsigned int) (atof(p) * samplerate_hz);
        }
    }

    return (result * 60 + atoi(num_start)) * samplerate_hz;
}

// Given a vorbis comment string (eg. "LOOP_START=12345"), set fields
// in the metadata structure as appropriate.
static void ParseVorbisComment(file_metadata_t *metadata, char *comment)
{
    char *eq, *key, *value;

    eq = strchr(comment, '=');

    if (eq == NULL)
    {
        return;
    }

    key = comment;
    *eq = '\0';
    value = eq + 1;

    if (!strcmp(key, LOOP_START_TAG))
    {
        metadata->start_time = ParseVorbisTime(metadata->samplerate_hz, value);
    }
    else if (!strcmp(key, LOOP_END_TAG))
    {
        metadata->end_time = ParseVorbisTime(metadata->samplerate_hz, value);
    }
}

// Parse a vorbis comments structure, reading from the given file.
static void ParseVorbisComments(file_metadata_t *metadata, FILE *fs)
{
    uint32_t buf;
    unsigned int num_comments, i, comment_len;
    char *comment;

    // We must have read the sample rate already from an earlier header.
    if (metadata->samplerate_hz == 0)
    {
	return;
    }

    // Skip the starting part we don't care about.
    if (fread(&buf, 4, 1, fs) < 1)
    {
        return;
    }
    if (fseek(fs, LONG(buf), SEEK_CUR) != 0)
    {
	return;
    }

    // Read count field for number of comments.
    if (fread(&buf, 4, 1, fs) < 1)
    {
        return;
    }
    num_comments = LONG(buf);

    // Read each individual comment.
    for (i = 0; i < num_comments; ++i)
    {
        // Read length of comment.
        if (fread(&buf, 4, 1, fs) < 1)
	{
            return;
	}

        comment_len = LONG(buf);

        // Read actual comment data into string buffer.
        comment = calloc(1, comment_len + 1);
        if (comment == NULL
         || fread(comment, 1, comment_len, fs) < comment_len)
        {
            free(comment);
            break;
        }

        // Parse comment string.
        ParseVorbisComment(metadata, comment);
        free(comment);
    }
}

static void ParseFlacStreaminfo(file_metadata_t *metadata, FILE *fs)
{
    byte buf[34];

    // Read block data.
    if (fread(buf, sizeof(buf), 1, fs) < 1)
    {
        return;
    }

    // We only care about sample rate and song length.
    metadata->samplerate_hz = (buf[10] << 12) | (buf[11] << 4)
                            | (buf[12] >> 4);
    // Song length is actually a 36 bit field, but 32 bits should be
    // enough for everybody.
    //metadata->song_length = (buf[14] << 24) | (buf[15] << 16)
    //                      | (buf[16] << 8) | buf[17];
}

static void ParseFlacFile(file_metadata_t *metadata, FILE *fs)
{
    byte header[4];
    unsigned int block_type;
    size_t block_len;
    boolean last_block;

    for (;;)
    {
        long pos = -1;

        // Read METADATA_BLOCK_HEADER:
        if (fread(header, 4, 1, fs) < 1)
        {
            return;
        }

        block_type = header[0] & ~0x80;
        last_block = (header[0] & 0x80) != 0;
        block_len = (header[1] << 16) | (header[2] << 8) | header[3];

        pos = ftell(fs);
        if (pos < 0)
        {
            return;
        }

        if (block_type == FLAC_STREAMINFO)
        {
            ParseFlacStreaminfo(metadata, fs);
        }
        else if (block_type == FLAC_VORBIS_COMMENT)
        {
            ParseVorbisComments(metadata, fs);
        }

        if (last_block)
        {
            break;
        }

        // Seek to start of next block.
        if (fseek(fs, pos + block_len, SEEK_SET) != 0)
        {
            return;
        }
    }
}

static void ParseOggIdHeader(file_metadata_t *metadata, FILE *fs)
{
    byte buf[21];

    if (fread(buf, sizeof(buf), 1, fs) < 1)
    {
        return;
    }

    metadata->samplerate_hz = (buf[8] << 24) | (buf[7] << 16)
                            | (buf[6] << 8) | buf[5];
}

static void ParseOggFile(file_metadata_t *metadata, FILE *fs)
{
    byte buf[7];
    unsigned int offset;

    // Scan through the start of the file looking for headers. They
    // begin '[byte]vorbis' where the byte value indicates header type.
    memset(buf, 0, sizeof(buf));

    for (offset = 0; offset < 100 * 1024; ++offset)
    {
	// buf[] is used as a sliding window. Each iteration, we
	// move the buffer one byte to the left and read an extra
	// byte onto the end.
        memmove(buf, buf + 1, sizeof(buf) - 1);

        if (fread(&buf[6], 1, 1, fs) < 1)
        {
            return;
        }

        if (!memcmp(buf + 1, "vorbis", 6))
        {
            switch (buf[0])
            {
                case OGG_ID_HEADER:
                    ParseOggIdHeader(metadata, fs);
                    break;
                case OGG_COMMENT_HEADER:
		    ParseVorbisComments(metadata, fs);
                    break;
                default:
                    break;
            }
        }
    }
}

EMSCRIPTEN_KEEPALIVE
void ReadLoopPoints(void)
{
    FILE *fs;
    char header[4];

    file_metadata_t *metadata = &file_metadata;
    char *filename = current_filename;

    metadata->valid = false;
    metadata->samplerate_hz = 0;
    metadata->start_time = 0;
    metadata->end_time = -1;

    fs = fopen(filename, "rb");

    if (fs == NULL)
    {
        return;
    }

    // Check for a recognized file format; use the first four bytes
    // of the file.

    if (fread(header, 4, 1, fs) < 1)
    {
        fclose(fs);
        return;
    }

    if (memcmp(header, FLAC_HEADER, 4) == 0)
    {
        ParseFlacFile(metadata, fs);
    }
    else if (memcmp(header, OGG_HEADER, 4) == 0)
    {
        ParseOggFile(metadata, fs);
    }

    fclose(fs);

    // Only valid if at the very least we read the sample rate.
    metadata->valid = metadata->samplerate_hz > 0;

    // If start and end time are both zero, ignore the loop tags.
    // This is consistent with other source ports.
    if (metadata->start_time == 0 && metadata->end_time == 0)
    {
        metadata->valid = false;
    }

    EM_ASM({
        if (window.doom_music){
            window.doom_music.loopStart = $0;
            window.doom_music.loopEnd = $1;
        }
    }, (double) file_metadata.start_time / file_metadata.samplerate_hz, (double) file_metadata.end_time / file_metadata.samplerate_hz);
}

// Given a MUS lump, look up a substitute MUS file to play instead
// (or NULL to just use normal MIDI playback).

static char *GetSubstituteMusicFile(void *data, size_t data_len)
{
    sha1_context_t context;
    sha1_digest_t hash;
    char *filename;
    unsigned int i;

    // Don't bother doing a hash if we're never going to find anything.
    if (subst_music_len == 0)
    {
        return NULL;
    }

    SHA1_Init(&context);
    SHA1_Update(&context, data, data_len);
    SHA1_Final(hash, &context);

    // Look for a hash that matches.
    // The substitute mapping list can (intentionally) contain multiple
    // filename mappings for the same hash. This allows us to try
    // different files and fall back if our first choice isn't found.

    filename = NULL;

    for (i = 0; i < subst_music_len; ++i)
    {
        if (memcmp(hash, subst_music[i].hash, sizeof(hash)) == 0)
        {
            filename = subst_music[i].filename;

            // If the file exists, then use this file in preference to
            // any fallbacks. But we always return a filename if it's
            // in the list, even if it's just so we can print an error
            // message to the user saying it doesn't exist.
            if (M_FileExists(filename))
            {
                break;
            }
        }
    }

    return filename;
}

// Add a substitute music file to the lookup list.

static void AddSubstituteMusic(subst_music_t *subst)
{
    ++subst_music_len;
    subst_music =
        I_Realloc(subst_music, sizeof(subst_music_t) * subst_music_len);
    memcpy(&subst_music[subst_music_len - 1], subst, sizeof(subst_music_t));
}

static int ParseHexDigit(char c)
{
    c = tolower(c);

    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    else if (c >= 'a' && c <= 'f')
    {
        return 10 + (c - 'a');
    }
    else
    {
        return -1;
    }
}

static char *GetFullPath(char *base_filename, char *path)
{
    char *basedir, *result;
    char *p;

    // Starting with directory separator means we have an absolute path,
    // so just return it.
    if (path[0] == DIR_SEPARATOR)
    {
        return M_StringDuplicate(path);
    }

#ifdef _WIN32
    // d:\path\...
    if (isalpha(path[0]) && path[1] == ':' && path[2] == DIR_SEPARATOR)
    {
        return M_StringDuplicate(path);
    }
#endif

    // Paths in the substitute filenames can contain Unix-style /
    // path separators, but we should convert this to the separator
    // for the native platform.
    path = M_StringReplace(path, "/", DIR_SEPARATOR_S);

    // Copy config filename and cut off the filename to just get the
    // parent dir.
    basedir = M_StringDuplicate(base_filename);
    p = strrchr(basedir, DIR_SEPARATOR);
    if (p != NULL)
    {
        p[1] = '\0';
        result = M_StringJoin(basedir, path, NULL);
    }
    else
    {
        result = M_StringDuplicate(path);
    }
    free(basedir);
    free(path);

    return result;
}

// Parse a line from substitute music configuration file; returns error
// message or NULL for no error.

static char *ParseSubstituteLine(char *filename, char *line)
{
    subst_music_t subst;
    char *p;
    int hash_index;

    // Strip out comments if present.
    p = strchr(line, '#');
    if (p != NULL)
    {
        while (p > line && isspace(*(p - 1)))
        {
            --p;
        }
        *p = '\0';
    }

    // Skip leading spaces.
    for (p = line; *p != '\0' && isspace(*p); ++p);

    // Empty line? This includes comment lines now that comments have
    // been stripped.
    if (*p == '\0')
    {
        return NULL;
    }

    // Read hash.
    hash_index = 0;
    while (*p != '\0' && *p != '=' && !isspace(*p))
    {
        int d1, d2;

        d1 = ParseHexDigit(p[0]);
        d2 = ParseHexDigit(p[1]);

        if (d1 < 0 || d2 < 0)
        {
            return "Invalid hex digit in SHA1 hash";
        }
        else if (hash_index >= sizeof(sha1_digest_t))
        {
            return "SHA1 hash too long";
        }

        subst.hash[hash_index] = (d1 << 4) | d2;
        ++hash_index;

        p += 2;
    }

    if (hash_index != sizeof(sha1_digest_t))
    {
        return "SHA1 hash too short";
    }

    // Skip spaces.
    for (; *p != '\0' && isspace(*p); ++p);

    if (*p != '=')
    {
        return "Expected '='";
    }

    ++p;

    // Skip spaces.
    for (; *p != '\0' && isspace(*p); ++p);

    // We're now at the filename. Cut off trailing space characters.
    while (strlen(p) > 0 && isspace(p[strlen(p) - 1]))
    {
        p[strlen(p) - 1] = '\0';
    }

    if (strlen(p) == 0)
    {
        return "No filename specified for music substitution";
    }

    // Expand full path and add to our database of substitutes.
    subst.filename = GetFullPath(filename, p);
    AddSubstituteMusic(&subst);

    return NULL;
}

// Read a substitute music configuration file.

static boolean ReadSubstituteConfig(char *filename)
{
    char *buffer;
    char *line;
    int linenum = 1;

    // This unnecessarily opens the file twice...
    if (!M_FileExists(filename))
    {
        return false;
    }

    M_ReadFile(filename, (byte **) &buffer);

    line = buffer;

    while (line != NULL)
    {
        char *error;
        char *next;

        // find end of line
        char *eol = strchr(line, '\n');
        if (eol != NULL)
        {
            // change the newline into NUL
            *eol = '\0';
            next = eol + 1;
        }
        else
        {
            // end of buffer
            next = NULL;
        }

        error = ParseSubstituteLine(filename, line);

        if (error != NULL)
        {
            fprintf(stderr, "%s:%i: Error: %s\n", filename, linenum, error);
        }

        ++linenum;
        line = next;
    }

    Z_Free(buffer);

    return true;
}

// Find substitute configs and try to load them.

static void LoadSubstituteConfigs(void)
{
    char *musicdir;
    char *path;
    unsigned int i;

    // We can configure the path to music packs using the music_pack_path
    // configuration variable. Otherwise we use the current directory, or
    // $configdir/music to look for .cfg files.
    if (strcmp(music_pack_path, "") != 0)
    {
        musicdir = M_StringJoin(music_pack_path, DIR_SEPARATOR_S, NULL);
    }
    else if (!strcmp(configdir, ""))
    {
        musicdir = M_StringDuplicate("");
    }
    else
    {
        musicdir = M_StringJoin(configdir, "music", DIR_SEPARATOR_S, NULL);
    }

    // Load all music packs. We always load all music substitution packs for
    // all games. Why? Suppose we have a Doom PWAD that reuses some music from
    // Heretic. If we have the Heretic music pack loaded, then we get an
    // automatic substitution.
    for (i = 0; i < arrlen(subst_config_filenames); ++i)
    {
        path = M_StringJoin(musicdir, subst_config_filenames[i], NULL);
        ReadSubstituteConfig(path);
        free(path);
    }

    free(musicdir);

    if (subst_music_len > 0)
    {
        printf("Loaded %i music substitutions from config files.\n",
               subst_music_len);
    }
}

// Returns true if the given lump number is a music lump that should
// be included in substitute configs.
// Identifying music lumps by name is not feasible; some games (eg.
// Heretic, Hexen) don't have a common naming pattern for music lumps.

static boolean IsMusicLump(int lumpnum)
{
    byte *data;
    boolean result;

    if (W_LumpLength(lumpnum) < 4)
    {
        return false;
    }

    data = W_CacheLumpNum(lumpnum, PU_STATIC);

    result = memcmp(data, MUS_HEADER_MAGIC, 4) == 0
          || memcmp(data, MID_HEADER_MAGIC, 4) == 0;

    W_ReleaseLumpNum(lumpnum);

    return result;
}

// Dump an example config file containing checksums for all MIDI music
// found in the WAD directory.

static void DumpSubstituteConfig(char *filename)
{
    sha1_context_t context;
    sha1_digest_t digest;
    char name[9];
    byte *data;
    FILE *fs;
    unsigned int lumpnum;
    size_t h;

    fs = fopen(filename, "w");

    if (fs == NULL)
    {
        I_Error("Failed to open %s for writing", filename);
        return;
    }

    fprintf(fs, "# Example %s substitute MIDI file.\n\n", PACKAGE_NAME);
    fprintf(fs, "# SHA1 hash                              = filename\n");

    for (lumpnum = 0; lumpnum < numlumps; ++lumpnum)
    {
        strncpy(name, lumpinfo[lumpnum]->name, 8);
        name[8] = '\0';

        if (!IsMusicLump(lumpnum))
        {
            continue;
        }

        // Calculate hash.
        data = W_CacheLumpNum(lumpnum, PU_STATIC);
        SHA1_Init(&context);
        SHA1_Update(&context, data, W_LumpLength(lumpnum));
        SHA1_Final(digest, &context);
        W_ReleaseLumpNum(lumpnum);

        // Print line.
        for (h = 0; h < sizeof(sha1_digest_t); ++h)
        {
            fprintf(fs, "%02x", digest[h]);
        }

        fprintf(fs, " = %s.ogg\n", name);
    }

    fprintf(fs, "\n");
    fclose(fs);

    printf("Substitute MIDI config file written to %s.\n", filename);
    I_Quit();
}

static boolean SDLIsInitialized(void)
{
    int freq, channels;
    Uint16 format;

    return Mix_QuerySpec(&freq, &format, &channels) != 0;
}

// Initialize music subsystem
static boolean I_WEB_InitMusic(void)
{
    int i;

    //!
    // @category obscure
    // @arg <filename>
    //
    // Read all MIDI files from loaded WAD files, dump an example substitution
    // music config file to the specified filename and quit.
    //

    i = M_CheckParmWithArgs("-dumpsubstconfig", 1);

    if (i > 0)
    {
        DumpSubstituteConfig(myargv[i + 1]);
    }

    // If SDL_mixer is not initialized, we have to initialize it
    // and have the responsibility to shut it down later on.

    if (SDLIsInitialized())
    {
        music_initialized = true;
    }
    else
    {
        if (SDL_Init(SDL_INIT_AUDIO) < 0)
        {
            fprintf(stderr, "Unable to set up sound.\n");
        }
        else if (Mix_OpenAudio(snd_samplerate, AUDIO_S16SYS, 2, 1024) < 0)
        {
            fprintf(stderr, "Error initializing SDL_mixer: %s\n",
                    Mix_GetError());
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }
        else
        {
            SDL_PauseAudio(0);

            sdl_was_initialized = true;
            music_initialized = true;
        }
    }

    LoadSubstituteConfigs();
    music_module = &music_opl_module;
    music_module->Init();

    return music_initialized;
}

static void UpdateMusicVolume(void)
{
    int vol;

    if (musicpaused)
    {
        vol = 0;
    }
    else
    {
        vol = (current_music_volume * MIX_MAX_VOLUME) / 127;
    }

    EM_ASM({
        try{
            if (window.doom_music && window.doom_music_gain) window.doom_music_gain.gain.value = $0 / 255;
        }catch(err){}
    }, vol);
}

// Set music volume (0 - 127)

static void I_WEB_SetMusicVolume(int volume)
{
    // Internal state variable.
    current_music_volume = volume;

    if (playing_substitute)
        UpdateMusicVolume();
    else
        music_module->SetMusicVolume(volume);
}

// Start playing a mid

static void I_WEB_PlaySong(void *handle, boolean looping)
{
    current_music_looping = looping;
    if (playing_substitute)
    {
        current_music_playing = true;
        EM_ASM_({
            window.doom_music_looping = $0;
            if (window.doom_music_buffer){
                const context = SDL2.audioContext;
                const source = context.createBufferSource();
                source.buffer = window.doom_music_buffer;
                source.loop = !!window.doom_music_looping;
                const gain = context.createGain();
                source.connect(gain);
                gain.connect(context.destination);
                source.start(0);
                window.doom_music = source;
                window.doom_music_gain = gain;
                window.doom_music_offset = 0;
                window.doom_music_start = context.currentTime;
                Module._ReadLoopPoints();
            }
        }, looping);

        UpdateMusicVolume();
    }
    else
        music_module->PlaySong(handle, looping);
}

static void I_WEB_StopSong(void)
{
    if (playing_substitute)
    {
        current_music_playing = false;
        EM_ASM({
            try{
                if (window.doom_music){
                    (window.doom_music.stop || window.doom_music.noteOff).call(window.doom_music, 0);
                    window.doom_music.disconnect();
                    window.doom_music_gain.disconnect();
                    delete window.doom_music;
                    delete window.doom_music_gain;
                }
            }catch(err){}
        });
    }
    else
        music_module->StopSong();
}

static void I_WEB_PauseSong(void)
{
    if (playing_substitute)
    {
        I_WEB_StopSong();

        EM_ASM({
            try{
                window.doom_music_offset = SDL2.audioContext.currentTime - window.doom_music_start;
            }catch(err){}
        });
    }
    else
        music_module->PauseMusic();
}

static void I_WEB_ResumeSong(void)
{
    if (playing_substitute)
    {
        current_music_playing = true;
        EM_ASM_({
            if (window.doom_music_buffer){
                const context = SDL2.audioContext;
                const source = context.createBufferSource();
                source.buffer = window.doom_music_buffer;
                source.loop = !!window.doom_music_looping;
                const gain = context.createGain();
                source.connect(gain);
                gain.connect(context.destination);
                source.start(0, window.doom_music_offset);
                window.doom_music = source;
                window.doom_music_gain = gain;
                window.doom_music_start = context.currentTime - window.doom_music_offset;
                Module._ReadLoopPoints();
            }
        });

        UpdateMusicVolume();
    }
    else
        music_module->ResumeMusic();
}

static void I_WEB_UnRegisterSong(void *handle)
{
    if (playing_substitute)
    {
        current_music_playing = false;
        EM_ASM_({
            try{
                if (window.doom_music){
                    (window.doom_music.stop || window.doom_music.noteOff).call(window.doom_music, 0);
                    window.doom_music.disconnect();
                    window.doom_music_gain.disconnect();
                }
                delete window.doom_music;
                delete window.doom_music_buffer;
                delete window.doom_music_gain;
                delete window.doom_music_offset;
                delete window.doom_music_start;
                delete window.doom_music_looping;
                var filename = Module.Pointer_stringify($0);
                Module.FS.unlink("./" + filename);
            }catch(err){}
        }, current_filename);
    }
    else
        music_module->UnRegisterSong(handle);
}

static void *I_WEB_RegisterSong(void *data, int len)
{
    char *filename;

    if (!music_initialized)
    {
        return NULL;
    }

    playing_substitute = false;

    // See if we're substituting this MUS for a high-quality replacement.
    filename = GetSubstituteMusicFile(data, len);

    if (filename != NULL)
    {
        playing_substitute = true;
        current_filename = filename;
        EM_ASM_({
            var filename = Module.Pointer_stringify($0);
            window.doom_music_filename = filename;
            fetch(filename).then(function(res){ return res.arrayBuffer(); }).then(function(audio){
                var context = SDL2.audioContext;
                var path = "";
                filename.split("/").slice(1, -1).forEach(function(dir){
                    if (path) path += "/";
                    path += dir;
                    try{ Module.FS.mkdir(path); }catch(err){}
                });
                Module.FS.writeFile("./" + filename, new Uint8Array(audio));
                context.decodeAudioData(audio, function(buffer){
                    if (window.doom_music_filename == filename){
                        var source = context.createBufferSource();
                        source.buffer = buffer;
                        source.loop = !!window.doom_music_looping;
                        var gain = context.createGain();
                        source.connect(gain);
                        gain.connect(context.destination);
                        gain.gain.value = $1 / 255;
                        source.start(0);
                        window.doom_music_buffer = buffer;
                        window.doom_music = source;
                        window.doom_music_gain = gain;
                        window.doom_music_offset = 0;
                        window.doom_music_start = context.currentTime;
                        Module._ReadLoopPoints();
                    }
                });
            }).catch(function(){
                Module._I_WEB_RegisterSongFallback();
            });
        }, filename, (current_music_volume * MIX_MAX_VOLUME) / 127);
    }
    current_music_handle = music_module->RegisterSong(data, len);
    return current_music_handle;
}

// Shutdown music

static void I_WEB_ShutdownMusic(void)
{
    if (music_initialized)
    {
        music_initialized = false;
        I_WEB_StopSong();
        music_module->Shutdown();

        if (sdl_was_initialized)
        {
            Mix_CloseAudio();
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            sdl_was_initialized = false;
        }
    }
}

// Is the song playing?
static boolean I_WEB_MusicIsPlaying(void)
{
    if (!music_initialized)
    {
        return false;
    }

    if (playing_substitute)
        return EM_ASM_INT({ return +((!!window.doom_music) || $0); }, current_music_playing);
    else
        return music_module->MusicIsPlaying();
}

EMSCRIPTEN_KEEPALIVE
void I_WEB_RegisterSongFallback()
{
    boolean isPlaying = I_WEB_MusicIsPlaying();
    I_WEB_UnRegisterSong(current_music_handle);
    if (isPlaying){
        playing_substitute = false;
        I_WEB_PlaySong(current_music_handle, isPlaying);
    }
}

static snddevice_t music_web_devices[] =
{
    SNDDEVICE_WEBAUDIO,
    SNDDEVICE_SB,
};

music_module_t music_web_module =
{
    music_web_devices,
    arrlen(music_web_devices),
    I_WEB_InitMusic,
    I_WEB_ShutdownMusic,
    I_WEB_SetMusicVolume,
    I_WEB_PauseSong,
    I_WEB_ResumeSong,
    I_WEB_RegisterSong,
    I_WEB_UnRegisterSong,
    I_WEB_PlaySong,
    I_WEB_StopSong,
    I_WEB_MusicIsPlaying,
    NULL, // Poll
};

