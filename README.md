# NightMare Network Lib

<p align="justify">
This is a library that contains some functions, classes and structures used by different projects across the <i> NightMare Network </i> Which is my own home automation environment. Having this code separeted in here makes it easier to mantain and sync the code used for the same stuff on all the projects. Also makes it easier for me to implement new features as I add them in the controller side, or if I have a better solution to a common problem acoss different devices.
And at Last, with <b>PlatafromIO</b> I can easily set a github repository as a dependancy of the project making the part of creating/porting new projects also easier. This funcion also helps when refactoring old projects to include this (or any other) Lib.
</p>

## Structure

The structure of this Lib is as follows:

+ Outter .h
  + Core Folder
    + Commonly used structs and functions throughout differents projects.
  + Services Folder
    + Classes that encapsulates a controller for another device on the network
  + Xtra Folder
    + Some extra components that are not really core nor a service but is still common code across multiple devices on the network

## Current Services

1. Ac Controller
2. Light Controller
3. Light with Color Controller

## Core Functionalities

1. Timers.
2. MQTT Client using esp mqtt.
3. Server Variables.
4. Misc. Functions
   + Convert Timestamp to human readable String
   + If LVGL is detected also compiles functions to easily hide/show objects and set/clear flags (specialy the Checked)
5. TimeSynchronization function.

## Xtra Functions

1. Scheduler
2. Command Resolver
