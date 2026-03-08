; ProxyWinmmTrampoline.asm -- Generated naked jump stubs for winmm.dll proxy
; Eisen -- Iron Warrior -- Proxy DLL Forwarding (MASM x64)
;
; Each stub is a naked JMP through a function pointer resolved at DLL load.
; This preserves all registers, flags, and stack -- perfect forwarding.
; Pointers are initialized to Proxy_Fallback (xor rax,rax; ret) for safety.
;
; DO NOT EDIT -- generated from real winmm.dll export table.

.data

; External function pointers (defined in EisenWinmmPtrs.h via Eisen.cpp)
externdef g_pfn_CloseDriver : qword
externdef g_pfn_DefDriverProc : qword
externdef g_pfn_DriverCallback : qword
externdef g_pfn_DrvGetModuleHandle : qword
externdef g_pfn_GetDriverModuleHandle : qword
externdef g_pfn_OpenDriver : qword
externdef g_pfn_PlaySound : qword
externdef g_pfn_PlaySoundA : qword
externdef g_pfn_PlaySoundW : qword
externdef g_pfn_SendDriverMessage : qword
externdef g_pfn_WOWAppExit : qword
externdef g_pfn_auxGetDevCapsA : qword
externdef g_pfn_auxGetDevCapsW : qword
externdef g_pfn_auxGetNumDevs : qword
externdef g_pfn_auxGetVolume : qword
externdef g_pfn_auxOutMessage : qword
externdef g_pfn_auxSetVolume : qword
externdef g_pfn_joyConfigChanged : qword
externdef g_pfn_joyGetDevCapsA : qword
externdef g_pfn_joyGetDevCapsW : qword
externdef g_pfn_joyGetNumDevs : qword
externdef g_pfn_joyGetPos : qword
externdef g_pfn_joyGetPosEx : qword
externdef g_pfn_joyGetThreshold : qword
externdef g_pfn_joyReleaseCapture : qword
externdef g_pfn_joySetCapture : qword
externdef g_pfn_joySetThreshold : qword
externdef g_pfn_mciDriverNotify : qword
externdef g_pfn_mciDriverYield : qword
externdef g_pfn_mciExecute : qword
externdef g_pfn_mciFreeCommandResource : qword
externdef g_pfn_mciGetCreatorTask : qword
externdef g_pfn_mciGetDeviceIDA : qword
externdef g_pfn_mciGetDeviceIDFromElementIDA : qword
externdef g_pfn_mciGetDeviceIDFromElementIDW : qword
externdef g_pfn_mciGetDeviceIDW : qword
externdef g_pfn_mciGetDriverData : qword
externdef g_pfn_mciGetErrorStringA : qword
externdef g_pfn_mciGetErrorStringW : qword
externdef g_pfn_mciGetYieldProc : qword
externdef g_pfn_mciLoadCommandResource : qword
externdef g_pfn_mciSendCommandA : qword
externdef g_pfn_mciSendCommandW : qword
externdef g_pfn_mciSendStringA : qword
externdef g_pfn_mciSendStringW : qword
externdef g_pfn_mciSetDriverData : qword
externdef g_pfn_mciSetYieldProc : qword
externdef g_pfn_midiConnect : qword
externdef g_pfn_midiDisconnect : qword
externdef g_pfn_midiInAddBuffer : qword
externdef g_pfn_midiInClose : qword
externdef g_pfn_midiInGetDevCapsA : qword
externdef g_pfn_midiInGetDevCapsW : qword
externdef g_pfn_midiInGetErrorTextA : qword
externdef g_pfn_midiInGetErrorTextW : qword
externdef g_pfn_midiInGetID : qword
externdef g_pfn_midiInGetNumDevs : qword
externdef g_pfn_midiInMessage : qword
externdef g_pfn_midiInOpen : qword
externdef g_pfn_midiInPrepareHeader : qword
externdef g_pfn_midiInReset : qword
externdef g_pfn_midiInStart : qword
externdef g_pfn_midiInStop : qword
externdef g_pfn_midiInUnprepareHeader : qword
externdef g_pfn_midiOutCacheDrumPatches : qword
externdef g_pfn_midiOutCachePatches : qword
externdef g_pfn_midiOutClose : qword
externdef g_pfn_midiOutGetDevCapsA : qword
externdef g_pfn_midiOutGetDevCapsW : qword
externdef g_pfn_midiOutGetErrorTextA : qword
externdef g_pfn_midiOutGetErrorTextW : qword
externdef g_pfn_midiOutGetID : qword
externdef g_pfn_midiOutGetNumDevs : qword
externdef g_pfn_midiOutGetVolume : qword
externdef g_pfn_midiOutLongMsg : qword
externdef g_pfn_midiOutMessage : qword
externdef g_pfn_midiOutOpen : qword
externdef g_pfn_midiOutPrepareHeader : qword
externdef g_pfn_midiOutReset : qword
externdef g_pfn_midiOutSetVolume : qword
externdef g_pfn_midiOutShortMsg : qword
externdef g_pfn_midiOutUnprepareHeader : qword
externdef g_pfn_midiStreamClose : qword
externdef g_pfn_midiStreamOpen : qword
externdef g_pfn_midiStreamOut : qword
externdef g_pfn_midiStreamPause : qword
externdef g_pfn_midiStreamPosition : qword
externdef g_pfn_midiStreamProperty : qword
externdef g_pfn_midiStreamRestart : qword
externdef g_pfn_midiStreamStop : qword
externdef g_pfn_mixerClose : qword
externdef g_pfn_mixerGetControlDetailsA : qword
externdef g_pfn_mixerGetControlDetailsW : qword
externdef g_pfn_mixerGetDevCapsA : qword
externdef g_pfn_mixerGetDevCapsW : qword
externdef g_pfn_mixerGetID : qword
externdef g_pfn_mixerGetLineControlsA : qword
externdef g_pfn_mixerGetLineControlsW : qword
externdef g_pfn_mixerGetLineInfoA : qword
externdef g_pfn_mixerGetLineInfoW : qword
externdef g_pfn_mixerGetNumDevs : qword
externdef g_pfn_mixerMessage : qword
externdef g_pfn_mixerOpen : qword
externdef g_pfn_mixerSetControlDetails : qword
externdef g_pfn_mmDrvInstall : qword
externdef g_pfn_mmGetCurrentTask : qword
externdef g_pfn_mmTaskBlock : qword
externdef g_pfn_mmTaskCreate : qword
externdef g_pfn_mmTaskSignal : qword
externdef g_pfn_mmTaskYield : qword
externdef g_pfn_mmioAdvance : qword
externdef g_pfn_mmioAscend : qword
externdef g_pfn_mmioClose : qword
externdef g_pfn_mmioCreateChunk : qword
externdef g_pfn_mmioDescend : qword
externdef g_pfn_mmioFlush : qword
externdef g_pfn_mmioGetInfo : qword
externdef g_pfn_mmioInstallIOProcA : qword
externdef g_pfn_mmioInstallIOProcW : qword
externdef g_pfn_mmioOpenA : qword
externdef g_pfn_mmioOpenW : qword
externdef g_pfn_mmioRead : qword
externdef g_pfn_mmioRenameA : qword
externdef g_pfn_mmioRenameW : qword
externdef g_pfn_mmioSeek : qword
externdef g_pfn_mmioSendMessage : qword
externdef g_pfn_mmioSetBuffer : qword
externdef g_pfn_mmioSetInfo : qword
externdef g_pfn_mmioStringToFOURCCA : qword
externdef g_pfn_mmioStringToFOURCCW : qword
externdef g_pfn_mmioWrite : qword
externdef g_pfn_mmsystemGetVersion : qword
externdef g_pfn_sndPlaySoundA : qword
externdef g_pfn_sndPlaySoundW : qword
externdef g_pfn_timeBeginPeriod : qword
externdef g_pfn_timeEndPeriod : qword
externdef g_pfn_timeGetDevCaps : qword
externdef g_pfn_timeGetSystemTime : qword
externdef g_pfn_timeGetTime : qword
externdef g_pfn_timeKillEvent : qword
externdef g_pfn_timeSetEvent : qword
externdef g_pfn_waveInAddBuffer : qword
externdef g_pfn_waveInClose : qword
externdef g_pfn_waveInGetDevCapsA : qword
externdef g_pfn_waveInGetDevCapsW : qword
externdef g_pfn_waveInGetErrorTextA : qword
externdef g_pfn_waveInGetErrorTextW : qword
externdef g_pfn_waveInGetID : qword
externdef g_pfn_waveInGetNumDevs : qword
externdef g_pfn_waveInGetPosition : qword
externdef g_pfn_waveInMessage : qword
externdef g_pfn_waveInOpen : qword
externdef g_pfn_waveInPrepareHeader : qword
externdef g_pfn_waveInReset : qword
externdef g_pfn_waveInStart : qword
externdef g_pfn_waveInStop : qword
externdef g_pfn_waveInUnprepareHeader : qword
externdef g_pfn_waveOutBreakLoop : qword
externdef g_pfn_waveOutClose : qword
externdef g_pfn_waveOutGetDevCapsA : qword
externdef g_pfn_waveOutGetDevCapsW : qword
externdef g_pfn_waveOutGetErrorTextA : qword
externdef g_pfn_waveOutGetErrorTextW : qword
externdef g_pfn_waveOutGetID : qword
externdef g_pfn_waveOutGetNumDevs : qword
externdef g_pfn_waveOutGetPitch : qword
externdef g_pfn_waveOutGetPlaybackRate : qword
externdef g_pfn_waveOutGetPosition : qword
externdef g_pfn_waveOutGetVolume : qword
externdef g_pfn_waveOutMessage : qword
externdef g_pfn_waveOutOpen : qword
externdef g_pfn_waveOutPause : qword
externdef g_pfn_waveOutPrepareHeader : qword
externdef g_pfn_waveOutReset : qword
externdef g_pfn_waveOutRestart : qword
externdef g_pfn_waveOutSetPitch : qword
externdef g_pfn_waveOutSetPlaybackRate : qword
externdef g_pfn_waveOutSetVolume : qword
externdef g_pfn_waveOutUnprepareHeader : qword
externdef g_pfn_waveOutWrite : qword

.code

; Safe fallback for functions not present in the real DLL.
; Returns 0 in RAX. x64 calling convention: caller cleans stack,
; so a simple xor+ret is safe for any function signature.
Proxy_Fallback proc
    xor rax, rax
    ret
Proxy_Fallback endp

public Proxy_Fallback

Proxy_CloseDriver proc
    jmp qword ptr [g_pfn_CloseDriver]
Proxy_CloseDriver endp

Proxy_DefDriverProc proc
    jmp qword ptr [g_pfn_DefDriverProc]
Proxy_DefDriverProc endp

Proxy_DriverCallback proc
    jmp qword ptr [g_pfn_DriverCallback]
Proxy_DriverCallback endp

Proxy_DrvGetModuleHandle proc
    jmp qword ptr [g_pfn_DrvGetModuleHandle]
Proxy_DrvGetModuleHandle endp

Proxy_GetDriverModuleHandle proc
    jmp qword ptr [g_pfn_GetDriverModuleHandle]
Proxy_GetDriverModuleHandle endp

Proxy_OpenDriver proc
    jmp qword ptr [g_pfn_OpenDriver]
Proxy_OpenDriver endp

Proxy_PlaySound proc
    jmp qword ptr [g_pfn_PlaySound]
Proxy_PlaySound endp

Proxy_PlaySoundA proc
    jmp qword ptr [g_pfn_PlaySoundA]
Proxy_PlaySoundA endp

Proxy_PlaySoundW proc
    jmp qword ptr [g_pfn_PlaySoundW]
Proxy_PlaySoundW endp

Proxy_SendDriverMessage proc
    jmp qword ptr [g_pfn_SendDriverMessage]
Proxy_SendDriverMessage endp

Proxy_WOWAppExit proc
    jmp qword ptr [g_pfn_WOWAppExit]
Proxy_WOWAppExit endp

Proxy_auxGetDevCapsA proc
    jmp qword ptr [g_pfn_auxGetDevCapsA]
Proxy_auxGetDevCapsA endp

Proxy_auxGetDevCapsW proc
    jmp qword ptr [g_pfn_auxGetDevCapsW]
Proxy_auxGetDevCapsW endp

Proxy_auxGetNumDevs proc
    jmp qword ptr [g_pfn_auxGetNumDevs]
Proxy_auxGetNumDevs endp

Proxy_auxGetVolume proc
    jmp qword ptr [g_pfn_auxGetVolume]
Proxy_auxGetVolume endp

Proxy_auxOutMessage proc
    jmp qword ptr [g_pfn_auxOutMessage]
Proxy_auxOutMessage endp

Proxy_auxSetVolume proc
    jmp qword ptr [g_pfn_auxSetVolume]
Proxy_auxSetVolume endp

Proxy_joyConfigChanged proc
    jmp qword ptr [g_pfn_joyConfigChanged]
Proxy_joyConfigChanged endp

Proxy_joyGetDevCapsA proc
    jmp qword ptr [g_pfn_joyGetDevCapsA]
Proxy_joyGetDevCapsA endp

Proxy_joyGetDevCapsW proc
    jmp qword ptr [g_pfn_joyGetDevCapsW]
Proxy_joyGetDevCapsW endp

Proxy_joyGetNumDevs proc
    jmp qword ptr [g_pfn_joyGetNumDevs]
Proxy_joyGetNumDevs endp

Proxy_joyGetPos proc
    jmp qword ptr [g_pfn_joyGetPos]
Proxy_joyGetPos endp

Proxy_joyGetPosEx proc
    jmp qword ptr [g_pfn_joyGetPosEx]
Proxy_joyGetPosEx endp

Proxy_joyGetThreshold proc
    jmp qword ptr [g_pfn_joyGetThreshold]
Proxy_joyGetThreshold endp

Proxy_joyReleaseCapture proc
    jmp qword ptr [g_pfn_joyReleaseCapture]
Proxy_joyReleaseCapture endp

Proxy_joySetCapture proc
    jmp qword ptr [g_pfn_joySetCapture]
Proxy_joySetCapture endp

Proxy_joySetThreshold proc
    jmp qword ptr [g_pfn_joySetThreshold]
Proxy_joySetThreshold endp

Proxy_mciDriverNotify proc
    jmp qword ptr [g_pfn_mciDriverNotify]
Proxy_mciDriverNotify endp

Proxy_mciDriverYield proc
    jmp qword ptr [g_pfn_mciDriverYield]
Proxy_mciDriverYield endp

Proxy_mciExecute proc
    jmp qword ptr [g_pfn_mciExecute]
Proxy_mciExecute endp

Proxy_mciFreeCommandResource proc
    jmp qword ptr [g_pfn_mciFreeCommandResource]
Proxy_mciFreeCommandResource endp

Proxy_mciGetCreatorTask proc
    jmp qword ptr [g_pfn_mciGetCreatorTask]
Proxy_mciGetCreatorTask endp

Proxy_mciGetDeviceIDA proc
    jmp qword ptr [g_pfn_mciGetDeviceIDA]
Proxy_mciGetDeviceIDA endp

Proxy_mciGetDeviceIDFromElementIDA proc
    jmp qword ptr [g_pfn_mciGetDeviceIDFromElementIDA]
Proxy_mciGetDeviceIDFromElementIDA endp

Proxy_mciGetDeviceIDFromElementIDW proc
    jmp qword ptr [g_pfn_mciGetDeviceIDFromElementIDW]
Proxy_mciGetDeviceIDFromElementIDW endp

Proxy_mciGetDeviceIDW proc
    jmp qword ptr [g_pfn_mciGetDeviceIDW]
Proxy_mciGetDeviceIDW endp

Proxy_mciGetDriverData proc
    jmp qword ptr [g_pfn_mciGetDriverData]
Proxy_mciGetDriverData endp

Proxy_mciGetErrorStringA proc
    jmp qword ptr [g_pfn_mciGetErrorStringA]
Proxy_mciGetErrorStringA endp

Proxy_mciGetErrorStringW proc
    jmp qword ptr [g_pfn_mciGetErrorStringW]
Proxy_mciGetErrorStringW endp

Proxy_mciGetYieldProc proc
    jmp qword ptr [g_pfn_mciGetYieldProc]
Proxy_mciGetYieldProc endp

Proxy_mciLoadCommandResource proc
    jmp qword ptr [g_pfn_mciLoadCommandResource]
Proxy_mciLoadCommandResource endp

Proxy_mciSendCommandA proc
    jmp qword ptr [g_pfn_mciSendCommandA]
Proxy_mciSendCommandA endp

Proxy_mciSendCommandW proc
    jmp qword ptr [g_pfn_mciSendCommandW]
Proxy_mciSendCommandW endp

Proxy_mciSendStringA proc
    jmp qword ptr [g_pfn_mciSendStringA]
Proxy_mciSendStringA endp

Proxy_mciSendStringW proc
    jmp qword ptr [g_pfn_mciSendStringW]
Proxy_mciSendStringW endp

Proxy_mciSetDriverData proc
    jmp qword ptr [g_pfn_mciSetDriverData]
Proxy_mciSetDriverData endp

Proxy_mciSetYieldProc proc
    jmp qword ptr [g_pfn_mciSetYieldProc]
Proxy_mciSetYieldProc endp

Proxy_midiConnect proc
    jmp qword ptr [g_pfn_midiConnect]
Proxy_midiConnect endp

Proxy_midiDisconnect proc
    jmp qword ptr [g_pfn_midiDisconnect]
Proxy_midiDisconnect endp

Proxy_midiInAddBuffer proc
    jmp qword ptr [g_pfn_midiInAddBuffer]
Proxy_midiInAddBuffer endp

Proxy_midiInClose proc
    jmp qword ptr [g_pfn_midiInClose]
Proxy_midiInClose endp

Proxy_midiInGetDevCapsA proc
    jmp qword ptr [g_pfn_midiInGetDevCapsA]
Proxy_midiInGetDevCapsA endp

Proxy_midiInGetDevCapsW proc
    jmp qword ptr [g_pfn_midiInGetDevCapsW]
Proxy_midiInGetDevCapsW endp

Proxy_midiInGetErrorTextA proc
    jmp qword ptr [g_pfn_midiInGetErrorTextA]
Proxy_midiInGetErrorTextA endp

Proxy_midiInGetErrorTextW proc
    jmp qword ptr [g_pfn_midiInGetErrorTextW]
Proxy_midiInGetErrorTextW endp

Proxy_midiInGetID proc
    jmp qword ptr [g_pfn_midiInGetID]
Proxy_midiInGetID endp

Proxy_midiInGetNumDevs proc
    jmp qword ptr [g_pfn_midiInGetNumDevs]
Proxy_midiInGetNumDevs endp

Proxy_midiInMessage proc
    jmp qword ptr [g_pfn_midiInMessage]
Proxy_midiInMessage endp

Proxy_midiInOpen proc
    jmp qword ptr [g_pfn_midiInOpen]
Proxy_midiInOpen endp

Proxy_midiInPrepareHeader proc
    jmp qword ptr [g_pfn_midiInPrepareHeader]
Proxy_midiInPrepareHeader endp

Proxy_midiInReset proc
    jmp qword ptr [g_pfn_midiInReset]
Proxy_midiInReset endp

Proxy_midiInStart proc
    jmp qword ptr [g_pfn_midiInStart]
Proxy_midiInStart endp

Proxy_midiInStop proc
    jmp qword ptr [g_pfn_midiInStop]
Proxy_midiInStop endp

Proxy_midiInUnprepareHeader proc
    jmp qword ptr [g_pfn_midiInUnprepareHeader]
Proxy_midiInUnprepareHeader endp

Proxy_midiOutCacheDrumPatches proc
    jmp qword ptr [g_pfn_midiOutCacheDrumPatches]
Proxy_midiOutCacheDrumPatches endp

Proxy_midiOutCachePatches proc
    jmp qword ptr [g_pfn_midiOutCachePatches]
Proxy_midiOutCachePatches endp

Proxy_midiOutClose proc
    jmp qword ptr [g_pfn_midiOutClose]
Proxy_midiOutClose endp

Proxy_midiOutGetDevCapsA proc
    jmp qword ptr [g_pfn_midiOutGetDevCapsA]
Proxy_midiOutGetDevCapsA endp

Proxy_midiOutGetDevCapsW proc
    jmp qword ptr [g_pfn_midiOutGetDevCapsW]
Proxy_midiOutGetDevCapsW endp

Proxy_midiOutGetErrorTextA proc
    jmp qword ptr [g_pfn_midiOutGetErrorTextA]
Proxy_midiOutGetErrorTextA endp

Proxy_midiOutGetErrorTextW proc
    jmp qword ptr [g_pfn_midiOutGetErrorTextW]
Proxy_midiOutGetErrorTextW endp

Proxy_midiOutGetID proc
    jmp qword ptr [g_pfn_midiOutGetID]
Proxy_midiOutGetID endp

Proxy_midiOutGetNumDevs proc
    jmp qword ptr [g_pfn_midiOutGetNumDevs]
Proxy_midiOutGetNumDevs endp

Proxy_midiOutGetVolume proc
    jmp qword ptr [g_pfn_midiOutGetVolume]
Proxy_midiOutGetVolume endp

Proxy_midiOutLongMsg proc
    jmp qword ptr [g_pfn_midiOutLongMsg]
Proxy_midiOutLongMsg endp

Proxy_midiOutMessage proc
    jmp qword ptr [g_pfn_midiOutMessage]
Proxy_midiOutMessage endp

Proxy_midiOutOpen proc
    jmp qword ptr [g_pfn_midiOutOpen]
Proxy_midiOutOpen endp

Proxy_midiOutPrepareHeader proc
    jmp qword ptr [g_pfn_midiOutPrepareHeader]
Proxy_midiOutPrepareHeader endp

Proxy_midiOutReset proc
    jmp qword ptr [g_pfn_midiOutReset]
Proxy_midiOutReset endp

Proxy_midiOutSetVolume proc
    jmp qword ptr [g_pfn_midiOutSetVolume]
Proxy_midiOutSetVolume endp

Proxy_midiOutShortMsg proc
    jmp qword ptr [g_pfn_midiOutShortMsg]
Proxy_midiOutShortMsg endp

Proxy_midiOutUnprepareHeader proc
    jmp qword ptr [g_pfn_midiOutUnprepareHeader]
Proxy_midiOutUnprepareHeader endp

Proxy_midiStreamClose proc
    jmp qword ptr [g_pfn_midiStreamClose]
Proxy_midiStreamClose endp

Proxy_midiStreamOpen proc
    jmp qword ptr [g_pfn_midiStreamOpen]
Proxy_midiStreamOpen endp

Proxy_midiStreamOut proc
    jmp qword ptr [g_pfn_midiStreamOut]
Proxy_midiStreamOut endp

Proxy_midiStreamPause proc
    jmp qword ptr [g_pfn_midiStreamPause]
Proxy_midiStreamPause endp

Proxy_midiStreamPosition proc
    jmp qword ptr [g_pfn_midiStreamPosition]
Proxy_midiStreamPosition endp

Proxy_midiStreamProperty proc
    jmp qword ptr [g_pfn_midiStreamProperty]
Proxy_midiStreamProperty endp

Proxy_midiStreamRestart proc
    jmp qword ptr [g_pfn_midiStreamRestart]
Proxy_midiStreamRestart endp

Proxy_midiStreamStop proc
    jmp qword ptr [g_pfn_midiStreamStop]
Proxy_midiStreamStop endp

Proxy_mixerClose proc
    jmp qword ptr [g_pfn_mixerClose]
Proxy_mixerClose endp

Proxy_mixerGetControlDetailsA proc
    jmp qword ptr [g_pfn_mixerGetControlDetailsA]
Proxy_mixerGetControlDetailsA endp

Proxy_mixerGetControlDetailsW proc
    jmp qword ptr [g_pfn_mixerGetControlDetailsW]
Proxy_mixerGetControlDetailsW endp

Proxy_mixerGetDevCapsA proc
    jmp qword ptr [g_pfn_mixerGetDevCapsA]
Proxy_mixerGetDevCapsA endp

Proxy_mixerGetDevCapsW proc
    jmp qword ptr [g_pfn_mixerGetDevCapsW]
Proxy_mixerGetDevCapsW endp

Proxy_mixerGetID proc
    jmp qword ptr [g_pfn_mixerGetID]
Proxy_mixerGetID endp

Proxy_mixerGetLineControlsA proc
    jmp qword ptr [g_pfn_mixerGetLineControlsA]
Proxy_mixerGetLineControlsA endp

Proxy_mixerGetLineControlsW proc
    jmp qword ptr [g_pfn_mixerGetLineControlsW]
Proxy_mixerGetLineControlsW endp

Proxy_mixerGetLineInfoA proc
    jmp qword ptr [g_pfn_mixerGetLineInfoA]
Proxy_mixerGetLineInfoA endp

Proxy_mixerGetLineInfoW proc
    jmp qword ptr [g_pfn_mixerGetLineInfoW]
Proxy_mixerGetLineInfoW endp

Proxy_mixerGetNumDevs proc
    jmp qword ptr [g_pfn_mixerGetNumDevs]
Proxy_mixerGetNumDevs endp

Proxy_mixerMessage proc
    jmp qword ptr [g_pfn_mixerMessage]
Proxy_mixerMessage endp

Proxy_mixerOpen proc
    jmp qword ptr [g_pfn_mixerOpen]
Proxy_mixerOpen endp

Proxy_mixerSetControlDetails proc
    jmp qword ptr [g_pfn_mixerSetControlDetails]
Proxy_mixerSetControlDetails endp

Proxy_mmDrvInstall proc
    jmp qword ptr [g_pfn_mmDrvInstall]
Proxy_mmDrvInstall endp

Proxy_mmGetCurrentTask proc
    jmp qword ptr [g_pfn_mmGetCurrentTask]
Proxy_mmGetCurrentTask endp

Proxy_mmTaskBlock proc
    jmp qword ptr [g_pfn_mmTaskBlock]
Proxy_mmTaskBlock endp

Proxy_mmTaskCreate proc
    jmp qword ptr [g_pfn_mmTaskCreate]
Proxy_mmTaskCreate endp

Proxy_mmTaskSignal proc
    jmp qword ptr [g_pfn_mmTaskSignal]
Proxy_mmTaskSignal endp

Proxy_mmTaskYield proc
    jmp qword ptr [g_pfn_mmTaskYield]
Proxy_mmTaskYield endp

Proxy_mmioAdvance proc
    jmp qword ptr [g_pfn_mmioAdvance]
Proxy_mmioAdvance endp

Proxy_mmioAscend proc
    jmp qword ptr [g_pfn_mmioAscend]
Proxy_mmioAscend endp

Proxy_mmioClose proc
    jmp qword ptr [g_pfn_mmioClose]
Proxy_mmioClose endp

Proxy_mmioCreateChunk proc
    jmp qword ptr [g_pfn_mmioCreateChunk]
Proxy_mmioCreateChunk endp

Proxy_mmioDescend proc
    jmp qword ptr [g_pfn_mmioDescend]
Proxy_mmioDescend endp

Proxy_mmioFlush proc
    jmp qword ptr [g_pfn_mmioFlush]
Proxy_mmioFlush endp

Proxy_mmioGetInfo proc
    jmp qword ptr [g_pfn_mmioGetInfo]
Proxy_mmioGetInfo endp

Proxy_mmioInstallIOProcA proc
    jmp qword ptr [g_pfn_mmioInstallIOProcA]
Proxy_mmioInstallIOProcA endp

Proxy_mmioInstallIOProcW proc
    jmp qword ptr [g_pfn_mmioInstallIOProcW]
Proxy_mmioInstallIOProcW endp

Proxy_mmioOpenA proc
    jmp qword ptr [g_pfn_mmioOpenA]
Proxy_mmioOpenA endp

Proxy_mmioOpenW proc
    jmp qword ptr [g_pfn_mmioOpenW]
Proxy_mmioOpenW endp

Proxy_mmioRead proc
    jmp qword ptr [g_pfn_mmioRead]
Proxy_mmioRead endp

Proxy_mmioRenameA proc
    jmp qword ptr [g_pfn_mmioRenameA]
Proxy_mmioRenameA endp

Proxy_mmioRenameW proc
    jmp qword ptr [g_pfn_mmioRenameW]
Proxy_mmioRenameW endp

Proxy_mmioSeek proc
    jmp qword ptr [g_pfn_mmioSeek]
Proxy_mmioSeek endp

Proxy_mmioSendMessage proc
    jmp qword ptr [g_pfn_mmioSendMessage]
Proxy_mmioSendMessage endp

Proxy_mmioSetBuffer proc
    jmp qword ptr [g_pfn_mmioSetBuffer]
Proxy_mmioSetBuffer endp

Proxy_mmioSetInfo proc
    jmp qword ptr [g_pfn_mmioSetInfo]
Proxy_mmioSetInfo endp

Proxy_mmioStringToFOURCCA proc
    jmp qword ptr [g_pfn_mmioStringToFOURCCA]
Proxy_mmioStringToFOURCCA endp

Proxy_mmioStringToFOURCCW proc
    jmp qword ptr [g_pfn_mmioStringToFOURCCW]
Proxy_mmioStringToFOURCCW endp

Proxy_mmioWrite proc
    jmp qword ptr [g_pfn_mmioWrite]
Proxy_mmioWrite endp

Proxy_mmsystemGetVersion proc
    jmp qword ptr [g_pfn_mmsystemGetVersion]
Proxy_mmsystemGetVersion endp

Proxy_sndPlaySoundA proc
    jmp qword ptr [g_pfn_sndPlaySoundA]
Proxy_sndPlaySoundA endp

Proxy_sndPlaySoundW proc
    jmp qword ptr [g_pfn_sndPlaySoundW]
Proxy_sndPlaySoundW endp

Proxy_timeBeginPeriod proc
    jmp qword ptr [g_pfn_timeBeginPeriod]
Proxy_timeBeginPeriod endp

Proxy_timeEndPeriod proc
    jmp qword ptr [g_pfn_timeEndPeriod]
Proxy_timeEndPeriod endp

Proxy_timeGetDevCaps proc
    jmp qword ptr [g_pfn_timeGetDevCaps]
Proxy_timeGetDevCaps endp

Proxy_timeGetSystemTime proc
    jmp qword ptr [g_pfn_timeGetSystemTime]
Proxy_timeGetSystemTime endp

Proxy_timeGetTime proc
    jmp qword ptr [g_pfn_timeGetTime]
Proxy_timeGetTime endp

Proxy_timeKillEvent proc
    jmp qword ptr [g_pfn_timeKillEvent]
Proxy_timeKillEvent endp

Proxy_timeSetEvent proc
    jmp qword ptr [g_pfn_timeSetEvent]
Proxy_timeSetEvent endp

Proxy_waveInAddBuffer proc
    jmp qword ptr [g_pfn_waveInAddBuffer]
Proxy_waveInAddBuffer endp

Proxy_waveInClose proc
    jmp qword ptr [g_pfn_waveInClose]
Proxy_waveInClose endp

Proxy_waveInGetDevCapsA proc
    jmp qword ptr [g_pfn_waveInGetDevCapsA]
Proxy_waveInGetDevCapsA endp

Proxy_waveInGetDevCapsW proc
    jmp qword ptr [g_pfn_waveInGetDevCapsW]
Proxy_waveInGetDevCapsW endp

Proxy_waveInGetErrorTextA proc
    jmp qword ptr [g_pfn_waveInGetErrorTextA]
Proxy_waveInGetErrorTextA endp

Proxy_waveInGetErrorTextW proc
    jmp qword ptr [g_pfn_waveInGetErrorTextW]
Proxy_waveInGetErrorTextW endp

Proxy_waveInGetID proc
    jmp qword ptr [g_pfn_waveInGetID]
Proxy_waveInGetID endp

Proxy_waveInGetNumDevs proc
    jmp qword ptr [g_pfn_waveInGetNumDevs]
Proxy_waveInGetNumDevs endp

Proxy_waveInGetPosition proc
    jmp qword ptr [g_pfn_waveInGetPosition]
Proxy_waveInGetPosition endp

Proxy_waveInMessage proc
    jmp qword ptr [g_pfn_waveInMessage]
Proxy_waveInMessage endp

Proxy_waveInOpen proc
    jmp qword ptr [g_pfn_waveInOpen]
Proxy_waveInOpen endp

Proxy_waveInPrepareHeader proc
    jmp qword ptr [g_pfn_waveInPrepareHeader]
Proxy_waveInPrepareHeader endp

Proxy_waveInReset proc
    jmp qword ptr [g_pfn_waveInReset]
Proxy_waveInReset endp

Proxy_waveInStart proc
    jmp qword ptr [g_pfn_waveInStart]
Proxy_waveInStart endp

Proxy_waveInStop proc
    jmp qword ptr [g_pfn_waveInStop]
Proxy_waveInStop endp

Proxy_waveInUnprepareHeader proc
    jmp qword ptr [g_pfn_waveInUnprepareHeader]
Proxy_waveInUnprepareHeader endp

Proxy_waveOutBreakLoop proc
    jmp qword ptr [g_pfn_waveOutBreakLoop]
Proxy_waveOutBreakLoop endp

Proxy_waveOutClose proc
    jmp qword ptr [g_pfn_waveOutClose]
Proxy_waveOutClose endp

Proxy_waveOutGetDevCapsA proc
    jmp qword ptr [g_pfn_waveOutGetDevCapsA]
Proxy_waveOutGetDevCapsA endp

Proxy_waveOutGetDevCapsW proc
    jmp qword ptr [g_pfn_waveOutGetDevCapsW]
Proxy_waveOutGetDevCapsW endp

Proxy_waveOutGetErrorTextA proc
    jmp qword ptr [g_pfn_waveOutGetErrorTextA]
Proxy_waveOutGetErrorTextA endp

Proxy_waveOutGetErrorTextW proc
    jmp qword ptr [g_pfn_waveOutGetErrorTextW]
Proxy_waveOutGetErrorTextW endp

Proxy_waveOutGetID proc
    jmp qword ptr [g_pfn_waveOutGetID]
Proxy_waveOutGetID endp

Proxy_waveOutGetNumDevs proc
    jmp qword ptr [g_pfn_waveOutGetNumDevs]
Proxy_waveOutGetNumDevs endp

Proxy_waveOutGetPitch proc
    jmp qword ptr [g_pfn_waveOutGetPitch]
Proxy_waveOutGetPitch endp

Proxy_waveOutGetPlaybackRate proc
    jmp qword ptr [g_pfn_waveOutGetPlaybackRate]
Proxy_waveOutGetPlaybackRate endp

Proxy_waveOutGetPosition proc
    jmp qword ptr [g_pfn_waveOutGetPosition]
Proxy_waveOutGetPosition endp

Proxy_waveOutGetVolume proc
    jmp qword ptr [g_pfn_waveOutGetVolume]
Proxy_waveOutGetVolume endp

Proxy_waveOutMessage proc
    jmp qword ptr [g_pfn_waveOutMessage]
Proxy_waveOutMessage endp

Proxy_waveOutOpen proc
    jmp qword ptr [g_pfn_waveOutOpen]
Proxy_waveOutOpen endp

Proxy_waveOutPause proc
    jmp qword ptr [g_pfn_waveOutPause]
Proxy_waveOutPause endp

Proxy_waveOutPrepareHeader proc
    jmp qword ptr [g_pfn_waveOutPrepareHeader]
Proxy_waveOutPrepareHeader endp

Proxy_waveOutReset proc
    jmp qword ptr [g_pfn_waveOutReset]
Proxy_waveOutReset endp

Proxy_waveOutRestart proc
    jmp qword ptr [g_pfn_waveOutRestart]
Proxy_waveOutRestart endp

Proxy_waveOutSetPitch proc
    jmp qword ptr [g_pfn_waveOutSetPitch]
Proxy_waveOutSetPitch endp

Proxy_waveOutSetPlaybackRate proc
    jmp qword ptr [g_pfn_waveOutSetPlaybackRate]
Proxy_waveOutSetPlaybackRate endp

Proxy_waveOutSetVolume proc
    jmp qword ptr [g_pfn_waveOutSetVolume]
Proxy_waveOutSetVolume endp

Proxy_waveOutUnprepareHeader proc
    jmp qword ptr [g_pfn_waveOutUnprepareHeader]
Proxy_waveOutUnprepareHeader endp

Proxy_waveOutWrite proc
    jmp qword ptr [g_pfn_waveOutWrite]
Proxy_waveOutWrite endp

end