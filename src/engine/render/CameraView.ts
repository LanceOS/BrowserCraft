export interface CameraView {
  readonly position: Float32Array;
  readonly forward: Float32Array;
  readonly right: Float32Array;
  readonly up: Float32Array;
  readonly projectionMatrix: Float32Array;
  readonly viewMatrix: Float32Array;
  readonly viewProjectionMatrix: Float32Array;
  readonly inverseViewProjectionMatrix: Float32Array;
}

export interface AudioListenerView {
  readonly position: Float32Array;
  readonly forward: Float32Array;
  readonly up: Float32Array;
}
