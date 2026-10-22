export class A { 
  named(v) { super.value = v; }
  computed(v) { super['value'] = v; }
  static named(v) { super.value = v; }
  static computed(v) { super['value'] = v; }
}
