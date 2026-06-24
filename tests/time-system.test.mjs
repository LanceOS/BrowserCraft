import assert from "node:assert/strict";
import test from "node:test";

import { TimeSystem } from "../dist/engine/ecs/systems/TimeSystem.js";

const createUniformRecorder = () => {
  const uploads = [];
  return {
    uploads,
    upload(data) {
      uploads.push(Array.from(data));
    },
  };
};

test("time system calculates daylight and uploads sun direction", () => {
  const ubo = createUniformRecorder();
  const time = new TimeSystem(ubo);

  assert.equal(time.currentTimeOfDay, 0.25);
  assert.ok(time.daylightFactor > 0);
  assert.ok(time.lightLevel > 4);
  assert.ok(time.lightLevel <= 15);

  const upload = ubo.uploads.at(-1);
  assert.ok(upload);
  assert.equal(upload[1], time.sunAngle);
  assert.ok(Math.abs(upload[4] - 1) < 1e-6);
  assert.ok(Math.abs(upload[5]) < 1e-6);
  assert.equal(upload[6], 0.20000000298023224);
});

test("time system skips night back to morning", () => {
  const ubo = createUniformRecorder();
  const time = new TimeSystem(ubo);

  time.update(1200 * 0.6);
  assert.equal(time.currentTimeOfDay, 0.85);
  assert.equal(time.isNight, true);

  time.skipToMorning();

  assert.equal(time.currentTimeOfDay, 0.25);
  assert.equal(time.isNight, false);
  assert.ok(ubo.uploads.length >= 3);
});
