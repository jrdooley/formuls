/*
 * Copyright (c) 2014 Rafael Vega <rvega@elsoftwarehamuerto.org>
 *
 * BSD Simplified License.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 *
 * See https://github.com/libpd/libpd for documentation
 *
 */
#include <iostream>
#include <unistd.h>
#include <stdlib.h>

#include "PdBase.hpp"
#include "RtAudio.h"
#include "PdObject.h"

RtAudio audio;
pd::PdBase lpd;
PdObject pdObject;

int audioCallback(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames, double streamTime, RtAudioStreamStatus status, void *userData){

   // pass audio samples to/from libpd
   int ticks = nBufferFrames / 64; // JD: don't change the "64" value
   lpd.processFloat(ticks, (float *)inputBuffer, (float*)outputBuffer);

   return 0;
}

void init(int thepatch, int deviceSelect, int channelsOut){  // JD: "int deviveSelect" takes the argument that sets the audio device
   unsigned int sampleRate = 48000;
   unsigned int bufferFrames = 512; // JD: this value can be changed. 512 is the lowest value though

   // init pd
   if(!lpd.init(0, channelsOut, sampleRate)) {
      std::cerr << "Could not init pd" << std::endl;
      exit(1);
   }

   // receive messages from pd
   lpd.setReceiver(&pdObject);
   lpd.subscribe("cursor");

   // send DSP 1 message to pd
   lpd.computeAudio(true);

   // load the patch
   // pd::Patch patch = lpd.openPatch("_main.pd", "./pd");
   if (thepatch == 0) {
     pd::Patch patch = lpd.openPatch("_main-control.pd", "./pd");
     std::cout << patch << std::endl;
   } else {
     pd::Patch patch = lpd.openPatch("_main-audio.pd", "./pd");
     std::cout << patch << std::endl;
   }

   // use the RtAudio API to connect to the default audio device
   if(audio.getDeviceCount()==0){
      std::cout << "There are no available sound devices." << std::endl;
      exit(1);
   }

   RtAudio::StreamParameters parameters;
   parameters.deviceId = deviceSelect; //JD: selects output device based on the incoming deviceSelect variable
   parameters.nChannels = channelsOut; // JD: sets number of audio output channels. first two are clean, 3 + 4 are reverb

   RtAudio::StreamOptions options;
   options.streamName = "libpd rtaudio test";
   options.flags = RTAUDIO_SCHEDULE_REALTIME;
   if(audio.getCurrentApi() != RtAudio::MACOSX_CORE) {
      options.flags |= RTAUDIO_MINIMIZE_LATENCY; // CoreAudio doesn't seem to like this
   }
   try {
      audio.openStream( &parameters, NULL, RTAUDIO_FLOAT32, sampleRate, &bufferFrames, &audioCallback, NULL, &options );
      audio.startStream();
   }
   catch(RtAudioError& e) {
      std::cerr << e.getMessage() << std::endl;
      exit(1);
   }
}

// JD: this is the main part of the program.
// first argument (0/1) chooses which pd patch to load: control/audio
// 2nd argument (0,1,2,...) choose the audio device number.
// 3rd argument sets number of output channels.
int main (int argc, char *argv[]) {
    int audioDevice;
    int channelOut;
    int pdPatch;

 if (argc > 2) {
   pdPatch = atoi(argv[1]);
   audioDevice = atoi(argv[2]);
   channelOut = atoi(argv[3]);
 } else {
   // sets to default audio device "0" with two channel audio out.
   audioDevice = 0;
   channelOut = 2;
   pdPatch = 0;
 }
  init(pdPatch, audioDevice, channelOut);

   // keep the program alive until it's killed with Ctrl+C
   while(1){
      lpd.receiveMessages();
      lpd.sendFloat("FromCpp", 578);
      usleep(100);
   }

   return 0;
}
