#!/opt/bin/mocha --ui=tdd

'use strict';

let assert = require('assert')
let cp = require('child_process')

let out = '_build.x86_64'

let t = function(name) {
    let r = cp.spawnSync(`${__dirname}/../${out}/test/battery`,
			 [`${__dirname}/${name}.txt`])
//    console.log(r)
    if (r.status !== 0) throw new Error(`${name} exit status is ${r.status}`)
    return r.stdout.toString().trim()
}

suite('Battery', function() {
    test('discharging', function() {
	assert.equal(t('on.regular'), "0 89 9156 2:32")
	assert.equal(t('on.min'), "0 30 7772 2:9")
	assert.equal(t('on.min.100'), "0 100 0 0:0")
	assert.equal(t('on.100'), "0 30 7772 2:9")
	assert.equal(t('on.negative-power'), "0 75 18684 5:11")
	assert.equal(t('on.73'), "0 73 4121 1:8")
//	assert.equal(t('on.'), "")
    })

    test('charging', function() {
	assert.equal(t('off.charging.0-rate'), "1 45 0 0:0")
	assert.equal(t('off.no-charging'), "0 89 0 0:0")
	assert.equal(t('off.regular'), "1 21 3936 1:5")
	assert.equal(t('off.charging.mAh'), "1 96 501 0:8")
	assert.equal(t('off.123'), "0 100 0 0:0")
//	assert.equal(t('off.'), "")
    })
})
