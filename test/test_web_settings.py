#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

"""Exercise production browser save batching and recovery without a device."""
from pathlib import Path
import re
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestWebSettings(unittest.TestCase):
    def test_save_batching(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node is required for browser regression tests")
        html = (ROOT / "data/settings.html").read_text()
        code = html[html.index("    function queueSetting"):html.index("    window.addEventListener('DOMContentLoaded'")]
        script = r'''
const assert = require('node:assert/strict');
let debounceTimer, saveInFlight=false, settingsLoaded=true;
const dirtySettings=new Map();
let requests=[], replies=[], messages=[];
const password={value:''};
global.document={getElementById:()=>password};
global.showSaveStatus=(...args)=>messages.push(args);
global.fetch=(url,options)=>new Promise(resolve=>{requests.push({url,options});replies.push(resolve);});
const field=(name,value,type='number')=>({name,value,type,checked:value});
''' + code + r'''
(async()=>{
  sendSetting(field('shiftStep','123'));
  sendSetting(field('stepperPower','700'));
  const first=flushSettings();
  assert.equal(requests.length,1);
  assert.equal(requests[0].url,'/send_settings');
  assert.equal(requests[0].options.method,'POST');
  assert.equal(requests[0].options.body.get('shiftStep'),'123');
  assert.equal(requests[0].options.body.get('stepperPower'),'700');
  sendSetting(field('shiftStep','456'));
  sendSetting(field('stealthChop',false,'checkbox'));
  await flushSettings(); assert.equal(requests.length,1);
  replies.shift()({ok:true}); await first;
  assert.equal(requests.length,2);
  assert.equal(requests[1].options.body.get('shiftStep'),'456');
  assert.equal(requests[1].options.body.get('stealthChop'),'false');
  queueSetting(field('shiftStep','789'));
  replies.shift()({ok:false,text:async()=> 'storage failure'});
  await new Promise(resolve=>setImmediate(resolve));
  assert.equal(dirtySettings.get('shiftStep'),'789');
  assert.equal(dirtySettings.get('stealthChop'),'false');
  assert.equal(requests.length,2); // Failed writes do not spin in a retry loop.
  const retry=flushSettings(); replies.shift()({ok:true}); await retry;
  assert.equal(dirtySettings.size,0);
  queueSetting(field('gearTeeth','[]'));
  queueSetting(field('gearPreset','1'));
  assert(!dirtySettings.has('gearTeeth'));
  assert.equal(dirtySettings.get('gearPreset'),'1');
  queueSetting(field('password',''));
  queueSetting({...field('password','secret'), disabled:true});
  assert(!dirtySettings.has('password'));
  password.value='new password';
  queueSetting(field('password', password.value));
  dirtySettings.delete('gearPreset');
  const passwordSave=flushSettings();
  assert.equal(requests.at(-1).url, '/send_settings');
  assert.equal(requests.at(-1).options.method, 'POST');
  assert.equal(requests.at(-1).options.body.get('password'), 'new password');
  replies.shift()({ok:true}); await passwordSave;
  assert.equal(password.value, '');
  clearTimeout(debounceTimer);
})().catch(error=>{console.error(error);process.exitCode=1;});
'''
        subprocess.run([node, "-"], input=script, text=True, check=True)

    def test_script_syntax_and_asset_parity(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node is required")
        for name in ("settings", "status", "shift", "btsimulator"):
            html = (ROOT / f"data/{name}.html").read_text()
            self.assertEqual(html, (ROOT / f"data_s3/{name}.html").read_text())
            for script in re.findall(r"<script[^>]*>(.*?)</script>", html, re.S):
                subprocess.run([node, "--check"], input=script, text=True, check=True)

    def test_status_timeout_releases_polling(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node is required")
        html = (ROOT / "data/status.html").read_text()
        code = html[html.index("    async function requestValues"):html.index("    let logRenderTimer")]
        script = r'''
const assert=require('node:assert/strict');
let updatePending=false, timeout, requests=0, updates=0;
global.document={hidden:false};
global.setTimeout=callback=>{timeout=callback;return 1;};
global.clearTimeout=()=>{};
global.updateValuesFromJson=()=>++updates;
global.fetch=(url,{signal})=>{
  ++requests;
  return new Promise((resolve,reject)=>signal.addEventListener('abort',()=>reject(new Error('timeout'))));
};
console.error=()=>{};
''' + code + r'''
(async()=>{
  const stalled=requestConfigValues();
  await requestRuntimeValues();assert.equal(requests,1);
  timeout();await stalled;assert.equal(updatePending,false);
  global.fetch=async()=>({ok:true,json:async()=>({watts:250})});
  await requestRuntimeValues();assert.equal(updates,1);assert.equal(updatePending,false);
  document.hidden=true;await requestRuntimeValues();assert.equal(updates,1);
})().catch(error=>{process.stderr.write(String(error));process.exitCode=1;});
'''
        subprocess.run([node, "-"], input=script, text=True, check=True)
