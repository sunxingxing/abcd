export class A{#x=1;#m(){return this.#x;} static #y=2;get(o){return #x in o ? o.#m():A.#y;}set(v){this.#x=v;}init=()=>this.#x;}
