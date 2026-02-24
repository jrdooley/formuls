#!/usr/bin/env python3

import sounddevice as sd

devices = sd.query_devices()

print(devices)
