export function f(g,o,a){g(...a);o.m(...a);return new g(...a);}
export class A{constructor(...a){this.a=a;}}
export class B extends A{constructor(...a){const f=()=>super(...a);f();} m(k,v){super[k]=v;super.p=v;return super[k]+super.p;}}
