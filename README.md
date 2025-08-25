# sgt2wav
DirectMusic SGT to WAV command-line file conversion utility

## Note
This requires Windows 7, Windows 8, Vista, or an early version of Windows 10.  Later versions of Windows 10, and Windows 11, are not supported as Microsoft removed OS support for the underlying DirectMusic interface.

DirectMusic was a Microsoft API that allowed music and sound effects to be composed and played using a flexible, interactive interface. A large number of games and other multimedia products relied on DirectMusic not only for its flexibility, but because it provided a high-level audio interface to Microsoft’s DirectSound API.

Although DirectMusic was deprecated with the release of Windows Vista, and barely runs under Windows 8, a significant amount of legacy audio content is still stored in DirectMusic segment (SGT), RMI (RMI), and MIDI (MID) files.

The purpose of this command-line utility is to allow legacy DirectMusic files to be captured in real-time and saved as uncompressed WAV files. An audio device is required, as the DirectMusic file is played back in real-time, and the data for the wave file is captured directly from the low-level audio mixer.
