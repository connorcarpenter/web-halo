// Minimal native-DAG writer. Paths and literal values have distinct escaping.
export function escapePath(value) {
  return String(value).replaceAll('$', () => '$$').replaceAll(' ', '$ ').replaceAll(':', '$:');
}
export function literal(value) {
  if (/[\r\n]/.test(value)) throw new Error('Ninja values cannot contain newlines');
  return value.replaceAll('$', () => '$$');
}
export class Writer {
  lines = [];
  variable(name, value, indent = false) {
    this.lines.push(`${indent ? '  ' : ''}${name} = ${value}`);
  }
  rule(name, fields) {
    this.lines.push(`rule ${name}`);
    for (const [key, value] of Object.entries(fields)) this.variable(key, value, true);
  }
  build(output, rule, inputs = [], implicit = [], variables = {}) {
    this.lines.push(`build ${escapePath(output)}: ${rule} ${inputs.map(escapePath).join(' ')}${implicit.length ? ` | ${implicit.map(escapePath).join(' ')}` : ''}`);
    for (const [key, value] of Object.entries(variables)) this.variable(key, value, true);
  }
  toString() { return `${this.lines.join('\n')}\n`; }
}
