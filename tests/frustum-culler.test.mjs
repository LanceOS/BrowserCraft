import assert from "node:assert/strict";
import test from "node:test";

import { FrustumCuller } from "../dist/engine/render/FrustumCuller.js";
import {
  createMat4,
  lookAtMat4,
  multiplyMat4,
  perspectiveMat4,
} from "../dist/engine/math/mat4.js";

test("frustum culler checks AABBs against identity clip bounds", () => {
  const culler = new FrustumCuller();
  culler.extractFrom(createMat4());

  assert.equal(culler.testAABB([-0.5, -0.5, -0.5], [0.5, 0.5, 0.5]), true);
  assert.equal(culler.testAABB([2, -0.5, -0.5], [3, 0.5, 0.5]), false);
});

test("frustum culler extracts planes from a view-projection matrix", () => {
  const projection = createMat4();
  const view = createMat4();
  const viewProjection = createMat4();
  perspectiveMat4(projection, Math.PI / 3, 1, 0.1, 20);
  lookAtMat4(view, [0, 0, 5], [0, 0, 0], [0, 1, 0]);
  multiplyMat4(viewProjection, projection, view);

  const culler = new FrustumCuller();
  culler.extractFrom(viewProjection);

  assert.equal(culler.testAABB([-1, -1, -1], [1, 1, 1]), true);
  assert.equal(culler.testAABB([0, 0, 6], [1, 1, 7]), false);
  assert.equal(culler.testAABB([-40, -1, 0], [-30, 1, 1]), false);
});
