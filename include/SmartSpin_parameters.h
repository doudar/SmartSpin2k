// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
#ifndef SmartSpin_PARAMETERS_H
#define SmartSpin_PARAMETERS_H

#include <Arduino.h>

class userParameters
{
    private:
    String  filename = "/config.txt";       //Used Static -Remove              String FirmwareUpdateURL would be nice instead
    float   incline;                        //Keep
    int     simulatedWatts;                 //Keep
    int     simulatedHr;                    //Keep
    int     simulatedCad;                   //Keep
    float   inclineStep;                    //Not Used - Remove                 String MDNS name would be nice instead
    int     shiftStep;                      //Keep
    float   inclineMultiplier;              //Keep
    bool    simulatePower;                  //Keep
    bool    simulateHr;                     //Keep
    bool    ERGMode;                        //Keep
    bool    wifiOn;                         //Depreciated - Remove              bool automaticUpdates would be nice instead
    String  ssid;                           //Keep
    String  password;                       //Keep
    String  foundDevices = " ";             //Keep
    String  connectedPowerMeter = " ";      //Keep
    String  connectedHeartMonitor = " ";    //Keep    

    public:
    const char* getFilename()                {return filename.c_str();}
    float       getIncline()                 {return incline;}
    int         getSimulatedWatts()          {return simulatedWatts;}
    int         getSimulatedHr()             {return simulatedHr;}
    float       getSimulatedCad()            {return simulatedCad;}
    float       getInclineStep()             {return inclineStep;}
    int         getShiftStep()               {return shiftStep;}
    float       getInclineMultiplier()       {return inclineMultiplier;}
    bool        getSimulatePower()           {return simulatePower;}
    bool        getSimulateHr()              {return simulateHr;}
    bool        getERGMode()                 {return ERGMode;}
    bool        getWifiOn()                  {return wifiOn;}
    const char* getSsid()                    {return ssid.c_str();}
    const char* getPassword()                {return password.c_str();}
    const char* getFoundDevices()            {return foundDevices.c_str();}
    const char* getconnectedPowerMeter()     {return connectedPowerMeter.c_str();}
    const char* getconnectedHeartMonitor()   {return connectedHeartMonitor.c_str();}

    void    setDefaults();
    void    setFilename(String flnm)            {filename = flnm;}
    void    setIncline(float inc)               {incline = inc;}
    void    setSimulatedWatts(int w)            {simulatedWatts = w;}
    void    setSimulatedHr(int hr)              {simulatedHr = hr;}
    void    setSimulatedCad(float cad)          {simulatedCad = cad;}
    void    setInclineStep(float is)            {inclineStep = is;}
    void    setShiftStep(int ss)                {shiftStep = ss;}
    void    setInclineMultiplier(float im)      {inclineMultiplier = im;}
    void    setSimulatePower(bool sp)           {simulatePower = sp;}
    void    setSimulateHr(bool shr)             {simulateHr = shr;}
    void    setERGMode(bool erg)                {ERGMode = erg;}
    void    setWifiOn(bool wifi)                {wifiOn = wifi;}
    void    setSsid(String sid)                 {ssid = sid;}
    void    setPassword(String pwd)             {password = pwd;} 
    void    setFoundDevices(String fdev)        {foundDevices = fdev;};
    void    setConnectedPowerMeter(String cpm)  {connectedPowerMeter = cpm;}
    void    setConnectedHeartMonitor(String cHr){connectedHeartMonitor = cHr;}
  
    String  returnJSON();
    void    saveToSPIFFS();
    void    loadFromSPIFFS();
    void    printFile();


};

#endif

