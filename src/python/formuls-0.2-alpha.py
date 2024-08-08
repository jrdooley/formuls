import sounddevice as sd
import os
import sys
from tkinter import *
import pathlib


# resource_path function to get the absolute path to resource, works for dev and for PyInstaller
def resource_path(relative_path):
    try:
        # PyInstaller creates a temp folder and stores path in _MEIPASS
        base_path = sys._MEIPASS
    except Exception:
        base_path = os.path.abspath(".")

    return os.path.join(base_path, relative_path)

# Create main window
mainWindow = Tk()
mainWindow.geometry("450x300")
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

#Access the audioDevice Menu Widget using StringVar function
audioDeviceClicked = StringVar()
audioDeviceClicked.set("Select audio output...") # set optionMenu initial value

#Create an instance of Menu in the frame for audiodevices
audioDeviceMenu = OptionMenu(mainWindow, audioDeviceClicked, *deviceName)
audioDeviceMenu.pack()
audioDeviceMenu.configure(bg='green', width = 25)
audioDeviceMenu.place(relx = 0.05, rely = 0.2)

#Access the outputChannels Menu Widget using StringVar function
outputChannelsClicked = StringVar()
outputChannelsClicked.set("Channels...") # set optionMenu initial value
channels = [2, 16]
outputChannels = StringVar()
outputChannels.set(channels[1])

#Create an instance of outputChannels Menu in the frame to select the number of audio output (stereo or individual channel outputs)
outputChannelsMenu = OptionMenu(mainWindow, outputChannelsClicked, *channels)
outputChannelsMenu.pack()
outputChannelsMenu.configure(bg='green', width = 6)
outputChannelsMenu.place(relx = 0.05, rely = 0.35)

# RUN function to access the selected menu item, then run formulsengine and node/open stage control when 'Start' is clicked.
# This also changes the 'runButton' label.
is_on = True
runButtonText = StringVar()
runButtonText.set("Start formuls")

def RUN():
    global is_on
    if is_on:
        deviceGet = audioDeviceClicked.get()
        audioOutput = deviceGet[0]
        channelsOut = outputChannels.get()
        
        # command0 = "cd " + resource_path("")
        command1 = resource_path("gui/node") + " " + resource_path("gui/open-stage-control/") + " --send 127.0.0.1:9000 --port 9001 --load " + resource_path("gui/_main.json") + " --client-options framerate=25 hdpi=0 &" # run o-s-c interface
        command2 = "cd " + resource_path("") + "; " + resource_path("pd/formulsengine") + " 1 " + audioOutput + " " + channelsOut + " &" # audio process
        command3 = "cd " + resource_path("") + "; " + resource_path("pd/formulsengine") + " 0 " + audioOutput + " 1 &" # control process
        
        # os.system(command0)
        os.system(command1)
        os.system(command2)
        os.system(command3)
        os.system("pwd")
        
        # change button label
        runButtonText.set("Stop formuls")
        is_on = False
    
    else:
        # os.system(command0)
        os.system("killall formulsengine")
        os.system("killall node")

        # change button label
        runButtonText.set("Start formuls")
        is_on = True


# QUIT function to kill all subprocesses and exit the program
def QUIT():
    os.system("killall formulsengine")
    os.system("killall node")
    sys.exit()

# load images for buttons. Currently deprecated, but an interface redesign might make this useful
# runicon = PhotoImage(file = resource_path("icons/run.png"))
# runicon = runicon.subsample(2,2)

# create run and quit buttons
runButton = Button(mainWindow, textvariable = runButtonText, width = 10, height = 2, command=RUN)
runButton.pack()
runButton.place(relx = 0.05, rely = 0.5)

quitButton = Button(mainWindow, text = "Quit", height = 2, command=QUIT)
quitButton.pack()
quitButton.place(relx = 0.8, rely = 0.9, anchor = 'center')

mainWindow.mainloop()