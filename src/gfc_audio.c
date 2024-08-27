#include "simple_logger.h"
#include "simple_json.h"

#include "gfc_pak.h"
#include "gfc_hashmap.h"
#include "gfc_audio.h"


typedef struct
{
    Uint32  max_sounds;
    GFC_Sound * sound_list;
    GFC_List  * sound_sequences;
}GFC_SoundManager;

static GFC_SoundManager sound_manager={0,NULL};

void gfc_sound_sequence_channel_callback(int channel);

void gfc_audio_close();
void gfc_sound_init(Uint32 max);

void gfc_audio_init(
    Uint32 maxGFC_Sounds,
    Uint32 channels,
    Uint32 channelGroups,
    Uint32 maxMusic,
    Uint8  enableMP3,
    Uint8  enableOgg)
{
    int flags = 0;

    if(Mix_OpenAudio(MIX_DEFAULT_FREQUENCY, MIX_DEFAULT_FORMAT, 2, 64)==-1)
    {
        slog("Failed to open audio: %s\n", SDL_GetError());
        return;
    }
    
    if (enableMP3)
    {
        flags |= MIX_INIT_MP3;
    }
    if (enableOgg)
    {
        flags |= MIX_INIT_OGG;
    }
    if (!(Mix_Init(flags) & flags))
    {
        slog("failed to initialize some audio support: %s",SDL_GetError());
    }
    atexit(Mix_Quit);
    atexit(gfc_audio_close);
    gfc_sound_init(maxGFC_Sounds);
}

void gfc_audio_close()
{
}

void gfc_sound_close()
{
    gfc_sound_clear_all();
    if (sound_manager.sound_list != NULL)
    {
        free(sound_manager.sound_list);
    }
    sound_manager.sound_list = NULL;
    sound_manager.max_sounds = 0;
}

void gfc_sound_init(Uint32 max)
{
    if (!max)
    {
        slog("cannot intialize a sound manager for Zero sounds!");
        return;
    }
    sound_manager.max_sounds = max;
    sound_manager.sound_list = gfc_allocate_array(sizeof(GFC_Sound),max);
    sound_manager.sound_sequences = gfc_list_new();
    Mix_ChannelFinished(gfc_sound_sequence_channel_callback);
    atexit(gfc_sound_close);
}

void gfc_sound_delete(GFC_Sound *sound)
{
    if (!sound)return;
    if (sound->sound != NULL)
    {
        Mix_FreeChunk(sound->sound);
    }    
    memset(sound,0,sizeof(GFC_Sound));//clean up all other data
}

void gfc_sound_free(GFC_Sound *sound)
{
    if (!sound) return;
    sound->ref_count--;
}

void gfc_sound_clear_all()
{
    int i;
    for (i = 0;i < sound_manager.max_sounds;i++)
    {
        gfc_sound_delete(&sound_manager.sound_list[i]);// clean up the data
    }
}

GFC_Sound *gfc_sound_new()
{
    int i;
    /*search for an unused sound address*/
    for (i = 0;i < sound_manager.max_sounds;i++)
    {
        if ((sound_manager.sound_list[i].ref_count == 0)&&(sound_manager.sound_list[i].sound == NULL))
        {
            sound_manager.sound_list[i].ref_count = 1;//set ref count
            return &sound_manager.sound_list[i];//return address of this array element        }
        }
    }
    /*find an unused sound address and clean up the old data*/
    for (i = 0;i < sound_manager.max_sounds;i++)
    {
        if (sound_manager.sound_list[i].ref_count == 0)
        {
            gfc_sound_delete(&sound_manager.sound_list[i]);// clean up the old data
            sound_manager.sound_list[i].ref_count = 1;//set ref count
            return &sound_manager.sound_list[i];//return address of this array element
        }
    }
    slog("error: out of sound addresses");
    return NULL;
}

GFC_Sound *gfc_sound_get_by_filename(const char * filename)
{
    int i;
    for (i = 0;i < sound_manager.max_sounds;i++)
    {
        if (gfc_line_cmp(sound_manager.sound_list[i].filepath,filename)==0)
        {
            return &sound_manager.sound_list[i];
        }
    }
    return NULL;// not found
}

GFC_Sound *gfc_sound_load(const char *filename,float volume,int defaultChannel)
{
    GFC_Sound *sound;
    if (!filename)return NULL;
    if (strlen(filename) == 0)return NULL;
    sound = gfc_sound_get_by_filename(filename);
    if (sound)
    {
        sound->ref_count++;
        return sound;
    }
    sound = gfc_sound_new();
    if (!sound)
    {
        return NULL;
    }
    sound->sound = Mix_LoadWAV(filename);
    if (!sound->sound)
    {
        slog("failed to load sound file %s",filename);
        gfc_sound_free(sound);
        return NULL;
    }
    sound->volume = volume;
    sound->defaultChannel = defaultChannel;
    gfc_line_cpy(sound->filepath,filename);
    return sound;
}

void gfc_sound_play(GFC_Sound *sound,int loops,float volume,int channel,int group)
{
    int chan;
    float netVolume = 1;
    if (!sound)return;
    if (!sound->sound)return;
    if (volume > 0)
    {
        netVolume *= volume;
    }
    if (channel >= 0)
    {
        chan = channel;
    }
    else
    {
        chan = sound->defaultChannel;
    }
    Mix_VolumeChunk(sound->sound, (int)(netVolume * MIX_MAX_VOLUME));
    Mix_PlayChannel(chan, sound->sound, loops);

}

void gfc_sound_pack_play(GFC_HashMap *pack, const char *name,int loops,float volume,int channel,int group)
{
    GFC_Sound *sound;
    if ((!pack)||(!name))return;
    sound = gfc_hashmap_get(pack,name);
    if (!sound)return;
    gfc_sound_play(sound,loops,volume,channel,group);
}


void gfc_sound_pack_load_sound(GFC_HashMap *pack, const char *name,const char *file)
{
    GFC_Sound *sound;
    if ((!pack)||(!name)||(!file))return;
    sound = gfc_hashmap_get(pack,name);
    if (sound)
    {
        gfc_sound_free(sound);//delete the old one
        gfc_hashmap_delete_by_key(pack,name);
    }
    sound = gfc_sound_load(file,1,-1);
    if (!sound)return;
    gfc_hashmap_insert(pack,name,sound);
}

void gfc_sound_pack_free(GFC_HashMap *pack)
{
    if (!pack)return;
    gfc_hashmap_foreach(pack, (gfc_work_func*)gfc_sound_free);
    gfc_hashmap_free(pack);
}

GFC_HashMap *gfc_sound_pack_parse_file(const char *filename)
{
    SJson *file;
    SJson *sounds;
    GFC_HashMap *pack;
    if (!filename)return NULL;
    file = gfc_pak_load_json(filename);
    if (!file)return NULL;
    
    sounds = sj_object_get_value(file,"sounds");
    if (!sounds)
    {
        slog("failed to parse sound pack file, no 'sounds' object");
        sj_free(file);
        return NULL;
    }
    pack = gfc_sound_pack_parse(sounds);
    sj_free(file);
    return pack;
}

GFC_HashMap *gfc_sound_pack_parse(SJson *sounds)
{
    int i,c;
    const char *name;
    const char *text;
    SJson *sound;
    GFC_HashMap *pack = NULL;
    if (!sounds)return NULL;
    
    c = sj_array_get_count(sounds);
    if (!c)return NULL;
    pack = gfc_hashmap_new();
    if (!pack)return NULL;
    for (i = 0; i < c; i++)
    {
        sound = sj_array_get_nth(sounds,i);
        if (!sound)continue;
        name = sj_get_string_value(sj_object_get_value(sound,"name"));
        text = sj_get_string_value(sj_object_get_value(sound,"file"));
        gfc_sound_pack_load_sound(pack, name,text);
    }
    return pack;
}

void gfc_sound_sequence_free(GFC_SoundSequence *sequence)
{
    if (!sequence)return;
    if (sequence->sequence)gfc_list_delete(sequence->sequence);
    free(sequence);
}

GFC_SoundSequence *gfc_sound_sequence_new()
{
    GFC_SoundSequence *sequence;
    sequence = gfc_allocate_array(sizeof(GFC_SoundSequence),1);
    if (!sequence)return NULL;
    sequence->sequence = gfc_list_new();
    return sequence;
}

void gfc_sound_queue_sequence(GFC_List *sounds,int channel)
{
    GFC_SoundSequence *sequence;
    if (!sounds)return;
    sequence = gfc_sound_sequence_new();
    if (!sequence)return;
    sequence->channel = channel;
    sequence->sequence = gfc_list_copy(sounds);
    gfc_list_append(sound_manager.sound_sequences,sequence);
    if (!Mix_Playing(channel))
    {
        gfc_sound_sequence_channel_callback(channel);
    }
}

void gfc_sound_sequence_channel_callback(int channel)
{
    GFC_Sound *sound;
    GFC_SoundSequence *sequence;
    int i,c;
    c = gfc_list_get_count(sound_manager.sound_sequences);
    for (i = 0; i < c;i++)
    {
        sequence = gfc_list_get_nth(sound_manager.sound_sequences,i);
        if (!sequence)continue;
        if (sequence->channel != channel)continue;
        sound = gfc_list_get_nth(sequence->sequence,sequence->current);
        if (!sound)continue;
        sequence->current++;
        gfc_sound_play(sound,0,sound->volume,channel,-1);
        if (sequence->current >= gfc_list_get_count(sequence->sequence))//we are finished with this sequence
        {
            gfc_list_delete_data(sound_manager.sound_sequences,sequence);
            gfc_sound_sequence_free(sequence);
        }
        return;
    }
}

Mix_Music *gfc_sound_load_music(const char *filename)
{
    Mix_Music *music;
    music = Mix_LoadMUS(filename);
    return music;
}
/*eol@eof*/
