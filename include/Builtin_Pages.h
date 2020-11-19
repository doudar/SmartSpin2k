// SmartSpin2K code
// This software registers an ESP32 as a BLE FTMS device which then uses a stepper motor to turn the resistance knob on a regular spin bike.
// BLE code based on examples from https://github.com/nkolban
// Copyright 2020 Anthony Doud
// This work is licensed under the GNU General Public License v2
// Prototype hardware build from plans in the SmartSpin2k repository are licensed under Cern Open Hardware Licence version 2 Permissive
// 


//OTA Update pages. Not stored in SPIFFS because we will use this to restore the webserver files if they get corrupt.
/* Style */
#ifndef BUILTIN_PAGES_H
#define BUILTIN_PAGES_H



#include <Arduino.h>

String OTAStyle =
    "<style>#file-input,input{width:100%;height:44px;border-radius:4px;margin:10px auto;font-size:15px}"
    "input{background:#f1f1f1;border:0;padding:0 15px}body{background:#3498db;font-family:sans-serif;font-size:14px;color:#777}"
    "#file-input{padding:0;border:1px solid #ddd;line-height:44px;text-align:left;display:block;cursor:pointer}"
    "#bar,#prgbar{background-color:#f1f1f1;border-radius:10px}#bar{background-color:#3498db;width:0%;height:10px}"
    "form{background:#fff;max-width:258px;margin:75px auto;padding:30px;border-radius:5px;text-align:center}"
    ".btn{background:#3498db;color:#fff;cursor:pointer}</style>";

/* Login page */
String OTALoginIndex =
    "<form name=loginForm>"
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

String noIndexHTML =
    "<!DOCTYPE html>"
    "<html>"
    "<body>"
    "The webserver files need to be updated."
    "Please enter the credentials for your network or upload a new filesystem image using the link below."
    "<head>"
    "  <title>SmartSpin2K Web Server</title>"
    "  <meta name='viewport' content='width=device-width, initial-scale=1'>"
    " <link rel='icon' href='data:,'>"
    "</head>"
    "<body>"
    "  <h1><strong>Network Settings</strong></h1>"
    "  <h2>"
    "    <p style='text-align: left;'></p>"
    "    <form action='/send_settings'>"
    "      <table border='0' style='height: 30px; width: 36.0582%; border-collapse: collapse; border-style: none;'"
    "        height='182' cellspacing='1'>"
    "        <tbody>"
    "          <tr style='height: 20px;'>"
    "            <td style='width: 14%; height: 20px;'>"
    "              <p><label for='ssid'>SSID</label></p>"
    "            </td>"
    "            <td style='width: 50%; height: 20px;'><input type='text' id='ssid' name='ssid' value='' /></td>"
    "          </tr>"
    "          <tr style='height: 20px;'>"
    "            <td style='width: 14%; height: 20px;'>"
    "              <p><label for='password'>Password</label></p>"
    "            </td>"
    "            <td style='width: 50%; height: 20px;'><input type='text' id='password' name='password' value='' />"
    "            </td>"
    "          </tr>"
    "          </tr>"
    "        </tbody>"
    "      </table>"
    "      <input type='submit' value='Submit' />"
    "    </form>"
    "<p style='text-align: center;'><strong><a href='login'>Update Firmware</a></strong></p></h2>"
    "<p><br></p>"
    "    <form action='/reboot.html'>"
    "      <input type='submit' value='Reboot!'>"
    "    </form>"
    "</body>"
    "</html> " +
    OTAStyle;

/* Server Index Page */
String OTAServerIndex =
    "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
    "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
    "<input type='file' name='update' id='file' onchange='sub(this)' style=display:none>"
    "<label id='file-input' for='file'>   Choose file...</label>"
    "<input type='submit' class=btn value='Update'>"
    "<br><br>"
    "<div id='prg'></div>"
    "<br><div id='prgbar'><div id='bar'></div></div><br></form>"
    "<script>"
    "function sub(obj){"
    "var fileName = obj.value.split('\\\\');"
    "document.getElementById('file-input').innerHTML = '   '+ fileName[fileName.length-1];"
    "};"
    "$('form').submit(function(e){"
    "e.preventDefault();"
    "var form = $('#upload_form')[0];"
    "var data = new FormData(form);"
    "$.ajax({"
    "url: '/update',"
    "type: 'POST',"
    "data: data,"
    "contentType: false,"
    "processData:false,"
    "xhr: function() {"
    "var xhr = new window.XMLHttpRequest();"
    "xhr.upload.addEventListener('progress', function(evt) {"
    "if (evt.lengthComputable) {"
    "var per = evt.loaded / evt.total;"
    "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
    "$('#bar').css('width',Math.round(per*100) + '%');"
    "}"
    "}, false);"
    "return xhr;"
    "},"
    "success:function(d, s) {"
    "console.log('success!') "
    "},"
    "error: function (a, b, c) {"
    "}"
    "});"
    "});"
    "</script>" +
    OTAStyle;

    #endif