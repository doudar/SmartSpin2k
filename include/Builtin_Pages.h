/*
 * Copyright (C) 2020  Anthony Doud & Joel Baranick
 * All rights reserved
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#pragma once

#include <Arduino.h>

String OTAStyle =
    "<style>"
    "body{background-color:#f0f0f0;font-family:Arial, sans-serif;font-size:14px;color:#333;}"
    "#file-input,input{width:90%;height:44px;border-radius:4px;margin:10px 5%;font-size:15px;}"
    "input{background:#ffffff;border:1px solid #ddd;padding:0 15px;}"
    "#file-input{line-height:44px;text-align:left;display:block;cursor:pointer;color:#333;}"
    "#bar,#prgbar{background-color:#e0e0e0;border-radius:10px;}"
    "#bar{background-color:#4CAF50;width:0%;height:20px;}"
    "form{background:#ffffff;max-width:300px;margin:50px auto;padding:20px;border-radius:8px;text-align:center;box-shadow: 0 4px 8px rgba(0,0,0,0.1);}"
    ".btn{background:#007BFF;color:#ffffff;cursor:pointer;border:none;padding:10px 15px;border-radius:5px;}"
    ".btn:hover{background:#0056b3;}"
    "</style>";

/* Login page */
String OTALoginIndex =
    "<form name='loginForm'>"
    "<h1>Firmware Update Login</h1>"
    "<input name='userid' placeholder='User ID'> "
    "<input name='pwd' placeholder='Password'> "
    "<input type='submit' onclick='check(this.form)' class='btn' value='Login'>"
    "</form>"
    "<script>"
    "function check(form) {"
    "if(form.userid.value=='admin' && form.pwd.value=='admin'){"
    "window.open('/OTAIndex');"
    "}else{"
    "alert('Error: Incorrect Username or Password');"
    "}"
    "}"
    "</script>" +
    OTAStyle;

String noIndexHTML =
    "<!DOCTYPE html>"
    "<html>"
    "<body>"
    "<h2>The webserver files need to be updated.</h2>"
    "Please enter the credentials for your network or upload a new filesystem image using the link below."
    "<form action='/send_settings'>"
    "<p><label for='ssid'>SSID:</label></p>"
    "<input type='text' id='ssid' name='ssid' value='' />"
    "<p><label for='password'>Password:</label></p>"
    "<input type='password' id='password' name='password' value='' />"
    "<input type='submit' value='Submit' class='btn'/>"
    "</form>"
    "<form action='/reboot.html'>"
    "<input type='submit' value='Reboot!' class='btn'/>"
    "</form>"
    "<p style='text-align: center;'><strong><a href='login'>Update Firmware</a></strong></p>"
    "</body>"
    "</html> " +
    OTAStyle;

/* Server Index Page */
String OTAServerIndex =
    "<body>"
    "<div style='width:400px;padding:20px;border-radius:10px;border:solid 2px #e0e0e0;margin:auto;margin-top:20px;background-color:#fff;'>"
    "<div style='text-align:center;font-size:18px;font-weight:bold;margin-bottom:12px;'>SmartSpin2k OTA</div>"
    "<form method='POST' action='#' enctype='multipart/form-data' id='upload-form'>"
    "<input type='file' name='update' style='margin-bottom:10px;'>"
    "<input type='submit' value='Update' class='btn' style='width:100%;'>"
    "</form>"
    "<div style='background-color:#e0e0e0;border-radius:8px;margin-top:10px;'>"
    "<div id='prg' style='width:0%;background-color:#4CAF50;padding:2px;border-radius:8px;color:white;text-align:center;'>0%</div>"
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
    "if(w == '100%'){"
    "prg.style.backgroundColor = '#04AA6D';"
    "prg.innerHTML = 'Success! You can leave this page.';"
    "}"
    "});"
    "req.send(data);"
    "});"
    "</script>";
