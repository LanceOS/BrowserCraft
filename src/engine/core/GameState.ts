export const enum GameState {
  BOOTING,
  MAIN_MENU,
  GENERATING_WORLD,
  IN_GAME,
  PAUSED,
}

export const GameContext = {
  state: GameState.BOOTING,
  worldSeed: 1337,
  renderDistance: 2,
};
