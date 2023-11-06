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

const char HTTP_STYLE[] =
    "<style>"
    "html {font-family: sans-serif;display: inline-block;margin: 5px auto;text-align: center;background-color: #03245c;line-height: 1em;}"
    "label {font-size: medium;}"
    "div {font-size: medium;}"
    "a{color: #000000;text-decoration:underline}"
    "a:visited {color: #000000;}"
    "h1{color: #03245c;padding: 0.5rem;line-height: 1em;}"
    "h2{color: #000000;font-size: 1.5rem;font-weight: bold;}"
    "p{font-size: 1rem;}"
    ".button {display: inline-block;background-color: #2a9df4;border: #404040;border-radius: 4px;color: #d0efff;padding: 10px 40px;text-decoration: none;font-size: 20px;margin: "
    "0px;cursor: pointer;}"
    ".button2 {background-color: #f44336;padding: 10px 35px;}"
    ".switch {position: relative;display: inline-block;width: 80px;height: 40px;}"
    ".switch input {display: none;}"
    ".slider {position: absolute;top: 0;left: 0;right: 0;bottom: 0;transition: 0.4s;background-color: #03254c;border-radius: 34px;}"
    ".slider:before {position: absolute;content: \"\";height: 37px;width: 37px;left: 2px;bottom: 2px;background-color: #d0efff;-webkit-transition: 0.4s;transition: "
    "0.4s;border-radius: 68px;}"
    "input:checked + .slider {transition: 0.4s;background-color: #2a9df4;}"
    "input:checked + .slider:before {-webkit-transform: translateX(38px);-ms-transform: translateX(38px);transform: translateX(38px);}"
    ".slider2 {"
    "  -webkit-appearance: none;"
    "  margin: 5px;"
    "  width: 270px;"
    "  height: 20px;"
    "  background: #d0efff;"
    "  outline: none;"
    "  -webkit-transition: 0.2s;"
    "  transition: opacity 0.2s;"
    "}"
    ".slider2::-webkit-slider-thumb {"
    "  -webkit-appearance: none;"
    "  appearance: none;"
    "  width: 30px;"
    "  height: 30px;"
    "  background: #03254c;"
    "  cursor: pointer;"
    "}"
    ".slider2::-moz-range-thumb {"
    "  width: 30px;"
    "  height: 30px;"
    "  background: #1167b1;"
    "  cursor: pointer;"
    "}"
    ""
    "table.center {"
    "  margin-left: auto;"
    "  margin-right: auto;"
    "}"
    ""
    ".tooltip {"
    "  position: relative;"
    "  display: inline-block;"
    "  border-bottom: 1px dotted #03254c;"
    "}"
    ""
    ".tooltip .tooltiptext {"
    "  visibility: hidden;"
    "  width: 120px;"
    "  background-color: #03254c;"
    "  color: #d0efff;"
    "  text-align: center;"
    "  border-radius: 6px;"
    "  padding: 5px 0;"
    "  position: absolute;"
    "  z-index: 1;"
    "}"
    ".tooltip:hover .tooltiptext {"
    "  visibility: visible;"
    "}"
    ""
    ".watermark {"
    "  display: inline;"
    "  position: fixed;"
    "  top: 0px;"
    "  left: 0px;"
    "  transform: translate(calc(50vw - 200px), calc(50vh - 170px)) rotate(45deg);"
    "  transition: 0.4s ease-in-out;"
    "  opacity: 0.7;"
    "  z-index: 99;"
    "  color: grey;"
    "  font-size: 7rem;"
    "}"
    ""
    ".watermark:hidden {"
    "  transition: visibility 0s 2s, opacity 2s linear;"
    "}"
    ""
    ".shiftButton {"
    "  -webkit-appearance: none;"
    "  -webkit-text-stroke: 2px rgba(104, 104, 104, 0.412);"
    "  appearance: auto;"
    "  width: 16%;"
    "  height: 6rem;"
    "  background: #03254c;"
    "  color: white;"
    "  cursor: pointer;"
    "  font-weight: bold;"
    "  font-size: calc(1vw + 1vh);"
    "}"
    ""
    ".shiftBox {"
    "  background: #2a9df4;"
    "  color: white;"
    "  font-weight: bold;"
    "  font-size: calc(1vw + 1vh);"
    "  width: 4%;"
    "  text-align: center;"
    "}"
    ""
    "body {"
    "  display: block;"
    "  margin: 0 auto;"
    "  background-color: #1167b1;"
    "  opacity: 1;"
    "  transition: 0.5s ease-in-out;"
    "  height: 100%;"
    "  width: 100%;"
    "}"
    ""
    "fieldset {"
    "  border: 10px solid;"
    "  border-color: #1e3252;"
    "  box-sizing: border-box;"
    "  grid-area: 1 / 1;"
    "  padding: 5px;"
    "  margin: 0 auto;"
    "  z-index: -1;"
    "}"
    ""
    ".confirmation-dialog {"
    "  display: flex;"
    "  align-items: center;"
    "  justify-content: center;"
    "  position: fixed;"
    "  top: 0;"
    "  left: 0;"
    "  height: 100%;"
    "  width: 100%;"
    "  background-color: rgba(201, 201, 201, 0.7);"
    "  z-index: 100;"
    "}"
    ""
    ".confirmation-dialog > .confirmation-panel {"
    "  background-color: #03245c;"
    "  color: white;"
    "  padding: 20px;"
    "  border-radius: 10px;"
    "}"
    ""
    ".confirmation-panel > .confirmation-buttongroup {"
    "  padding-top: 10px;"
    "}"
    ""
    ".confirmation-buttongroup > input[type=\"button\"] {"
    "  width: 75px;"
    "  height: 40px;"
    "  font-size: 1em;"
    "  border-radius: 10px;"
    "}"
    // quality icons
    ".q{height:16px;margin:0;padding:0 "
    "5px;text-align:right;min-width:38px;float:right}.q.q-0:after{background-position-x:0}.q.q-1:after{background-position-x:-16px}.q.q-2:after{background-position-x:-32px}.q.q-3:"
    "after{background-position-x:-48px}.q.q-4:after{background-position-x:-64px}.q.l:before{background-position-x:-80px;padding-right:5px}.ql "
    ".q{float:left}.q:after,.q:before{content:'';width:16px;height:16px;display:inline-block;background-repeat:no-repeat;background-position: 16px 0;"
    "background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAGAAAAAQCAMAAADeZIrLAAAAJFBMVEX///"
    "8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADHJj5lAAAAC3RSTlMAIjN3iJmqu8zd7vF8pzcAAABsSURBVHja7Y1BCsAwCASNSVo3/v+/"
    "BUEiXnIoXkoX5jAQMxTHzK9cVSnvDxwD8bFx8PhZ9q8FmghXBhqA1faxk92PsxvRc2CCCFdhQCbRkLoAQ3q/wWUBqG35ZxtVzW4Ed6LngPyBU2CobdIDQ5oPWI5nCUwAAAAASUVORK5CYII=');}"
    // icons @2x media query (32px rescaled)
    "@media (-webkit-min-device-pixel-ratio: 2),(min-resolution: 192dpi){.q:before,.q:after {"
    "background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAALwAAAAgCAMAAACfM+KhAAAALVBMVEX///"
    "8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAADAOrOgAAAADnRSTlMAESIzRGZ3iJmqu8zd7gKjCLQAAACmSURBVHgB7dDBCoMwEEXRmKlVY3L//3NLhyzqIqSUggy8uxnhCR5Mo8xLt+14aZ7wwgsvvPA/"
    "ofv9+44334UXXngvb6XsFhO/VoC2RsSv9J7x8BnYLW+AjT56ud/uePMdb7IP8Bsc/"
    "e7h8Cfk912ghsNXWPpDC4hvN+D1560A1QPORyh84VKLjjdvfPFm++i9EWq0348XXnjhhT+4dIbCW+WjZim9AKk4UZMnnCEuAAAAAElFTkSuQmCC');"
    "background-size: 95px 16px;}}"
    // msg callouts
    ".msg{padding:20px;margin:20px 0;border:1px solid #eee;border-left-width:5px;border-left-color:#777}.msg "
    "h4{margin-top:0;margin-bottom:5px}.msg.P{border-left-color:#1fa3ec}.msg.P h4{color:#1fa3ec}.msg.D{border-left-color:#dc3630}.msg.D h4{color:#dc3630}.msg.S{border-left-color: "
    "#5cb85c}.msg.S h4{color: #5cb85c}"
    // lists
    "dt{font-weight:bold}dd{margin:0;padding:0 0 0.5em 0;min-height:12px}"
    "td{vertical-align: top;}"
    ".h{display:none}"
    "button{transition: 0s opacity;transition-delay: 3s;transition-duration: 0s;cursor: pointer}"
    "button.D{background-color:#dc3630}"
    "button:active{opacity:50% !important;cursor:wait;transition-delay: 0s}"
    // links
    "a{color:#000;font-weight:700;text-decoration:none}a:hover{color:#1fa3ec;text-decoration:underline}"
    "</style>";

/* Login page */
const char OTALOGININDEX[] =
    "<form name=loginForm>"
    "<h1>SmartSpin2k Login</h1>"
    "Visit Github for the Username and Password"
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
    "</script>";

const char HTTP_TITLE[] = "<title>SmartSpin2K Web Server</title>";

const char NOINDEXHTML[] =
    "<h2>The webserver files <br>need to be updated.</h2>"
    "Please enter the credentials for your network <br>or upload a new "
    "filesystem image using the <br>link below."
    "<form action='/login'>"
    "<input type='submit' value='Update Firmware'>"
    "</form>";

const char GITHUBLINK[] = "<legend><a href='http://github.com/doudar/SmartSpin2k'>http://github.com/doudar/SmartSpin2k</a></legend>";

/* Server Index Page */
String OTAServerIndex =
    "<body style='font-family: Verdana,sans-serif; font-size: 14px;'>"
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

const char WM_LANGUAGE[] = "en-US";  // i18n lang code

const char HTTP_HEAD_START[] =
    "<!DOCTYPE html>"
    "<html lang='en'><head>"
    "<meta name='format-detection' content='telephone=no'>"
    "<meta charset='UTF-8'>"
    "<meta  name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'/>"
    "<title>{v}</title>";

const char HTTP_SCRIPT[] =
    "<script>function c(l){"
    "document.getElementById('ssid').value=l.getAttribute('data-ssid')||l.innerText||l.textContent;"
    "password = l.nextElementSibling.classList.contains('l');"
    "document.getElementById('password').disabled = !password;"
    "if(password)document.getElementById('password').focus();};"
    "function f() {var x = document.getElementById('password');x.type==='password'?x.type='text':x.type='password';}"
    "</script>";  // @todo add button states, disable on click , show ack , spinner etc

const char HTTP_HEAD_END[] =
    "</head><body class='{c}'><fieldset><legend><a href='http://github.com/doudar/SmartSpin2k'>http://github.com/doudar/SmartSpin2k</a></legend><p style=\"text-align: "
    "left;\"><strong><a href=\"index.html\">Main Index</a></strong></p>{1}<div class='wrap'>";  // {c} = _bodyclass

const char HTTP_ROOT_MAIN[] = "<h1>{t}</h1><h3>{v}</h3>";

const char* const HTTP_PORTAL_MENU[] = {
    "<form action='/wifi'    method='get'><button>Configure WiFi</button></form><br/>\n",            // MENU_WIFI
    "<form action='/0wifi'   method='get'><button>Configure WiFi (No scan)</button></form><br/>\n",  // MENU_WIFINOSCAN
    "<form action='/info'    method='get'><button>Info</button></form><br/>\n",                      // MENU_INFO
    "<form action='/param'   method='get'><button>Setup</button></form><br/>\n",                     // MENU_PARAM
    "<form action='/close'   method='get'><button>Close</button></form><br/>\n",                     // MENU_CLOSE
    "<form action='/restart' method='get'><button>Restart</button></form><br/>\n",                   // MENU_RESTART
    "<form action='/exit'    method='get'><button>Exit</button></form><br/>\n",                      // MENU_EXIT
    "<form action='/erase'   method='get'><button class='D'>Erase</button></form><br/>\n",           // MENU_ERASE
    "<form action='/update'  method='get'><button>Update</button></form><br/>\n",                    // MENU_UPDATE
    "<hr><br/>"                                                                                      // MENU_SEP
};

// const char HTTP_PORTAL_OPTIONS[]    = strcat(HTTP_PORTAL_MENU[0] , HTTP_PORTAL_MENU[3] , HTTP_PORTAL_MENU[7]);
const char HTTP_PORTAL_OPTIONS[] = "";
const char HTTP_ITEM_QI[]        = "<div role='img' aria-label='{r}%' title='{r}%' class='q q-{q} {i} {h}'></div>";  // rssi icons
const char HTTP_ITEM_QP[]        = "<div class='q {h}'>{r}%</div>";                                                  // rssi percentage {h} = hidden showperc pref
const char HTTP_ITEM[]           = "<div><a href='#p' onclick='c(this)' data-ssid='{V}'>{v}</a>{qi}{qp}</div>";      // {q} = HTTP_ITEM_QI, {r} = HTTP_ITEM_QP
// const char HTTP_ITEM[]             = "<div><a href='#p' onclick='c(this)'>{v}</a> {R} {r}% {q} {e}</div>"; // test all tokens

const char HTTP_FORM_START[] = "<form method='POST' action='{v}'>";
const char HTTP_FORM_WIFI[] =
    "<label for='ssid'>SSID</label><input id='ssid' name='ssid' maxlength='32' autocorrect='off' autocapitalize='none' placeholder='{v}'><br/><label "
    "for='password'>Password</label><input id='password' "
    "name='password' maxlength='64' type='password' placeholder='password'><input type='checkbox' onclick='f()'> Show Password";
const char HTTP_FORM_WIFI_END[]    = "";
const char HTTP_FORM_STATIC_HEAD[] = "<hr><br/>";
const char HTTP_FORM_END[]         = "<br/><br/><button type='submit'>Save</button></form>";
const char HTTP_FORM_LABEL[]       = "<label for='{i}'>{t}</label>";
const char HTTP_FORM_PARAM_HEAD[]  = "<hr><br/>";
const char HTTP_FORM_PARAM[]       = "<br/><input id='{i}' name='{n}' maxlength='{l}' value='{v}' {c}>\n";  // do not remove newline!

const char HTTP_SCAN_LINK[]   = "<br/><form action='/wifi?refresh=1' method='POST'><button name='refresh' value='1'>Refresh</button></form>";
const char HTTP_REBOOT_LINK[] = "<br><form action='/reboot.html'> <input type='submit' value='Reboot!'></form>";

const char HTTP_SAVED[]      = "<div class='msg'>Saving Credentials<br/>Trying to connect ESP to network.<br />If it fails reconnect to AP to try again</div>";
const char HTTP_PARAMSAVED[] = "<div class='msg S'>Saved<br/></div>";
const char HTTP_END[]        = "</div></fieldset></body></html>";
const char HTTP_ERASEBTN[]   = "<br/><form action='/erase' method='get'><button class='D'>Erase WiFi config</button></form>";
const char HTTP_UPDATEBTN[]  = "<br/><form action='/update' method='get'><button>Update</button></form>";
const char HTTP_BACKBTN[]    = "<hr><br/><form action='/' method='get'><button>Back</button></form>";

const char HTTP_STATUS_ON[]      = "<div class='msg S'><strong>Connected</strong> to {v}<br/><em><small>with IP {i}</small></em></div>";
const char HTTP_STATUS_OFF[]     = "<div class='msg {c}'><strong>Not connected</strong> to {v}{r}</div>";  // {c=class} {v=ssid} {r=status_off}
const char HTTP_STATUS_OFFPW[]   = "<br/>Authentication failure";                                          // STATION_WRONG_PASSWORD,  no eps32
const char HTTP_STATUS_OFFNOAP[] = "<br/>AP not found";                                                    // WL_NO_SSID_AVAIL
const char HTTP_STATUS_OFFFAIL[] = "<br/>Could not connect";                                               // WL_CONNECT_FAILED
const char HTTP_STATUS_NONE[]    = "<div class='msg'>No AP set</div>";
const char HTTP_BR[]             = "<br/>";

const char HTTP_UPDATE_FAIL[]    = "<div class='msg D'><strong>Update failed!</strong><Br/>Reboot device and try again</div>";
const char HTTP_UPDATE_SUCCESS[] = "<div class='msg S'><strong>Update successful.  </strong> <br/> Device rebooting now...</div>";

const char HTTP_INFO_esphead[]   = "<h3>esp32</h3><hr><dl>";
const char HTTP_INFO_chiprev[]   = "<dt>Chip rev</dt><dd>{1}</dd>";
const char HTTP_INFO_lastreset[] = "<dt>Last reset reason</dt><dd>CPU0: {1}<br/>CPU1: {2}</dd>";
const char HTTP_INFO_aphost[]    = "<dt>Access point hostname</dt><dd>{1}</dd>";
const char HTTP_INFO_psrsize[]   = "<dt>PSRAM Size</dt><dd>{1} bytes</dd>";
const char HTTP_INFO_temp[]      = "<dt>Temperature</dt><dd>{1} C&deg; / {2} F&deg;</dd>";
const char HTTP_INFO_hall[]      = "<dt>Hall</dt><dd>{1}</dd>";

const char HTTP_INFO_memsmeter[] = "<br/><progress value='{1}' max='{2}'></progress></dd>";
const char HTTP_INFO_memsketch[] = "<dt>Memory - Sketch size</dt><dd>Used / Total bytes<br/>{1} / {2}";
const char HTTP_INFO_freeheap[]  = "<dt>Memory - Free heap</dt><dd>{1} bytes available</dd>";
const char HTTP_INFO_wifihead[]  = "<br/><h3>WiFi</h3><hr>";
const char HTTP_INFO_uptime[]    = "<dt>Uptime</dt><dd>{1} mins {2} secs</dd>";
const char HTTP_INFO_chipid[]    = "<dt>Chip ID</dt><dd>{1}</dd>";
const char HTTP_INFO_idesize[]   = "<dt>Flash size</dt><dd>{1} bytes</dd>";
const char HTTP_INFO_sdkver[]    = "<dt>SDK version</dt><dd>{1}</dd>";
const char HTTP_INFO_cpufreq[]   = "<dt>CPU frequency</dt><dd>{1}MHz</dd>";
const char HTTP_INFO_apip[]      = "<dt>Access point IP</dt><dd>{1}</dd>";
const char HTTP_INFO_apmac[]     = "<dt>Access point MAC</dt><dd>{1}</dd>";
const char HTTP_INFO_apssid[]    = "<dt>Access point SSID</dt><dd>{1}</dd>";
const char HTTP_INFO_apbssid[]   = "<dt>BSSID</dt><dd>{1}</dd>";
const char HTTP_INFO_stassid[]   = "<dt>Station SSID</dt><dd>{1}</dd>";
const char HTTP_INFO_staip[]     = "<dt>Station IP</dt><dd>{1}</dd>";
const char HTTP_INFO_stagw[]     = "<dt>Station gateway</dt><dd>{1}</dd>";
const char HTTP_INFO_stasub[]    = "<dt>Station subnet</dt><dd>{1}</dd>";
const char HTTP_INFO_dnss[]      = "<dt>DNS Server</dt><dd>{1}</dd>";
const char HTTP_INFO_host[]      = "<dt>Hostname</dt><dd>{1}</dd>";
const char HTTP_INFO_stamac[]    = "<dt>Station MAC</dt><dd>{1}</dd>";
const char HTTP_INFO_conx[]      = "<dt>Connected</dt><dd>{1}</dd>";
const char HTTP_INFO_autoconx[]  = "<dt>Autoconnect</dt><dd>{1}</dd>";

const char HTTP_INFO_aboutarduino[] = "<dt>Arduino</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutsdk[]     = "<dt>ESP-SDK/IDF</dt><dd>{1}</dd>";
const char HTTP_INFO_aboutdate[]    = "<dt>Build date</dt><dd>{1}</dd>";

const char WIFI_PAGE_TITLE[]     = "<h1><strong>WiFi Setup</strong></h1>";
const char S_y[]                 = "Yes";
const char S_n[]                 = "No";
const char S_enable[]            = "Enabled";
const char S_disable[]           = "Disabled";
const char S_GET[]               = "GET";
const char S_POST[]              = "POST";
const char S_NA[]                = "Unknown";
const char S_passph[]            = "********";
const char S_titlewifisaved[]    = "Credentials saved";
const char S_titlewifisettings[] = "Settings saved";
const char S_titlewifi[]         = "Config ESP";
const char S_titleinfo[]         = "Info";
const char S_titleparam[]        = "Setup";
const char S_titleparamsaved[]   = "Setup saved";
const char S_titleexit[]         = "Exit";
const char S_titlereset[]        = "Reset";
const char S_titleerase[]        = "Erase";
const char S_titleclose[]        = "Close";
const char S_options[]           = "options";
const char S_nonetworks[]        = "No networks found. Refresh to scan again.";
const char S_staticip[]          = "Static IP";
const char S_staticgw[]          = "Static gateway";
const char S_staticdns[]         = "Static DNS";
const char S_subnet[]            = "Subnet";
const char S_exiting[]           = "Exiting";
const char S_resetting[]         = "Module will reset in a few seconds.";
const char S_closing[]           = "You can close the page, portal will continue to run";
const char S_error[]             = "An error occured";
const char S_notfound[]          = "File not found\n\n";
const char S_uri[]               = "URI: ";
const char S_method[]            = "\nMethod: ";
const char S_args[]              = "\nArguments: ";
const char S_parampre[]          = "param_";

// debug strings
const char D_HR[] = "--------------------";

const char S_ssidpre[] = "ESP32";

const char WM_VERSION_STR[] = "v2.0.15-rc.1";

static const char _wifi_token[]       = "wifi";
static const char _wifinoscan_token[] = "wifinoscan";
static const char _info_token[]       = "info";
static const char _param_token[]      = "param";
static const char _close_token[]      = "close";
static const char _restart_token[]    = "restart";
static const char _exit_token[]       = "exit";
static const char _erase_token[]      = "erase";
static const char _update_token[]     = "update";
static const char _sep_token[]        = "sep";
static const char _custom_token[]     = "custom";
static PGM_P _menutokens[]            = {_wifi_token, _wifinoscan_token, _info_token,   _param_token, _close_token, _restart_token,
                                         _exit_token, _erase_token,      _update_token, _sep_token,   _custom_token};
const uint8_t _nummenutokens          = (sizeof(_menutokens) / sizeof(PGM_P));

const char R_root[]       = "/";
const char R_wifi[]       = "/wifi";
const char R_wifinoscan[] = "/0wifi";
const char R_wifisave[]   = "/send_settings";
const char R_info[]       = "/info";
const char R_param[]      = "/param";
const char R_paramsave[]  = "/paramsave";
const char R_restart[]    = "/restart";
const char R_exit[]       = "/exit";
const char R_close[]      = "/close";
const char R_erase[]      = "/erase";
const char R_status[]     = "/status";
const char R_update[]     = "/update";
const char R_updatedone[] = "/u";

// Strings
const char S_ip[]  = "ip";
const char S_gw[]  = "gw";
const char S_sn[]  = "sn";
const char S_dns[] = "dns";

// Tokens
//@todo consolidate and reduce
const char T_ss[] = "{";    // token start sentinel
const char T_es[] = "}";    // token end sentinel
const char T_1[]  = "{1}";  // @token 1
const char T_2[]  = "{2}";  // @token 2
const char T_3[]  = "{3}";  // @token 2
const char T_v[]  = "{v}";  // @token v
const char T_V[]  = "{V}";  // @token v
const char T_I[]  = "{I}";  // @token I
const char T_i[]  = "{i}";  // @token i
const char T_n[]  = "{n}";  // @token n
const char T_p[]  = "{p}";  // @token p
const char T_t[]  = "{t}";  // @token t
const char T_l[]  = "{l}";  // @token l
const char T_c[]  = "{c}";  // @token c
const char T_e[]  = "{e}";  // @token e
const char T_q[]  = "{q}";  // @token q
const char T_r[]  = "{r}";  // @token r
const char T_R[]  = "{R}";  // @token R
const char T_h[]  = "{h}";  // @token h

// http
const char HTTP_HEAD_CL[]             = "Content-Length";
const char HTTP_HEAD_CT[]             = "text/html";
const char HTTP_HEAD_CT2[]            = "text/plain";
const char HTTP_HEAD_CORS[]           = "Access-Control-Allow-Origin";
const char HTTP_HEAD_CORS_ALLOW_ALL[] = "*";

const char* const WIFI_STA_STATUS[]{
    "WL_IDLE_STATUS",            // 0 STATION_IDLE
    "WL_NO_SSID_AVAIL",          // 1 STATION_NO_AP_FOUND
    "WL_SCAN_COMPLETED",         // 2
    "WL_CONNECTED",              // 3 STATION_GOT_IP
    "WL_CONNECT_FAILED",         // 4 STATION_CONNECT_FAIL, STATION_WRONG_PASSWORD(NI)
    "WL_CONNECTION_LOST",        // 5
    "WL_DISCONNECTED",           // 6
    "WL_STATION_WRONG_PASSWORD"  // 7 KLUDGE
};

const char* const AUTH_MODE_NAMES[]{"OPEN", "WEP", "WPA_PSK", "WPA2_PSK", "WPA_WPA2_PSK", "WPA2_ENTERPRISE", "MAX"};

const char* const WIFI_MODES[] = {"NULL", "STA", "AP", "STA+AP"};