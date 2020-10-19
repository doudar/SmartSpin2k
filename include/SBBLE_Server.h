#ifndef SBBLE_SERVER_H
#define SBBLE_SERVER_H

void startBLEServer();
void BLENotify();
bool connectToServer();
void BLEServerScan(bool connectRequest);
void bleClient();
void startBleClient();
double computePID(double, double);
void computeERG(int, int);
void setupBLE();
void computeCSC();

#endif