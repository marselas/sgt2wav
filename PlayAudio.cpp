//-----------------------------------------------------------------------------
// modified by marselas at gmail.com 
// - automatically plays file (sgt, rmi, or mid) passed on command-line, and exits on completion
// 
// File: PlayAudio.cpp
//
// Desc: Plays a primary segment using DirectMusic
//
// Copyright (c) 2000-2001 Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
#define STRICT
#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <tchar.h>
#include <Shellapi.h>

#include <string>

#include <dmusicc.h>
#include <dmusici.h>
#include <dxerr8.h>

#include "resource.h"
#include "DMUtil.h"
#include "DXUtil.h"

//-----------------------------------------------------------------------------
// Function-prototypes
//-----------------------------------------------------------------------------
INT_PTR CALLBACK MainDlgProc( HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam );
HRESULT OnInitDialog( HWND hDlg );
HRESULT ProcessDirectMusicMessages(BOOL &bDone);
HRESULT LoadSegmentFile( HWND hDlg, const TCHAR* strFileName );


extern int initializeWASAPI(LPWSTR *szArglist, int nArgs);
extern int setupCaptureWASAPI();
extern void startCaptureWASAPI();
extern void completeCaptureWASAPI();

extern wchar_t *gpInput;

//-----------------------------------------------------------------------------
// Defines, constants, and global variables
//-----------------------------------------------------------------------------
CMusicManager*     g_pMusicManager          = NULL;
CMusicSegment*     g_pMusicSegment          = NULL;
HINSTANCE          g_hInst                  = NULL;
HANDLE             g_hDMusicMessageEvent    = NULL;

//-----------------------------------------------------------------------------
// Name: WinMain()
// Desc: Entry point for the application.  Since we use a simple dialog for 
//       user interaction we don't need to pump messages.
//-----------------------------------------------------------------------------
// std::string gcsFilename;

INT APIENTRY WinMain( HINSTANCE hInst, HINSTANCE /*hPrevInst*/, LPSTR /*pCmdLine*/, INT /*nCmdShow*/ )
{
    HWND    hDlg = NULL;
    BOOL    bDone = FALSE;
    int     nExitCode = 0;
    HRESULT hr; 
    DWORD   dwResult;
    MSG     msg;

    InitCommonControls();

    int nArgs;
    LPWSTR *szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);

    int rc = initializeWASAPI(szArglist, nArgs);
    if (rc == -1)
        return 0;

    LocalFree(szArglist);

    g_hInst = hInst;

    // Display the main dialog box.
    hDlg = CreateDialog( hInst, MAKEINTRESOURCE(IDD_MAIN), NULL, MainDlgProc );

    while( !bDone ) 
    { 
        dwResult = MsgWaitForMultipleObjects( 1, &g_hDMusicMessageEvent, 
                                              FALSE, INFINITE, QS_ALLEVENTS );
        switch( dwResult )
        {
            case WAIT_OBJECT_0 + 0:
                // g_hDPMessageEvent is signaled, so there are
                // DirectPlay messages available
                if( FAILED( hr = ProcessDirectMusicMessages(bDone ) ) ) 
                {
                    DXTRACE_ERR( TEXT("ProcessDirectMusicMessages"), hr );
                    return FALSE;
                }
                if (bDone)
                {
                    EndDialog(hDlg, 1);
                }
                break;

            case WAIT_OBJECT_0 + 1:
                // Windows messages are available
                while( PeekMessage( &msg, NULL, 0, 0, PM_REMOVE ) ) 
                { 
                    if( !IsDialogMessage( hDlg, &msg ) )  
                    {
                        TranslateMessage( &msg ); 
                        DispatchMessage( &msg ); 
                    }

                    if( msg.message == WM_QUIT )
                    {
                        nExitCode = (int)msg.wParam;
                        bDone     = TRUE;
                        DestroyWindow( hDlg );
                    }
                }
                break;
        }
    }

    return nExitCode;
}

//-----------------------------------------------------------------------------
// Name: MainDlgProc()
// Desc: Handles dialog messages
//-----------------------------------------------------------------------------
INT_PTR CALLBACK MainDlgProc( HWND hDlg, UINT msg, WPARAM /*wParam*/, LPARAM /*lParam*/ )
{
    HRESULT hr;

    switch( msg ) 
    {
        case WM_INITDIALOG:
            if( FAILED( hr = OnInitDialog( hDlg ) ) )
            {
                DXTRACE_ERR( TEXT("OnInitDialog"), hr );
                MessageBox( hDlg, _TEXT("Error initializing DirectMusic.  Sample will now exit."), 
                                  _TEXT("DirectMusic Sample"), MB_OK | MB_ICONERROR );
                EndDialog( hDlg, 0 );
                return TRUE;
            }

            if (LoadSegmentFile(hDlg, gpInput) == S_FALSE)
            {
                EndDialog(hDlg, 0);
                return TRUE;
            }

            // Set the segment to not repeat
            g_pMusicSegment->SetRepeats(0);

            PostMessage(hDlg, WM_USER + 10, 0, 0);

            break;

        case (WM_USER + 10):
            startCaptureWASAPI();

            // Play the segment and wait. The DMUS_SEGF_BEAT indicates to play on the  next beat if there is a segment currently playing
            g_pMusicSegment->Play(DMUS_SEGF_BEAT); // DMUS_SEGF_DEFAULT
            break;

        case WM_DESTROY:
            // Cleanup everything
            SAFE_DELETE( g_pMusicSegment );
            SAFE_DELETE( g_pMusicManager );
            CloseHandle( g_hDMusicMessageEvent );
            break; 

        default:
            return FALSE; // Didn't handle message
    }

    return TRUE; // Handled message
}

//-----------------------------------------------------------------------------
// Name: OnInitDialog()
// Desc: Initializes the dialogs (sets up UI controls, etc.)
//-----------------------------------------------------------------------------
HRESULT OnInitDialog( HWND hDlg )
{
    HRESULT hr; 

    g_hDMusicMessageEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
    g_pMusicManager = new CMusicManager();

    if( FAILED( hr = g_pMusicManager->Initialize( hDlg ) ) )
        return DXTRACE_ERR( TEXT("Initialize"), hr );

    // Register segment notification
    IDirectMusicPerformance* pPerf = g_pMusicManager->GetPerformance();
    GUID guid = GUID_NOTIFICATION_SEGMENT;
    pPerf->AddNotificationType( guid );
    pPerf->SetNotificationHandle( g_hDMusicMessageEvent, 0 );  

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: LoadSegmentFile()
// Desc: 
//-----------------------------------------------------------------------------
HRESULT LoadSegmentFile( HWND /*hDlg*/, const TCHAR* strFileName )
{
    HRESULT hr;

    // Free any previous segment, and make a new one
    SAFE_DELETE( g_pMusicSegment );

    // Have the loader collect any garbage now that the old 
    // segment has been released
    g_pMusicManager->CollectGarbage();

    // Set the media path based on the file name (something like C:\MEDIA)
    // to be used as the search directory for finding DirectMusic content
    // related to this file.
    TCHAR strMediaPath[MAX_PATH] = { 0 };
    _tcscpy( strMediaPath, strFileName );
    TCHAR* strLastSlash = _tcsrchr(strMediaPath, TEXT('\\'));
    if (strLastSlash)
    {
        *strLastSlash = 0;
        if (FAILED(hr = g_pMusicManager->SetSearchDirectory(strMediaPath)))
            return DXTRACE_ERR(TEXT("SetSearchDirectory"), hr);
    }

    // For DirectMusic must know if the file is a standard MIDI file or not
    // in order to load the correct instruments.
    BOOL bMidiFile = FALSE;
    if( wcsstr( strFileName, TEXT(".mid") ) != NULL ||
        wcsstr( strFileName, TEXT(".rmi") ) != NULL ) 
    {
        bMidiFile = TRUE;
    }

    // Load the file into a DirectMusic segment 
    if( FAILED( g_pMusicManager->CreateSegmentFromFile( &g_pMusicSegment, (TCHAR *) strFileName, TRUE, bMidiFile ) ) )
    {
        // Not a critical failure, so just update the status
        return S_FALSE; 
    }

    int rc = setupCaptureWASAPI();
    if (rc == -1)
        return S_FALSE;

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: ProcessDirectMusicMessages()
// Desc: Handle DirectMusic notification messages
//-----------------------------------------------------------------------------
HRESULT ProcessDirectMusicMessages(BOOL &bDone)
{
    bDone = false;

    HRESULT hr;
    IDirectMusicPerformance8* pPerf = NULL;
    DMUS_NOTIFICATION_PMSG* pPMsg;
        
    if( NULL == g_pMusicManager )
        return S_OK;

    pPerf = g_pMusicManager->GetPerformance();

    // Get waiting notification message from the performance
    while( S_OK == pPerf->GetNotificationPMsg( &pPMsg ) )
    {
        switch( pPMsg->dwNotificationOption )
        {
        case DMUS_NOTIFICATION_SEGSTART:
//            startCaptureWASAPI();
            break;

        case DMUS_NOTIFICATION_SEGEND:
            if( pPMsg->punkUser )
            {
                completeCaptureWASAPI();

                IDirectMusicSegmentState8* pSegmentState   = NULL;
                IDirectMusicSegment*       pNotifySegment   = NULL;
                IDirectMusicSegment8*      pNotifySegment8  = NULL;
                IDirectMusicSegment8*      pPrimarySegment8 = NULL;

                // The pPMsg->punkUser contains a IDirectMusicSegmentState8, 
                // which we can query for the segment that the SegmentState refers to.
                if( FAILED( hr = pPMsg->punkUser->QueryInterface( IID_IDirectMusicSegmentState8,
                                                                  (VOID**) &pSegmentState ) ) )
                    return DXTRACE_ERR( TEXT("QueryInterface"), hr );

                if( FAILED( hr = pSegmentState->GetSegment( &pNotifySegment ) ) )
                {
                    // Sometimes the segend arrives after the segment is gone
                    // This can happen when you load another segment as 
                    // a motif or the segment is ending
                    if( hr == DMUS_E_NOT_FOUND )
                    {
                        SAFE_RELEASE( pSegmentState );
                        return S_OK;
                    }

                    return DXTRACE_ERR( TEXT("GetSegment"), hr );
                }

                if( FAILED( hr = pNotifySegment->QueryInterface( IID_IDirectMusicSegment8,
                                                                 (VOID**) &pNotifySegment8 ) ) )
                    return DXTRACE_ERR( TEXT("QueryInterface"), hr );

                // Get the IDirectMusicSegment for the primary segment
                pPrimarySegment8 = g_pMusicSegment->GetSegment();

                // Cleanup
                SAFE_RELEASE( pSegmentState );
                SAFE_RELEASE( pNotifySegment );
                SAFE_RELEASE( pNotifySegment8 );

                bDone = true;
            }
            break;
        }

        pPerf->FreePMsg( (DMUS_PMSG*)pPMsg ); 
    }

    return S_OK;
}
