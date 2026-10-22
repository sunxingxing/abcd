export function dynamic(o,hook){if(o===null||o===undefined)throw({}=null);const key=""+hook();const {[key]:value,...rest}=o;return [value,rest];}
