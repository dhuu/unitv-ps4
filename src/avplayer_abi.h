#pragma once
// Declarações do libSceAvPlayer. O header do OpenOrbis (orbis/AvPlayer.h) só tem protótipos vazios,
// então as estruturas abaixo seguem a ABI documentada pelo projeto shadPS4 (avplayer.h).
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* SceAvPlayerHandle;

typedef enum {
    SCE_AVPLAYER_STATE_STOP = 0x01,
    SCE_AVPLAYER_STATE_READY = 0x02,
    SCE_AVPLAYER_STATE_PLAY = 0x03,
    SCE_AVPLAYER_STATE_PAUSE = 0x04,
    SCE_AVPLAYER_STATE_BUFFERING = 0x05,
    SCE_AVPLAYER_TIMED_TEXT_DELIVERY = 0x10,
    SCE_AVPLAYER_WARNING_ID = 0x20,
    SCE_AVPLAYER_ENCRYPTION = 0x30,
    SCE_AVPLAYER_DRM_ERROR = 0x40,
} SceAvPlayerEvents;

typedef enum {
    SCE_AVPLAYER_VIDEO = 0,
    SCE_AVPLAYER_AUDIO = 1,
    SCE_AVPLAYER_TIMEDTEXT = 2,
    SCE_AVPLAYER_UNKNOWN = 3,
} SceAvPlayerStreamType;

typedef void* (*SceAvPlayerAllocate)(void* p, uint32_t align, uint32_t size);
typedef void (*SceAvPlayerDeallocate)(void* p, void* mem);
typedef void* (*SceAvPlayerAllocateTexture)(void* p, uint32_t align, uint32_t size);
typedef void (*SceAvPlayerDeallocateTexture)(void* p, void* mem);

typedef struct {
    void* objectPointer;
    SceAvPlayerAllocate allocate;
    SceAvPlayerDeallocate deallocate;
    SceAvPlayerAllocateTexture allocateTexture;
    SceAvPlayerDeallocateTexture deallocateTexture;
} SceAvPlayerMemAllocator;

typedef int32_t (*SceAvPlayerOpenFile)(void* p, const char* name);
typedef int32_t (*SceAvPlayerCloseFile)(void* p);
typedef int32_t (*SceAvPlayerReadOffsetFile)(void* p, uint8_t* buf, uint64_t pos, uint32_t len);
typedef uint64_t (*SceAvPlayerSizeFile)(void* p);

typedef struct {
    void* objectPointer;
    SceAvPlayerOpenFile open;
    SceAvPlayerCloseFile close;
    SceAvPlayerReadOffsetFile readOffset;
    SceAvPlayerSizeFile size;
} SceAvPlayerFileReplacement;

typedef void (*SceAvPlayerEventCallback)(void* p, int32_t argEventId, int32_t argSourceId, void* argEventData);

typedef struct {
    void* objectPointer;
    SceAvPlayerEventCallback eventCallback;
} SceAvPlayerEventReplacement;

typedef struct {
    SceAvPlayerMemAllocator memoryReplacement;
    SceAvPlayerFileReplacement fileReplacement;
    SceAvPlayerEventReplacement eventReplacement;
    int32_t debugLevel;
    uint32_t basePriority;
    int32_t numOutputVideoFramebuffers;
    bool autoStart;
    uint8_t reserved[3];
    const char* defaultLanguage;
} SceAvPlayerInitData;

typedef struct {
    uint16_t channelCount;
    uint8_t reserved1[2];
    uint32_t sampleRate;
    uint32_t size;
    uint8_t languageCode[4];
} SceAvPlayerAudio;

typedef struct {
    uint32_t width;
    uint32_t height;
    float aspectRatio;
    uint8_t languageCode[4];
} SceAvPlayerVideo;

typedef struct {
    uint8_t languageCode[4];
    uint16_t textSize;
    uint16_t fontSize;
    uint16_t top, left, bottom, right;
} SceAvPlayerTimedText;

typedef union {
    uint8_t reserved[16];
    SceAvPlayerAudio audio;
    SceAvPlayerVideo video;
    SceAvPlayerTimedText subs;
} SceAvPlayerStreamDetails;

typedef struct {
    uint8_t* pData;
    uint8_t reserved[4];
    uint64_t timeStamp;
    SceAvPlayerStreamDetails details;
} SceAvPlayerFrameInfo;

typedef struct {
    uint32_t type;
    uint8_t reserved[4];
    SceAvPlayerStreamDetails details;
    uint64_t duration;
    uint64_t startTime;
} SceAvPlayerStreamInfo;

typedef struct {
    uint32_t width;
    uint32_t height;
    float aspectRatio;
    uint8_t languageCode[4];
    uint32_t framerate;
    uint32_t cropLeftOffset;
    uint32_t cropRightOffset;
    uint32_t cropTopOffset;
    uint32_t cropBottomOffset;
    uint32_t pitch;
    uint8_t lumaBitDepth;
    uint8_t chromaBitDepth;
    bool videoFullRangeFlag;
    uint8_t reserved1[37];
} SceAvPlayerVideoEx;

typedef union {
    SceAvPlayerVideoEx video;
    uint8_t reserved1[80];
} SceAvPlayerStreamDetailsEx;

typedef struct {
    void* pData;
    uint8_t reserved[4];
    uint64_t timeStamp;
    SceAvPlayerStreamDetailsEx details;
} SceAvPlayerFrameInfoEx;

SceAvPlayerHandle sceAvPlayerInit(SceAvPlayerInitData* pInit);
int32_t sceAvPlayerAddSource(SceAvPlayerHandle h, const char* filename);
int32_t sceAvPlayerClose(SceAvPlayerHandle h);
uint64_t sceAvPlayerCurrentTime(SceAvPlayerHandle h);
bool sceAvPlayerGetAudioData(SceAvPlayerHandle h, SceAvPlayerFrameInfo* frameInfo);
bool sceAvPlayerGetVideoDataEx(SceAvPlayerHandle h, SceAvPlayerFrameInfoEx* videoInfo);
int32_t sceAvPlayerGetStreamInfo(SceAvPlayerHandle h, uint32_t argStreamID, SceAvPlayerStreamInfo* argInfo);
bool sceAvPlayerIsActive(SceAvPlayerHandle h);
int32_t sceAvPlayerJumpToTime(SceAvPlayerHandle h, uint64_t jumpTimeMsec);
int32_t sceAvPlayerPause(SceAvPlayerHandle h);
int32_t sceAvPlayerResume(SceAvPlayerHandle h);
int32_t sceAvPlayerStart(SceAvPlayerHandle h);
int32_t sceAvPlayerStop(SceAvPlayerHandle h);
int32_t sceAvPlayerStreamCount(SceAvPlayerHandle h);

#ifdef __cplusplus
}
#endif
