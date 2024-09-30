# formuls
*formuls* is an experimental electronic musical instrument for synthesising and sculpting sound live!

![alt text](https://github.com/jrdooley/formuls/blob/main/formuls_image_1.png)

Using an intuitive, novel touchscreen-based interface, formuls can sculpt primary sounds into kaleidoscopic meshes of finely tuned timbres and pulsating rhythms. Harnessing the power of simple gestural interactions typically used to operate smartphones and tablets, complex sound synthesis techniques are easy to access, control and explore, reducing the required instrumental technique to finger taps, drags and swipes. formuls aims to make synthesis easy for the beginner, while opening up a plethora of creative sonic possibilities to the seasoned sound designer.

This software uses [libpd](https://github.com/libpd/libpd), [Faust](https://faust.grame.fr/), [Open Stage Control](https://openstagecontrol.ammd.net/) and [Python](https://www.python.org/).

***
### Instrument features
- 6 x polyphonic 2 operator FM synthesisers, each with integrated audio effects and generative polyrhythmic sequencer.
- Touchscreen interface accessed via a web browser on an iPad/Android device.
- Record and automate gestural interactions with parameters.
- Modulate parameters with LFOs, 'chaos', and audio signals from other synth instances.
- Master bus effects including reverb, rolling spectral freezer, repeater, digitaliser, and 'bus smashing' limiter.
- Microtonal scale tuning.
- Ableton Link integration.

![alt text](https://github.com/jrdooley/formuls/blob/main/formuls_image_2.png)

***
### Running the app
Currently, there are only Apple Silicon builds for *formuls* and this has only been tested on an M1 processor—YMMV!

To run formuls:
- Launch the formuls app.
- Select your audio output and the number of output channels (2 is default, though you can isolate each individual synth channel as well as master bus effects).
- Click "Start formuls".
- To access the control GUI on the computer ruunning the formuls app, you can launch a web browser and navigate to the following address: 127.0.0.1:9001.
- To access the control GUI on an iPad or Android tablet, make sure your touchscreen device is connected to the same network, determine the IP address of the computer running the formuls app (e.g. 192.168.0.1), open a browser tab on your device and navigate to the computer's IP address with the port address of 9001. For example, 192.168.0.1:9001. Note: the interface is designed with large screens in mind, ideally 12" (or bigger).

***
### Demos
Watch the video below introducing formuls:

[![alt text](https://img.youtube.com/vi/Z_R3phNMLHo/0.jpg)](https://youtu.be/Z_R3phNMLHo)

Listen to music made with formuls (the instrument) by formuls (the artist who made the instrument):

https://formuls.bandcamp.com/

***
This work is licensed under a [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International License](http://creativecommons.org/licenses/by-nc-sa/4.0/), James Dooley aka formuls 2014-24.
