// modified by marselas at gmail.com 
// - automatically plays file (sgt, rmi, or mid) passed on command-line, and exits on completion
// - PlayAudio.cpp is the main file

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved
//

//
// WASAPICaptureSharedTimerDriven.cpp : Scaffolding associated with the WASAPI Capture Shared Timer Driven sample application.
//
//  This application captures data from the specified input device and writes it to a uniquely named .WAV file in the current directory.
//

#include <windows.h>
#include <new>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys.h>
#include <stdio.h>
#include <strsafe.h>
#include <string>
#include <sstream>

#include "WASAPICapture.h"
#include "CmdLine.h"

template <class T> void SafeRelease(T **ppT)
{
    if (*ppT)
    {
        (*ppT)->Release();
        *ppT = NULL;
    }
}

int gdwDuration = 4 * 60 * 60; // yeah, duration is a misnomer.  this is really up to about 20 minutes of uncompressed wav data

int TargetLatency = 20;
bool ShowHelp;
bool DisableMMCSS;

wchar_t *gpInput;

int gdwDeviceIndex = 0;

CommandLineSwitch CmdLineArgs[] = 
{
    { L"?", L"Print this help", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&ShowHelp)},
    { L"h", L"Print this help", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&ShowHelp)},
    { L"l", L"Audio Capture Latency (ms)", CommandLineSwitch::SwitchTypeInteger, reinterpret_cast<void **>(&TargetLatency), false},
    { L"m", L"Disable the use of MMCSS", CommandLineSwitch::SwitchTypeNone, reinterpret_cast<void **>(&DisableMMCSS)},
    { L"device", L"Device (endpoint) ID", CommandLineSwitch::SwitchTypeInteger, reinterpret_cast<void **>(&gdwDeviceIndex), true },
    { L"source", L"Source DirectMusic sgt, rmi, or mid file", CommandLineSwitch::SwitchTypeString, reinterpret_cast<void **>(&gpInput), false },
};

size_t CmdLineArgLength = ARRAYSIZE(CmdLineArgs);

//
//  Print help for the sample
//
void ListDevices(std::wostringstream &ostr);

void Help(LPCWSTR ProgramName)
{
    std::wostringstream ostr;

    ostr << L"Usage: " << ProgramName << L" [-/ ][Switch][:][Value]\n\n";
    ostr << L"Where Switch is one of the following: \n";

    for (size_t i = 0 ; i < CmdLineArgLength ; i += 1)
    {
        ostr << L"    " << CmdLineArgs[i].SwitchName << L":" << CmdLineArgs[i].SwitchHelp << L"\n";
    }

    ostr << "\nDevices:\n";

    ListDevices(ostr);

    MessageBox(GetDesktopWindow(), ostr.str().c_str(), L"DirectMusic File Converter", MB_OK | MB_ICONINFORMATION);
}

//
//  Retrieves the device friendly name for a particular device in a device collection.  
//
//  The returned string was allocated using malloc() so it should be freed using free();
//
LPWSTR GetDeviceName(IMMDeviceCollection *DeviceCollection, UINT DeviceIndex)
{
    IMMDevice *device;
    LPWSTR deviceId;
    HRESULT hr;

    hr = DeviceCollection->Item(DeviceIndex, &device);
    if (FAILED(hr))
    {
        printf("Unable to get device %d: %x\n", DeviceIndex, hr);
        return NULL;
    }
    hr = device->GetId(&deviceId);
    if (FAILED(hr))
    {
        printf("Unable to get device %d id: %x\n", DeviceIndex, hr);
        return NULL;
    }

    IPropertyStore *propertyStore;
    hr = device->OpenPropertyStore(STGM_READ, &propertyStore);
    SafeRelease(&device);
    if (FAILED(hr))
    {
        printf("Unable to open device %d property store: %x\n", DeviceIndex, hr);
        return NULL;
    }

    PROPVARIANT friendlyName;
    PropVariantInit(&friendlyName);
    hr = propertyStore->GetValue(PKEY_Device_FriendlyName, &friendlyName);
    SafeRelease(&propertyStore);

    if (FAILED(hr))
    {
        printf("Unable to retrieve friendly name for device %d : %x\n", DeviceIndex, hr);
        return NULL;
    }

    wchar_t deviceName[128];
    hr = StringCbPrintf(deviceName, sizeof(deviceName), L"%s (%s)", friendlyName.vt != VT_LPWSTR ? L"Unknown" : friendlyName.pwszVal, deviceId);
    if (FAILED(hr))
    {
        printf("Unable to format friendly name for device %d : %x\n", DeviceIndex, hr);
        return NULL;
    }

    PropVariantClear(&friendlyName);
    CoTaskMemFree(deviceId);

    wchar_t *returnValue = _wcsdup(deviceName);
    if (returnValue == NULL)
    {
        printf("Unable to allocate buffer for return\n");
        return NULL;
    }
    return returnValue;
}
//
//  Based on the input switches, pick the specified device to use.
//
void ListDevices(std::wostringstream &ostr)
{
    HRESULT hr;
    bool retValue = true;
    IMMDeviceEnumerator *deviceEnumerator = NULL;
    IMMDeviceCollection *deviceCollection = NULL;
    IMMDevice *device = NULL;

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&deviceEnumerator));
    if (FAILED(hr))
    {
        printf("Unable to instantiate device enumerator: %x\n", hr);
        retValue = false;
        goto Exit;
    }

    //
    //  The user didn't specify an output device, prompt the user for a device and use that.
    //
    hr = deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &deviceCollection);
    if (FAILED(hr))
    {
        printf("Unable to retrieve device collection: %x\n", hr);
        retValue = false;
        goto Exit;
    }

    UINT deviceCount;
    hr = deviceCollection->GetCount(&deviceCount);
    if (FAILED(hr))
    {
        printf("Unable to get device collection length: %x\n", hr);
        retValue = false;
        goto Exit;
    }
    for (UINT i = 0; i < deviceCount; i += 1)
    {
        LPWSTR deviceName;

        deviceName = GetDeviceName(deviceCollection, i);
        if (deviceName == NULL)
        {
            retValue = false;
            goto Exit;
        }
        ostr << L"  " << i << L":" << deviceName << L"\n";
        free(deviceName);
    }

Exit:
    SafeRelease(&deviceCollection);
    SafeRelease(&deviceEnumerator);
    SafeRelease(&device);
}

bool PickDevice(IMMDevice **DeviceToUse, bool *IsDefaultDevice, ERole *DefaultDeviceRole)
{
    HRESULT hr;
    bool retValue = true;
    IMMDeviceEnumerator *deviceEnumerator = NULL;
    IMMDeviceCollection *deviceCollection = NULL;

    *IsDefaultDevice = false;   // Assume we're not using the default device.

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&deviceEnumerator));
    if (FAILED(hr))
    {
        printf("Unable to instantiate device enumerator: %x\n", hr);
        retValue = false;
        goto Exit;
    }

    IMMDevice *device = NULL;

    hr = deviceEnumerator->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &deviceCollection);
    if (FAILED(hr))
    {
        printf("Unable to retrieve device collection: %x\n", hr);
        retValue = false;
        goto Exit;
    }

    hr = deviceCollection->Item(gdwDeviceIndex, &device);
    if (FAILED(hr))
    {
        printf("Unable to get endpoint for endpoint %ld: %x\n", gdwDeviceIndex, hr);
        retValue = false;
        goto Exit;
    }

    *DefaultDeviceRole = eConsole;

    *DeviceToUse = device;
    retValue = true;
Exit:
    SafeRelease(&deviceCollection);
    SafeRelease(&deviceEnumerator);

    return retValue;
}

//
//  WAV file writer.
//
//  This is a VERY simple .WAV file writer.
//

//
//  A wave file consists of:
//
//  RIFF header:    8 bytes consisting of the signature "RIFF" followed by a 4 byte file length.
//  WAVE header:    4 bytes consisting of the signature "WAVE".
//  fmt header:     4 bytes consisting of the signature "fmt " followed by a WAVEFORMATEX 
//  WAVEFORMAT:     <n> bytes containing a waveformat structure.
//  DATA header:    8 bytes consisting of the signature "data" followed by a 4 byte file length.
//  wave data:      <m> bytes containing wave data.
//
//
//  Header for a WAV file - we define a structure describing the first few fields in the header for convenience.
//
struct WAVEHEADER
{
    DWORD   dwRiff;                     // "RIFF"
    DWORD   dwSize;                     // Size
    DWORD   dwWave;                     // "WAVE"
    DWORD   dwFmt;                      // "fmt "
    DWORD   dwFmtSize;                  // Wave Format Size
};

//  Static RIFF header, we'll append the format to it.
const BYTE WaveHeader[] = 
{
    'R',   'I',   'F',   'F',  0x00,  0x00,  0x00,  0x00, 'W',   'A',   'V',   'E',   'f',   'm',   't',   ' ', 0x00, 0x00, 0x00, 0x00
};

//  Static wave DATA tag.
const BYTE WaveData[] = { 'd', 'a', 't', 'a'};

//
//  Write the contents of a WAV file.  We take as input the data to write and the format of that data.
//
bool WriteWaveFile(HANDLE FileHandle, const BYTE *Buffer, const size_t BufferSize, const WAVEFORMATEX *WaveFormat)
{
    DWORD waveFileSize = sizeof(WAVEHEADER) + sizeof(WAVEFORMATEX) + WaveFormat->cbSize + sizeof(WaveData) + sizeof(DWORD) + static_cast<DWORD>(BufferSize);
    BYTE *waveFileData = new (std::nothrow) BYTE[waveFileSize];
    BYTE *waveFilePointer = waveFileData;
    WAVEHEADER *waveHeader = reinterpret_cast<WAVEHEADER *>(waveFileData);

    if (waveFileData == NULL)
    {
        printf("Unable to allocate %d bytes to hold output wave data\n", waveFileSize);
        return false;
    }

    //
    //  Copy in the wave header - we'll fix up the lengths later.
    //
    CopyMemory(waveFilePointer, WaveHeader, sizeof(WaveHeader));
    waveFilePointer += sizeof(WaveHeader);

    //
    //  Update the sizes in the header.
    //
    waveHeader->dwSize = waveFileSize - (2 * sizeof(DWORD));
    waveHeader->dwFmtSize = sizeof(WAVEFORMATEX) + WaveFormat->cbSize;

    //
    //  Next copy in the WaveFormatex structure.
    //
    CopyMemory(waveFilePointer, WaveFormat, sizeof(WAVEFORMATEX) + WaveFormat->cbSize);
    waveFilePointer += sizeof(WAVEFORMATEX) + WaveFormat->cbSize;


    //
    //  Then the data header.
    //
    CopyMemory(waveFilePointer, WaveData, sizeof(WaveData));
    waveFilePointer += sizeof(WaveData);
    *(reinterpret_cast<DWORD *>(waveFilePointer)) = static_cast<DWORD>(BufferSize);
    waveFilePointer += sizeof(DWORD);

    //
    //  And finally copy in the audio data.
    //
    CopyMemory(waveFilePointer, Buffer, BufferSize);

    //
    //  Last but not least, write the data to the file.
    //
    DWORD bytesWritten;
    if (!WriteFile(FileHandle, waveFileData, waveFileSize, &bytesWritten, NULL))
    {
        printf("Unable to write wave file: %d\n", GetLastError());
        delete []waveFileData;
        return false;
    }

    if (bytesWritten != waveFileSize)
    {
        printf("Failed to write entire wave file\n");
        delete []waveFileData;
        return false;
    }
    delete []waveFileData;
    return true;
}

//
//  Write the captured wave data to an output file so that it can be examined later.
//
void SaveWaveData(BYTE *CaptureBuffer, size_t BufferSize, const WAVEFORMATEX *WaveFormat)
{
    wchar_t waveFileName[MAX_PATH];
    HRESULT hr = StringCbCopy(waveFileName, sizeof(waveFileName), gpInput);
    if (SUCCEEDED(hr))
    {
        WCHAR *pExt = wcschr(waveFileName, '.');
        wcscpy(pExt, L".wav");

        DeleteFile(waveFileName);

        HANDLE waveHandle = CreateFile(waveFileName, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 
                                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
        if (waveHandle != INVALID_HANDLE_VALUE)
        {
            if (WriteWaveFile(waveHandle, CaptureBuffer, BufferSize, WaveFormat))
            {
                printf("Successfully wrote WAVE data to %S\n", waveFileName);
            }
            else
            {
                printf("Unable to write wave file\n");
            }
            CloseHandle(waveHandle);
        }
        else
        {
            printf("Unable to open output WAV file %S: %d\n", waveFileName, GetLastError());
        }
    }
}

//
//  The core of the sample.
//
//  Parse the command line, interpret the input parameters, pick an audio device then capture data from that device.
//  When done, write the data to a file.
//

IMMDevice *device = NULL;
bool isDefaultDevice;
ERole role;

int initializeWASAPI(LPWSTR *szArglist, int nArgs)
{
    //
    //  A GUI application should use COINIT_APARTMENTTHREADED instead of COINIT_MULTITHREADED.
    //
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        printf("Unable to initialize COM: %x\n", hr);
        return -1;
    }

    if (nArgs == 1)
    {
        Help(szArglist[0]);
        CoUninitialize();
        return -1;
    }

    if (!ParseCommandLine(nArgs, szArglist, CmdLineArgs, CmdLineArgLength))
    {
        CoUninitialize();
        return -1;
    }
    //
    //  Now that we've parsed our command line, do some semantic checks.
    //

    //
    //  First off, show the help for the app if the user asked for it.
    //
    if (ShowHelp)
    {
        Help(szArglist[0]);
        CoUninitialize();
        return -1;
    }

    //
    //  Now that we've parsed our command line, pick the device to capture.
    //
    if (!PickDevice(&device, &isDefaultDevice, &role))
    {
        CoUninitialize();
        return -1;
    }

    return 0;
}

CWASAPICapture *capturer;
size_t captureBufferSize;
BYTE *captureBuffer;

int setupCaptureWASAPI()
{
    //
    //  Instantiate a capturer and capture sounds for TargetDuration seconds
    //
    //  Configure the capturer to enable stream switching on the specified role if the user specified one of the default devices.
    //

    capturer = new (std::nothrow) CWASAPICapture(device, isDefaultDevice, role);
    if (capturer == NULL)
    {
        printf("Unable to allocate capturer\n");
        return -1;
    }

    capturer->Initialize(TargetLatency);

    //
    //  We've initialized the capturer.  Once we've done that, we know some information about the
    //  mix format and we can allocate the buffer that we're going to capture.
    //
    //
    //  The buffer is going to contain "TargetDuration" seconds worth of PCM data.  That means 
    //  we're going to have TargetDuration*samples/second frames multiplied by the frame size.
    //

    captureBufferSize = capturer->SamplesPerSecond() * gdwDuration * capturer->FrameSize();
    captureBuffer = new (std::nothrow) BYTE[captureBufferSize];

    if (captureBuffer == NULL)
    {
        printf("Unable to allocate capture buffer\n");
        return -1;
    }

    capturer->SetupStartThread(captureBuffer, captureBufferSize);

    return 0;
}

void startCaptureWASAPI()
{
   capturer->Start();
}

void completeCaptureWASAPI()
{
    capturer->Stop();

    //
    //  We've now captured our wave data.  Now write it out in a wave file.
    //
    SaveWaveData(captureBuffer, capturer->BytesCaptured(), capturer->MixFormat());


    //
    //  Now shut down the capturer and release it we're done.
    //
    capturer->Shutdown();
    SafeRelease(&capturer);

    delete[]captureBuffer;

    // Exit:
    SafeRelease(&device);
    CoUninitialize();

}
