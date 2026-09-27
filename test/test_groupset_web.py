#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

"""Check the shipped JavaScript presets against independent tooth/ratio fixtures."""
from pathlib import Path
import json
import math
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TestGroupsetWeb(unittest.TestCase):
    def test_tooth_presets_and_custom_loading(self):
        node = shutil.which("node")
        if not node:
            self.skipTest("Node.js is required for web groupset tests")
        html = (ROOT / "data/settings.html").read_text(encoding="utf-8")
        self.assertEqual(html, (ROOT / "data_s3/settings.html").read_text(encoding="utf-8"))
        presets = html[html.index("    const groupsetPresets ="):html.index("    function createGroupsetSetting")]
        helpers = html[html.index("    function updateGroupsetDescription"):html.index("    // Generate HTML for text input settings")]
        script = presets + helpers + r'''
const assert = require('node:assert/strict');
const select = {
  options: groupsetPresets.map(p => ({value:p.value,dataset:{}})),
  index:0, name:'gearTeeth',
  get value() { return this.options[this.index].value; },
  set value(value) { this.index=this.options.findIndex(o=>o.value===value); },
  get selectedOptions() { return [this.options[this.index]]; },
  querySelector() { const i=this.options.findIndex(o=>o.dataset.custom); return i<0 ? null : {remove:()=>this.options.splice(i,1)}; },
  appendChild(option) { this.options.push(option); }
};
global.Option = function(label,value) {
  this.value=value; this.dataset={};
  Object.defineProperty(this,'selected',{set:()=>{select.index=select.options.indexOf(this);}});
};
const text = {};
global.document={getElementById:id=>id==='gearTeeth' ? select : text};
loadGroupset([3434,3417]);
assert.equal(select.name,'gearTeeth');
assert.deepEqual(JSON.parse(select.value),[3434,3417]);
loadGroupset([]);
assert.equal(select.name,'gearTeeth');
assert.equal(select.value,'[]');
loadGroupset(groupsetPresets[1].teeth);
assert.equal(select.name,'gearTeeth');
assert.equal(select.index,1);
loadGroupset([],1);
assert.equal(select.name,'gearPreset');
assert.equal(select.value,'1');
assert.equal(select.index,4);
loadGroupset(groupsetPresets[1].teeth);
assert.equal(select.name,'gearTeeth');
assert.equal(select.index,1);
console.log(JSON.stringify(groupsetPresets));
'''
        result = subprocess.run([node, "-"], input=script, text=True, capture_output=True, check=True)
        actual = json.loads(result.stdout)
        self.assertEqual(len(actual), 6)
        self.assertEqual(actual[4]["preset"], 1)
        self.assertEqual(actual[4]["count"], 24)
        self.assertEqual(actual[4]["value"], "1")
        self.assertEqual(actual[5]["chainrings"], [48, 35])
        self.assertEqual(actual[5]["cassette"], [10, 11, 12, 13, 14, 15, 17, 19, 21, 24, 28, 33])
        for preset in actual[:4] + actual[5:]:
            expected = [front * 100 + rear for front in preset["chainrings"] for rear in preset["cassette"]]
            expected.sort(key=lambda p: (math.floor((p // 100) / (p % 100) * 1000 + 0.5), p))
            self.assertEqual(preset["teeth"], expected)
        self.assertEqual(actual[1]["teeth"][0], 3434)
        self.assertEqual(actual[1]["teeth"][-1], 5011)
