console.log('Loading example-node-addon.node')

const {fib, Vec3} = require('./build/RelWithDebInfo/example-node-addon.node');

console.log('Loaded example-node-addon.node')

console.log("fib(10) =", fib(10));

const tests = [
    [0, 0],
    [1, 1], [2, 1],
    [5, 5],
    [10, 55],
];
for (const [n, expected] of tests) {
    const result = fib(n);
    if (result !== expected) {
        throw new Error(`fib(${n}) = ${result}, expected ${expected}`);
    }
}

let v3 = new Vec3(0.5, 3, 2.0);
console.log(`v3: ${v3.x}, ${v3.y}, ${v3.z}`);
if (v3.x !== 0.5 || v3.y !== 3 || v3.z !== 2.0) {
    throw new Error("Vec3 constructor failed");
}

console.log("All tests passed!");