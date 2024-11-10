# Zihao's Work

`---- Tracker` => This is the source folder to be compiled by Arduino and executed in the Tracker \
`  |---- Tracker.ino` => Main program \
`  |---- FramManager.h` => Management of FRAM storage, used by `Tracker.ino` \
`---- EdgeDevice` => Python programs running in the edge device \
`  |---- receiveBLE.py` => Receives audio transmitted via BLE from the Tracker. Requires Bleak v0.22 and Scipy \
`  |---- callWhisperCpp.py` => Scans for saved audio that has not been updated for a while and calls Whisper.Cpp to transcribe it. This program requires a locally compiled EXE of Whisper.Cpp. The project and installation instructions can be found at https://github.com/ggerganov/whisper.cpp
