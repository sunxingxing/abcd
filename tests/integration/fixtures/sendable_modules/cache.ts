import library from './dependency.mjs';
export class Cache {
  lock_: library.Lock;
  books_: library.Store;
  constructor() {
    "use sendable";
    this.lock_ = new library.Lock();
    this.books_ = new library.Store();
    this.books_.put('initial', 'one');
  }
  async put(key, value) {
    return this.lock_.run(() => { this.books_.put(key, value); return value; });
  }
  async get(key) {
    return this.lock_.run(() => this.books_.get(key));
  }
  async keys() {
    return this.lock_.run(() => this.books_.keys());
  }
}
