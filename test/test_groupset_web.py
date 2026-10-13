#
# Copyright (C) 2020  Anthony Doud & Joel Baranick
# All rights reserved
#
# SPDX-License-Identifier: GPL-2.0-only
#

"""Exercise groupset selection and saved custom gearing in both shipped web UIs."""
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
        for variant in ("data", "data_s3"):
            with self.subTest(variant=variant):
                self.check_groupsets(node, ROOT / variant / "settings.html")

    def check_groupsets(self, node, page):
        html = page.read_text(encoding="utf-8")
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
const fields = {groupsetGearing: {}, groupsetDescription: {}};
global.document={getElementById:id=>id==='gearTeeth' ? select : fields[id]};
// Every shipped choice must round-trip saved settings and submit the right field.
for (const preset of groupsetPresets) {
  loadGroupset(preset.teeth, preset.preset || 0);
  assert.equal(select.name, preset.preset ? 'gearPreset' : 'gearTeeth');
  assert.equal(select.value, preset.value);
  assert.equal(fields.groupsetDescription.textContent, preset.description);
  const count = preset.count || preset.teeth.length;
  assert.equal(fields.groupsetGearing.textContent,
    count ? `${preset.gearing} · ${count} virtual gears` : preset.gearing);
}
// Custom gearing must survive loading without silently becoming a preset.
const optionCount = select.options.length;
loadGroupset([3434,3417]);
assert.equal(select.name,'gearTeeth');
assert.deepEqual(JSON.parse(select.value),[3434,3417]);
assert.equal(select.options.length,optionCount + 1);
assert.equal(fields.groupsetGearing.textContent,'2 virtual gears');
assert.match(fields.groupsetDescription.textContent,/custom/i);
// Loading another saved custom set replaces its option rather than accumulating it.
loadGroupset([3630,3615,5012]);
assert.deepEqual(JSON.parse(select.value),[3630,3615,5012]);
assert.equal(select.options.length,optionCount + 1);
loadGroupset([]);
assert.equal(select.name,'gearTeeth');
assert.equal(select.value,'[]');
assert.equal(select.options.length,optionCount);
// Preset ID 1 is a persisted firmware protocol value, independent of menu order.
loadGroupset([],1);
assert.equal(select.name,'gearPreset');
assert.equal(select.value,'1');
assert.equal(select.options.length,optionCount);
console.log(JSON.stringify(groupsetPresets));
'''
        result = subprocess.run([node, "-"], input=script, text=True, capture_output=True, check=True)
        actual = json.loads(result.stdout)
        self.assertTrue(actual, "The user must have groupset choices")
        self.assertEqual(len({preset["value"] for preset in actual}), len(actual))
        self.assertIn(1, [preset.get("preset") for preset in actual])
        for preset in (preset for preset in actual if not preset.get("preset")):
            expected = [front * 100 + rear for front in preset["chainrings"] for rear in preset["cassette"]]
            expected.sort(key=lambda p: (math.floor((p // 100) / (p % 100) * 1000 + 0.5), p))
            self.assertEqual(preset["teeth"], expected)
