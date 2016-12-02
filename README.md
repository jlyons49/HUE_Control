# HUE_Control
Various software tools for controlling Phillips HUE Lights

#Dependencies
Currently dependent on:

libcurl14

wiringPi

#Building project
Currently recommend calling as such:
gcc -o hue_control lux_modify.c -lwiringPi -lcurl
