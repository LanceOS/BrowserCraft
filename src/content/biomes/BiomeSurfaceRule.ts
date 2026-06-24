export interface BiomeSurfaceRule {
  readonly name: string;
  readonly topBlock: number;
  readonly fillerBlock: number;
  readonly depth: number;
  readonly heightBias: number;
}
