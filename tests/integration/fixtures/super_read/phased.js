export class Child extends globalThis.TestBase {
  run(arg) { try { return super.value(arg()); } catch (error) { return error.name; } }
  read() { return super.value; }
  static run(arg) { try { return super.value(arg()); } catch (error) { return error.name; } }
}
