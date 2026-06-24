type EventMap = Record<string, unknown>;
type EventListener<T> = (payload: T) => void;

export class EventBus<TEvents extends EventMap> {
  private readonly listeners = new Map<keyof TEvents, Set<EventListener<unknown>>>();

  on<TKey extends keyof TEvents>(type: TKey, listener: EventListener<TEvents[TKey]>): () => void {
    let bucket = this.listeners.get(type);
    if (!bucket) {
      bucket = new Set<EventListener<unknown>>();
      this.listeners.set(type, bucket);
    }
    bucket.add(listener as EventListener<unknown>);
    return () => bucket?.delete(listener as EventListener<unknown>);
  }

  emit<TKey extends keyof TEvents>(type: TKey, payload: TEvents[TKey]): void {
    const bucket = this.listeners.get(type);
    if (!bucket) return;
    for (const listener of bucket) {
      (listener as EventListener<TEvents[TKey]>)(payload);
    }
  }
}
