#ifndef SMARTBIKE_PARAMETERS_H
#define SMARTBIKE_PARAMETERS_H

// define all data parameters
#include <Arduino.h>

/*espidf stuff
#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "sdkconfig.h"
*/
//bool readConfigFile(char*, bool);

class userParameters
{
    private:
    String filename = "/config.txt";
    float incline;
    int simulatedWatts;
    int simulatedHr;
    float inclineStep;
    int shiftStep;
    float inclineMultiplier;
    bool simulatePower;
    bool simulateHr;
    bool ERGMode;
    bool wifiOn;
    String ssid;
    String password;
    public:
   
    const char* getFilename()           {return filename.c_str();}
    float       getIncline()            {return incline;}
    int         getSimulatedWatts()     {return simulatedWatts;}
    int         getSimulatedHr()        {return simulatedHr;}
    float       getInclineStep()        {return inclineStep;}
    int         getShiftStep()          {return shiftStep;}
    float       getInclineMultiplier()  {return inclineMultiplier;}
    bool        getSimulatePower()      {return simulatePower;}
    bool        getSimulateHr()         {return simulateHr;}
    bool        getERGMode()            {return ERGMode;}
    bool        getWifiOn()             {return wifiOn;}
    const char* getSsid()               {return ssid.c_str();}
    const char* getPassword()           {return password.c_str();}

    void    setDefaults();
    void    setFilename(String flnm)         {filename = flnm;}
    void    setIncline(float inc)            {incline = inc;}
    void    setSimulatedWatts(int w)         {simulatedWatts = w;}
    void    setSimulatedHr(int hr)           {simulatedHr = hr;}
    void    setInclineStep(float is)         {inclineStep = is;}
    void    setShiftStep(int ss)             {shiftStep = ss;}
    void    setInclineMultiplier(float im)   {inclineMultiplier = im;}
    void    setSimulatePower(bool sp)        {simulatePower = sp;}
    void    setSimulateHr(bool shr)          {simulateHr = shr;}
    void    setERGMode(bool erg)             {ERGMode = erg;}
    void    setWifiOn(bool wifi)             {wifiOn = wifi;}
    void    setSsid(String sid)              {ssid = sid;}
    void    setPassword(String pwd)           {password = pwd;} 
  
    String  returnJSON();
    void    saveToSPIFFS();
    void    loadFromSPIFFS();
    void    printFile();

};

#endif

