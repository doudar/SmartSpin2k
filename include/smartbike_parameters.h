// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
#ifndef SMARTBIKE_PARAMETERS_H
#define SMARTBIKE_PARAMETERS_H

#include <Arduino.h>

class userParameters
{
    private:
    String filename = "/config.txt";
    float incline;
    int simulatedWatts;
    int simulatedHr;
    int simulatedCad;
    float inclineStep;
    int shiftStep;
    float inclineMultiplier;
    bool simulatePower;
    bool simulateHr;
    bool ERGMode;
    bool wifiOn;
    String ssid;
    String password;
    String foundDevices = " ";
    String connectedPowerMeter = " ";
    String connectedHeartMonitor = " ";

    public:
    const char* getFilename()                {return filename.c_str();}
    float       getIncline()                 {return incline;}
    int         getSimulatedWatts()          {return simulatedWatts;}
    int         getSimulatedHr()             {return simulatedHr;}
    float         getSimulatedCad()            {return simulatedCad;}
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
    const char* getconnectedPowerMeter()        {return connectedPowerMeter.c_str();}
    const char* getconnectedHeartMonitor()        {return connectedHeartMonitor.c_str();}

    void    setDefaults();
    void    setFilename(String flnm)         {filename = flnm;}
    void    setIncline(float inc)            {incline = inc;}
    void    setSimulatedWatts(int w)         {simulatedWatts = w;}
    void    setSimulatedHr(int hr)           {simulatedHr = hr;}
    void    setSimulatedCad(float cad)         {simulatedCad = cad;}
    void    setInclineStep(float is)         {inclineStep = is;}
    void    setShiftStep(int ss)             {shiftStep = ss;}
    void    setInclineMultiplier(float im)   {inclineMultiplier = im;}
    void    setSimulatePower(bool sp)        {simulatePower = sp;}
    void    setSimulateHr(bool shr)          {simulateHr = shr;}
    void    setERGMode(bool erg)             {ERGMode = erg;}
    void    setWifiOn(bool wifi)             {wifiOn = wifi;}
    void    setSsid(String sid)              {ssid = sid;}
    void    setPassword(String pwd)          {password = pwd;} 
    void    setFoundDevices(String fdev)     {foundDevices = fdev;};
    void    setConnectedPowerMeter(String cpm) {connectedPowerMeter = cpm;}
    void    setConnectedHeartMonitor(String cHr) {connectedHeartMonitor = cHr;}
  
    String  returnJSON();
    void    saveToSPIFFS();
    void    loadFromSPIFFS();
    void    printFile();


};

#endif

