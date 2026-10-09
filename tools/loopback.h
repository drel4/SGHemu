#ifndef SGH_LOOPBACK_H
#define SGH_LOOPBACK_H
/* Probe-only Windows 7 WASAPI loopback recorder. Not the emulated MA-3 clock. */
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mmreg.h>
#include <math.h>
/* C has no __uuidof; use the interface UUIDs from the Windows SDK headers. */
static const GUID loop_enumerator_class = {0xbcde0395,0xe52f,0x467c,{0x8e,0x3d,0xc4,0x57,0x92,0x91,0x69,0x2e}};
static const GUID loop_enumerator_iid = {0xa95664d2,0x9614,0x4f35,{0xa7,0x46,0xde,0x8d,0xb6,0x36,0x17,0xe6}};
static const GUID loop_client_iid = {0x1cb9ad4c,0xdbfa,0x4c32,{0xb1,0x78,0xc2,0xf5,0x68,0xa7,0x03,0xb2}};
static const GUID loop_capture_iid = {0xc8adbd64,0xe71e,0x48a0,{0xa4,0xde,0x18,0x5c,0x39,0x5c,0xd3,0x17}};
static int record_loopback(unsigned seconds)
{
    IMMDeviceEnumerator *enumerator = NULL;
    IMMDevice *device = NULL;
    IAudioClient *client = NULL;
    IAudioCaptureClient *capture = NULL;
    WAVEFORMATEX *format = NULL;
    FILE *wav = NULL;
    HRESULT hr;
    DWORD start, flags, riff_size, data_size = 0, fmt_size;
    UINT32 packets, frames;
    BYTE *data;
    double peak = 0;
    unsigned long long nonzero_samples = 0;
    int running = 0, result = 0, com = 0, float_samples;
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) goto done;
    com = 1;
    hr = CoCreateInstance(&loop_enumerator_class, NULL, CLSCTX_ALL, &loop_enumerator_iid, (void **)&enumerator);
    if (FAILED(hr)) goto done;
    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &device);
    if (FAILED(hr)) goto done;
    hr = IMMDevice_Activate(device, &loop_client_iid, CLSCTX_ALL, NULL, (void **)&client);
    if (FAILED(hr)) goto done;
    hr = IAudioClient_GetMixFormat(client, &format);
    if (FAILED(hr)) goto done;
    float_samples = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22 && ((WAVEFORMATEXTENSIBLE *)format)->SubFormat.Data1 == WAVE_FORMAT_IEEE_FLOAT);
    if (!((float_samples && format->wBitsPerSample == 32) || (!float_samples && format->wBitsPerSample == 16))) {
        fprintf(stderr, "Unsupported loopback mix format for probe\n"); goto done;
    }
    hr = IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, format, NULL);
    if (FAILED(hr)) goto done;
    hr = IAudioClient_GetService(client, &loop_capture_iid, (void **)&capture);
    if (FAILED(hr)) goto done;
    wav = fopen("build/ma5-loopback.wav", "wb"); if (!wav) goto done;
    fmt_size = sizeof(WAVEFORMATEX) + format->cbSize;
    riff_size = 0;
    fwrite("RIFF", 1, 4, wav); fwrite(&riff_size, 4, 1, wav); fwrite("WAVEfmt ", 1, 8, wav);
    fwrite(&fmt_size, 4, 1, wav); fwrite(format, 1, fmt_size, wav); fwrite("data", 1, 4, wav); fwrite(&data_size, 4, 1, wav);
    hr = IAudioClient_Start(client); if (FAILED(hr)) goto done;
    running = 1; start = GetTickCount();
    while (GetTickCount() - start < seconds * 1000) {
        Sleep(5);
        hr = IAudioCaptureClient_GetNextPacketSize(capture, &packets); if (FAILED(hr)) goto done;
        while (packets) {
            size_t bytes, i, samples;
            hr = IAudioCaptureClient_GetBuffer(capture, &data, &frames, &flags, NULL, NULL); if (FAILED(hr)) goto done;
            bytes = (size_t)frames * format->nBlockAlign;
            samples = (size_t)frames * format->nChannels;
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                static const uint8_t zeros[4096] = {0};
                size_t left = bytes;
                while (left) { size_t part = left < sizeof(zeros) ? left : sizeof(zeros); if (fwrite(zeros, 1, part, wav) != part) break; left -= part; }
                if (left) { IAudioCaptureClient_ReleaseBuffer(capture, frames); goto done; }
            } else {
                if (fwrite(data, 1, bytes, wav) != bytes) { IAudioCaptureClient_ReleaseBuffer(capture, frames); goto done; }
                for (i = 0; i < samples; ++i) {
                    double amplitude;
                    if (float_samples) { float sample; memcpy(&sample, data + i * 4, 4); amplitude = fabs((double)sample); }
                    else { int16_t sample; memcpy(&sample, data + i * 2, 2); amplitude = fabs((double)sample / 32768.0); }
                    if (amplitude > 0.00001) ++nonzero_samples;
                    if (amplitude > peak) peak = amplitude;
                }
            }
            data_size += (DWORD)bytes;
            hr = IAudioCaptureClient_ReleaseBuffer(capture, frames); if (FAILED(hr)) goto done;
            hr = IAudioCaptureClient_GetNextPacketSize(capture, &packets); if (FAILED(hr)) goto done;
        }
    }
    result = 1;
    printf("Host loopback: %lu bytes, peak=%.6f, nonzero samples=%llu -> build/ma5-loopback.wav\n", data_size, peak, nonzero_samples);
    puts("Loopback includes other desktop audio; compare the recording to the test MMF.");
done:
    if (running) IAudioClient_Stop(client);
    if (wav && format) {
        riff_size = 4 + 8 + (DWORD)sizeof(WAVEFORMATEX) + format->cbSize + 8 + data_size;
        fseek(wav, 4, SEEK_SET); fwrite(&riff_size, 4, 1, wav);
        fseek(wav, 24 + (long)sizeof(WAVEFORMATEX) + format->cbSize, SEEK_SET); fwrite(&data_size, 4, 1, wav);
        if (ferror(wav)) result = 0;
        if (fclose(wav)) result = 0;
    }
    if (capture) IAudioCaptureClient_Release(capture);
    if (format) CoTaskMemFree(format);
    if (client) IAudioClient_Release(client);
    if (device) IMMDevice_Release(device);
    if (enumerator) IMMDeviceEnumerator_Release(enumerator);
    if (com) CoUninitialize();
    if (!result) fprintf(stderr, "Loopback capture failed (HRESULT=%08lx); playback is still tested\n", (unsigned long)hr);
    return result;
}
#endif
