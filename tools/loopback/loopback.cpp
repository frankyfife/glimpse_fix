// WASAPI loopback recorder: loopback.exe <seconds> <out.wav> - records the default render device mix
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <vector>

int main(int argc, char** argv)
{
    int secs = argc > 1 ? atoi(argv[1]) : 5;
    const char* out = argc > 2 ? argv[2] : "loop.wav";
    CoInitialize(NULL);
    IMMDeviceEnumerator* en = NULL;
    IMMDevice* dev = NULL;
    IAudioClient* ac = NULL;
    IAudioCaptureClient* cap = NULL;
    WAVEFORMATEX* fmt = NULL;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)) ||
        FAILED(en->GetDefaultAudioEndpoint(eRender, eConsole, &dev)) ||
        FAILED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&ac)) || FAILED(ac->GetMixFormat(&fmt)) ||
        FAILED(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, fmt, NULL)) ||
        FAILED(ac->GetService(__uuidof(IAudioCaptureClient), (void**)&cap))) {
        printf("init failed\n");
        return 1;
    }
    DWORD mask = fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE ? ((WAVEFORMATEXTENSIBLE*)fmt)->dwChannelMask : 0;
    printf("mix format: %d ch, %lu Hz, %d bit, tag %#x, mask %#lx\n", fmt->nChannels, fmt->nSamplesPerSec, fmt->wBitsPerSample,
           fmt->wFormatTag, mask);
    std::vector<BYTE> data;
    ac->Start();
    DWORD end = GetTickCount() + secs * 1000;
    while (GetTickCount() < end) {
        Sleep(10);
        UINT32 n;
        while (SUCCEEDED(cap->GetNextPacketSize(&n)) && n) {
            BYTE* p;
            UINT32 frames;
            DWORD flags;
            cap->GetBuffer(&p, &frames, &flags, NULL, NULL);
            size_t bytes = frames * fmt->nBlockAlign;
            size_t at = data.size();
            data.resize(at + bytes);
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) memset(&data[at], 0, bytes);
            else memcpy(&data[at], p, bytes);
            cap->ReleaseBuffer(frames);
        }
    }
    ac->Stop();
    FILE* f = fopen(out, "wb");
    DWORD fmtSize = sizeof(WAVEFORMATEX) + fmt->cbSize, dataSize = (DWORD)data.size(), riff = 4 + 8 + fmtSize + 8 + dataSize;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtSize, 4, 1, f);
    fwrite(fmt, 1, fmtSize, f); fwrite("data", 1, 4, f); fwrite(&dataSize, 4, 1, f); fwrite(data.data(), 1, dataSize, f);
    fclose(f);
    printf("wrote %u bytes\n", dataSize);
    return 0;
}
