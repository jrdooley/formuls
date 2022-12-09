#!/usr/bin/env python3

import sounddevice as sd
import os
from tkinter import *

# Create main window
mainWindow =Tk()
mainWindow.geometry("500x100")
mainWindow.title("formuls")
mainWindow.configure(bg='green')

# Query the available audio devices
audioOptions = sd.query_devices()
deviceName = []
devConcat = str
x = 0
for i in audioOptions:
    devConcat = str(x) + " " + (i['name'])
    deviceName.append(devConcat)
    x = x + 1

#Access the Menu Widget using StringVar function
clicked = StringVar()
clicked.set("Select audio output...") # set optionMenu initial value

#Create an instance of Menu in the frame
audioDeviceMenu = OptionMenu(mainWindow, clicked, *deviceName)
audioDeviceMenu.pack()
audioDeviceMenu.configure(bg='green')
audioDeviceMenu.place(relx = 0.2, rely = 0.5, anchor = 'center')

# function to access the selected menu item, then run formulsengine and node/open stage control
runx = 1
def RUN():
    deviceGet = clicked.get()
    audioOutput = deviceGet[0]
    command1 = "./gui/node ./gui/open-stage-control/ -s 127.0.0.1:9000 -o 9001 -l ./gui/_main.json --client-options framerate=25 hdpi=0 &" # run o-s-c interface
    command2 = "./formulsengine 1 " + audioOutput + " 16 &" # audio process
    command3 = "./formulsengine 0 " + audioOutput + " 1 &" # control process
    os.system(command1)
    os.system(command2)
    os.system(command3)

# function to stop formulsengines and node/open stage control
def STOP():
    os.system("killall formulsengine")
    os.system("killall node")

# function to kill all subprocesses and quit
def QUIT():
    os.system("killall formulsengine")
    os.system("killall node")
    quit()

# load images for run, stop and quit buttons
runicon = PhotoImage(file = r"icons/run.png")
runicon = runicon.subsample(2,2)
stopicon = PhotoImage(file = r"icons/stop.png")
stopicon = stopicon.subsample(2,2)
quiticon = PhotoImage(file = r"icons/quit.png")
quiticon = quiticon.subsample(3,3)

# create run, stop and quit buttons
runButton = Button(mainWindow, fg = 'green', border = 0, image = runicon, command=RUN)
runButton.pack()
runButton.place(relx = 0.5, rely = 0.5, anchor = 'center')

stopButton = Button(mainWindow, fg = 'green', border = 0, image = stopicon, command=STOP)
stopButton.pack()
stopButton.place(relx = 0.62, rely = 0.5, anchor = 'center')

quitButton = Button(mainWindow, fg = 'green', border = 0, image = quiticon, command=QUIT)
quitButton.pack()
quitButton.place(relx = 0.9, rely = 0.5, anchor = 'center')

mainWindow.mainloop()