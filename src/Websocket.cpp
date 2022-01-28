/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "Websocket.h"

WebSocket WebSocket::INSTANCE = WebSocket();
WebSocketsServer webSocket    = WebSocketsServer(81);

void WebSocket::start() {
  webSocket.begin();
  webSocket.onEvent(WebSocket::INSTANCE.webSocketEvent);
}

void WebSocket::loop() { webSocket.loop(); }

void WebSocket::log(const char *format, va_list args) { WebSocket::INSTANCE.log_internal(format, args); }

void WebSocket::log_internal(const char *format, va_list args) {
  if (WiFi.status() == WL_CONNECTED && webSocket.connectedClients()) {
    const size_t buffer_size = 255;
    char buffer[buffer_size];

    vsnprintf(buffer, buffer_size, format, args);
    webSocket.broadcastTXT((uint8_t *)buffer);
  } else {
    // ESP_LOGE(UDP_LOGGER_TAG, "UPD not logging. No WIFI connection.");
  }
}

void WebSocket::webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length){

/*switch(type) {
        case WStype_DISCONNECTED:
            USE_SERIAL.printf("[%u] Disconnected!\n", num);
            break;
        case WStype_CONNECTED:
            {
                IPAddress ip = webSocket.remoteIP(num);
                USE_SERIAL.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", num, ip[0], ip[1], ip[2], ip[3], payload);

				// send message to client
				webSocket.sendTXT(num, "Connected");
            }
            break;
        case WStype_TEXT:
            USE_SERIAL.printf("[%u] get Text: %s\n", num, payload);

            // send message to client
            // webSocket.sendTXT(num, "message here");

            // send data to all connected clients
            // webSocket.broadcastTXT("message here");
            break;
        case WStype_BIN:
            USE_SERIAL.printf("[%u] get binary length: %u\n", num, length);
            hexdump(payload, length);

            // send message to client
            // webSocket.sendBIN(num, payload, length);
            break;
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
    }
*/


}