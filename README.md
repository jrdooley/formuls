# formuls
*formuls* is an experimental electronic musical instrument for synthesising and sculpting sound live!

![alt text](https://github.com/jrdooley/formuls/blob/main/formuls_image_1.png)

*formuls* makes sound synthesis easy for the beginner, while opening up a plethora of creative sonic possibilities to the seasoned sound designer. Using an intuitive, novel touchscreen-based interface, *formuls* can sculpt primary sounds into kaleidoscopic meshes of finely tuned timbres and pulsating rhythms. Harnessing the power of simple gestural interactions typically used to operate smartphones and tablets, complex sound synthesis techniques are easy to access, control and explore, reducing the required instrumental technique to finger taps, drags and swipes.

This software uses [JUCE](https://juce.com/), [libpd](https://github.com/libpd/libpd), [Faust](https://faust.grame.fr/) and [Open Stage Control](https://openstagecontrol.ammd.net/).

***
### Instrument features
- 6 x polyphonic 2 operator FM synthesisers, each with integrated audio effects and generative polyrhythmic sequencer.
- Touchscreen interface accessed via a web browser on an iPad/Android device.
- Record and automate gestural interactions with parameters.
- Modulate parameters with LFOs, 'chaos', and audio signals from other synth instances.
- Master bus effects including reverb, rolling spectral freezer and repeater effect.
- Microtonal scale tuning.
- Ableton Link integration.

![alt text](https://github.com/jrdooley/formuls/blob/main/formuls_image_2.png)

***
### Running the app
Currently, there are only Apple Silicon builds for *formuls* and this has only been tested on an M1 processor—YMMV!

To run formuls:
- Launch the formuls app.
- Select your audio output, the number of output channels (2 is default, though you can isolate each individual synth channel as well as master bus effects), and the sample rate (48 kHz is default; if your audio interface cannot run at the chosen rate, formuls uses the nearest rate it can and tells you).
- Click "Start formuls". The app then lists every web address the control GUI can be opened at — you no longer need to look up your computer's IP address yourself.
- To access the control GUI on the computer running the formuls app, launch a web browser and go to the address listed under "On this machine" (127.0.0.1:9001).
- To access the control GUI on an iPad or Android tablet, make sure your touchscreen device is connected to the same network as the computer, then open a browser tab on your device and go to one of the addresses listed under "On a tablet or phone on the same network" (for example, 192.168.0.1:9001). If more than one is listed, try them in order — a computer connected to several networks has an address on each. Note: the interface is designed with large screens in mind, ideally 12" (or bigger).
- Click "Stop formuls" to shut the sound engine and control GUI down without quitting the app; closing the window quits formuls entirely.

***
### Demos
Watch the video below introducing formuls:

[![alt text](https://img.youtube.com/vi/Z_R3phNMLHo/0.jpg)](https://youtu.be/Z_R3phNMLHo)

Listen to music made with formuls (the instrument) by formuls (the artist who made the instrument):

https://formuls.bandcamp.com/

***
This work is licensed under a [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-nc-sa/4.0/), James Dooley aka formuls 2014-26.
