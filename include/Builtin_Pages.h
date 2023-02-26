/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

// OTA Update pages. Not stored in LittleFS because we will use this to restore
// the webserver files if they get corrupt.
/* Style */
#pragma once

#include <Arduino.h>

String OTAStyle = "<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:"
                  "10px auto;font-size:15px}"
                  "input{background:#f1f1f1;border:0;padding:0 "
                  "15px}body{background:#031cbd;font-family:sans-serif;font-size:14px;color:#"
                  "777}"
                  "#file-input{padding:0;border:1px solid "
                  "#ddd;line-height:44px;text-align:left;display:block;cursor:pointer}"
                  "#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-"
                  "color:#3498db;width:0%;height:10px}"
                  "form{background:#fff;max-width:258px;margin:75px "
                  "auto;padding:30px;border-radius:5px;text-align:center}"
                  ".btn{background:#031cbd;color:#fff;cursor:pointer}</style>";

/* Login page */
String OTALoginIndex = "<form name=loginForm>"
                       "<h1>ESP32 Login</h1>"
                       "<input name=userid placeholder='User ID'> "
                       "<input name=pwd placeholder=Password type=Password> "
                       "<input type=submit onclick=check(this.form) class=btn value=Login></form>"
                       "<script>"
                       "function check(form) {"
                       "if(form.userid.value=='admin' && form.pwd.value=='admin')"
                       "{window.open('/OTAIndex')}"
                       "else"
                       "{alert('Error Password or Username')}"
                       "}"
                       "</script>" +
                       OTAStyle;

String noIndexHTML = "<!DOCTYPE html>"
                     "<html>"
                     "<body>"
                     "<h2>The webserver files <br>need to be updated.</h2>"
                     "Please enter the credentials for your network <br>or upload a new "
                     "filesystem image using the <br>link below."
                     "<head>"
                     "  <title>SmartSpin2K Web Server</title>"
                     "</head>"
                     "<body>"
                     "<form action='/send_settings'>"
                     "<p><label for='ssid'>SSID</label></p>"
                     "<input type='text' id='ssid' name='ssid' value='' />"
                     "<label for='password'>Password</label>"
                     "<input type='text' id='password' name='password' value='' />"
                     "<input type='submit' value='Submit' />"
                     "</form>"
                     "<form action='/reboot.html'>"
                     "<input type='submit' value='Reboot!'>"
                     "</form>"
                     "<p style='text-align: center;'><strong><a href='login'>Update "
                     "Firmware</a></strong></p></h2>"
                     "</body>"
                     "</html> " +
                     OTAStyle;

/* Server Index Page */
String OTAServerIndex = "<body style='font-family: Verdana,sans-serif; font-size: 14px;'>"
                        "<div style='width:400px;padding:20px;border-radius:10px;border:solid 2px #e0e0e0;margin:auto;margin-top:20px;'>"
                        "<div style='width:100%;text-align:center;font-size:18px;font-weight:bold;margin-bottom:12px;'>SmartSpin2k OTA</div>"
                        "<form method='POST' action='#' enctype='multipart/form-data' id='upload-form' style='width:100%;margin-bottom:8px;'>"
                        "<input type='file' name='update'>"
                        "<input type='submit' value='Update' style='float:right;'>"
                        "</form>"
                        "<div style='width:100%;background-color:#e0e0e0;border-radius:8px;'>"
                        "<div id='prg' style='width:0%;background-color:#2196F3;padding:2px;border-radius:8px;color:white;text-align:center;'>0%</div>"
                        "</div>"
                        "<div>Valid files are firmware.bin or littlefs.bin</div>"
                        "</div>"
                        "</body>"
                        "<script>"
                        "var prg = document.getElementById('prg');"
                        "var form = document.getElementById('upload-form');"
                        "form.addEventListener('submit', e=>{"
                        "e.preventDefault();"
                        "var data = new FormData(form);"
                        "var req = new XMLHttpRequest();"
                        "req.open('POST', '/update');"
                        "req.upload.addEventListener('progress', p=>{"
                        "let w = Math.round((p.loaded / p.total)*100) + '%';"
                        "if(p.lengthComputable){"
                        "prg.innerHTML = w;"
                        "prg.style.width = w;"
                        "}"
                        "if(w == '100%'){ prg.style.backgroundColor = '#04AA6D';"
                        "prg.innerHTML = 'Success! You can leave this page.';}"
                        "});"
                        "req.send(data);"
                        "});"
                        "</script>";
