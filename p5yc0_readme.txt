UrbanTerror 4.2 with dmaHD Sound System by p5yc0runn3r
p5yc0runn3r@gmail.com

This is a custom build of Urban Terror based on the original 4.2 source code with the dmaHD sound engine.

IMPORTANT!

Please backup your original Quake3-UrT.exe and config files before installing these builds!!!

dmaHD sound system features:
o Advanced 3D-Hybrid-HRTF function with Bauer Delay and Spatialization. 
o Low/High frequency band pass filtering and extraction for increased effects.
o 3 different mixers to chose from all mixing at a maximum of 44.1KHz 
o Automatic memory management! (No need for CVAR's)
o Low CPU usage!
o Logarithmic attenuation in different mediums!
o Speed-of-sound mapping with Doppler in air and water!
o Weapon sounds are more pronounced.
o Faithful to original listening distance of default Quake 3 sound.
o Increased sound quality with cubic/Hermite 4-point spline interpolation.

For best listening experience use good quality headphones with good bass response.

Following are the CVARS this new build gives:
/dmaHD_enable 
This will enable (1) or disable (0) dmaHD. 
Default: "1"

/dmaHD_interpolation
This will set the type of sound re-sampling interpolation used.
0 = No interpolation
1 = Linear interpolation
2 = 4-point Cubic spline interpolation
3 = 4-point Hermite spline interpolation
(This option needs a total game restart after change)
Default: "3"

/dmaHD_mixer
This will set the active mixer:
10 = Hybrid-HRTF [3D]
11 = Hybrid-HRTF [2D]
20 = dmaEX2
21 = dmaEX2 [No reverb]
30 = dmaEX
(This option changes mixers on the fly)
Default: "10"

/in_mouse
Set to "2" to enable RAW mouse input.
(This option needs a total game restart after change)
Default: "1"

The following are some other CVARS that affect dmaHD. Please set them as specified:
/com_soundMegs
This has no effect anymore. This should be set to default "8"
(This option needs a total game restart after change)

/s_khz 
Set to "44" for best sound but lower it to "22" or "11" in case of FPS drops.
(This option needs a total game restart after change)

/s_mixahead
This is for fine-tuning the mixer. It will mix ahead the number of seconds specified.
The more you increase the better the sound but it will increase latency which you do not want.
(This option needs a total game restart after change)
Default: "0.1"

/s_mixPreStep
This is for fine-tuning the mixer. It will mix this number of seconds every mixing step.
The more you increase the better the sound but it will increase drastically the amount of processing power needed.
(This option needs a total game restart after change)
Default: "0.05"
