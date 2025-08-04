# Minimal application to test the mp3 decoing capabilities of esp-adf
This is a minimal application to test out the mp3 decoding capabilities of esp-adf
Its primary purpose was to compare with the arduino project

ESP8266audio at https://github.com/earlephilhower/ESP8266Audio

Using the I2SNODAC output device.

This project is extremely good but It seemed to run out of steam and stutter so I got the impression that the esp32 was not able
to handle MP3 decoding.
Using an oscilloscope it seemed to be continuously in the MP3 decode loop

The whole project was being developed to create a minimal board that cold be used as an audio source in a vintage radio
to replace the AM bands which are disapearing.
The lower quality generated was not an issue here because the old radios are not really 'HIFI'

## Development

This was developed using the visual studio code IDE with the esp-idf extension installed

Added to the esp-idf extension is the esp-adf support library.
Which is installed into the esp-idf extension using 'Install ESP-ADF'

It was first developed and used on an esp32 dev board with a built in uart and may work as it is.

This is my first project using esp-idf and the environment is a fairly high learning curev to take on.

### using a different board
If using a different board and /or processor change the esp-idf processor using 
'Set Expressive Device Target (IDF_TARGET)'
this will reset all the configuration and preset #defines for the new processor

The following may be easier to read without wordwrap on the editor
using idf.py menuconfig  (note <esc> takes you back a level)

1. Set the Serial flasher config (default is 2MB and 40Mhz)

    (top) Serial flasher config  ---> Flash size (4 MB)
    (top) Serial flasher config  ---> Flash SPI speed (80 MHz)
2. Set partition table to custom partition which gives a fiilesystem partition and 2 app partitions
    this is an option of the 1st row which by default may be -> Partition Table [Single factory app, no OTA]
    (top) Partition Table  --->    Partition Table (Custom partition table CSV)  --->
    once this is selected then the partition name can be set using
    (top) Partition Table  ---> (partitions.csv) Custom partition CSV file
    once this is selected change the table to partitions_audio1.csv
    (top) Partition Table  ---> (partitions_audio1.csv) Custom partition CSV file
3. Set the audio board to custom
    If you are not using a recognised audio board (which is most likely) then a custom [empty] board is provided in the components directory
    Select this (may be another default board for a different processor)
    (top) Audio HAL  ---> Audio board (ESP32-Lyrat V4.3)  --->
    once this is selected choose custom board
    (top) Audio HAL  ---> Audio board (Custom audio board)  --->
4. Select allow potentially insecure options and skip server cert verification
    NOTE:   THIS IS NOT NORMALL DONE BUT THE CERTIFICATES WOULD NEED TO BE KNOWN FOR ALL OF THE INTERNET RADIO SITES YOU MAY WANT TO CONNECT TO.
            THIS IS PROBABLY IMPRACTICAL AND IF WE ARE ONLY STREAMING DOWN AUDIO THEN MAYBE SAFE TO IGNORE SSL CHECKING
    (top) Component config  ---> ESP-TLS  --->  [ ] Allow potentially insecure options
    select this and then also select opened etra option
    (top) Component config  ---> ESP-TLS  --->  [ ] Allow potentially insecure options. ---> [ ]     Skip server certificate verification by default (WARNING: ONLY FOR TESTING PURPOSE, READ HELP) (NEW)

5   Set the CPU clock frequency - 
    (top) Component config  ---> ESP System Settings  ---> CPU frequency (240 MHz)  --->

6   Choose channel for console output - default of UART0 is OK for most ESP32 dev boards with onboard serial to usb using the standard uart ports
    It gets more complicated with esp32s2, esp32s3, esp32c3 etc wheser there is an onchip serial usb converter but this is where you set it
    (top) Component config  ---> ESP System Settings  ---> Channel for console output (Default: UART0)  --->

7   Enable FFREETOS Runtime stats - used by the task_monitor task_monitor
    (top) Component config  ---> FreeRTOS  ---> Kernel --->  [*] configGENERATE_RUN_TIME_STATS
    selecting this will open another option of saving task core ID which also needs enabling (opens above this entry)
    (top) Component config  ---> FreeRTOS  ---> Kernel --->   [*] Enable display of xCoreID in vTaskList (NEW)

Save options changed by menuconfig (press S) and exit.

