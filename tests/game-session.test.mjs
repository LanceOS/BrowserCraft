import assert from "node:assert/strict";
import test from "node:test";

import { GameState } from "../dist/engine/core/GameState.js";
import { GameSession } from "../dist/game/GameSession.js";

test("game session advances through the singleplayer lifecycle", () => {
  const session = new GameSession(6);

  session.enterMainMenu();
  assert.equal(session.state, GameState.MAIN_MENU);

  session.startSingleplayer("creative");
  assert.equal(session.state, GameState.GENERATING_WORLD);
  assert.equal(session.gameMode, "creative");
  assert.equal(session.startRequestId, 1);

  assert.equal(session.markWorldReady(), true);
  assert.equal(session.state, GameState.IN_GAME);

  assert.equal(session.pause(), true);
  assert.equal(session.state, GameState.PAUSED);

  assert.equal(session.resume(), true);
  assert.equal(session.state, GameState.IN_GAME);

  session.returnToTitle();
  assert.equal(session.state, GameState.MAIN_MENU);
});

test("game session clamps render distance updates", () => {
  const session = new GameSession(1);
  assert.equal(session.renderDistance, 2);
  assert.equal(session.gameMode, "survival");

  session.setRenderDistance(128);
  assert.equal(session.renderDistance, 4);

  session.setRenderDistance(Number.NaN);
  assert.equal(session.renderDistance, 2);
});
